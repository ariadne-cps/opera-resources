/***************************************************************************
 *            process.cpp
 *
 *  Copyright  2021  Luca Geretti
 *
 ****************************************************************************/

/*
 * This file is part of Opera, under the MIT license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "thread.hpp"
#include "scenario_utility.hpp"
#include "message.hpp"
#include "deserialisation.hpp"
#include "kafka.hpp"
#include "mqtt.hpp"
#include "memory.hpp"
#include "conclog/include/logging.hpp"
#include "runtime.hpp"
#include "command_line_interface.hpp"

using namespace Opera;

void process(Pair<BrokerAccess,BodyPresentationTopic> const& bp_access, Pair<BrokerAccess,HumanStateTopic> const& hs_access,
             Pair<BrokerAccess,RobotStateTopic> const& rs_access, Pair<BrokerAccess,CollisionNotificationTopic> const& cn_access,
             String const& scenario_t, String const& scenario_k, SizeType const& speedup, SizeType const& concurrency, LookAheadJobFactory const& job_factory) {

    BodyPresentationMessage rp = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/robot/presentation.json")).make();
    BodyPresentationMessage hp = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/human/presentation.json")).make();

    Runtime runtime(bp_access, hs_access, rs_access, cn_access, job_factory, concurrency*speedup);

    List<CollisionNotificationMessage> collisions;

    auto* cn_subscriber = cn_access.first.make_collision_notification_subscriber([&](auto p){
        collisions.push_back(p);
    },cn_access.second);

    auto bp_publisher = bp_access.first.make_body_presentation_publisher(bp_access.second);
    std::this_thread::sleep_for(std::chrono::milliseconds (1000));
    bp_publisher->put(rp);
    //bp_publisher->put(hp);
    std::this_thread::sleep_for(std::chrono::milliseconds (1000));
    delete bp_publisher;

    auto first_human_state = Deserialiser<HumanStateMessage>(ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/0.json")).make();
    auto sync_timestamp = first_human_state.timestamp();

    auto rs_publisher = rs_access.first.make_robot_state_publisher(rs_access.second);
    SizeType idx = 0;
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(idx++)+".json");
        if (not exists(filepath)) break;
        auto msg = Deserialiser<RobotStateMessage>(filepath).make();
        if (msg.timestamp() > sync_timestamp) {
            --idx;
            break;
        }
        rs_publisher->put(msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds (1000));
    delete rs_publisher;

    std::this_thread::sleep_for(std::chrono::milliseconds (10));

    CONCLOG_PRINTLN("Robot messages inserted up to sync timestamp of " << sync_timestamp << " at message #" << idx)

    std::deque<RobotStateMessage> robot_messages;
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(idx++)+".json");
        if (not exists(filepath)) break;
        robot_messages.push_back(Deserialiser<RobotStateMessage>(filepath).make());
    }

    std::deque<HumanStateMessage> human_messages;
    SizeType human_idx = 0;
    TimestampType latest_timestamp = 0;
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/"+std::to_string(human_idx++)+".json");
        if (not exists(filepath)) break;
        auto msg = Deserialiser<HumanStateMessage>(filepath).make();
        if (msg.timestamp() > latest_timestamp) {
            human_messages.push_back(msg);
            latest_timestamp = msg.timestamp();
        }
    }

    Thread human_production([&]{
        CONCLOG_SCOPE_CREATE
        auto initial_timestamp = static_cast<unsigned long long>(duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        auto* publisher = hs_access.first.make_human_state_publisher(hs_access.second);
        while (not human_messages.empty()) {
            auto& p = human_messages.front();
            auto current_timestamp = static_cast<unsigned long long>(duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            if (p.timestamp()-sync_timestamp <= (current_timestamp-initial_timestamp)*speedup) {
                publisher->put(p);
                human_messages.pop_front();
                CONCLOG_PRINTLN("Sending human message with timestamp " << p.timestamp() << " at " << current_timestamp)
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CONCLOG_PRINTLN("Human messages completed")
        delete publisher;
    },"hu_p");

    Thread robot_production([&]{
        CONCLOG_SCOPE_CREATE
        auto initial_timestamp = static_cast<unsigned long long>(duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        auto* publisher = rs_access.first.make_robot_state_publisher(rs_access.second);
        while (not robot_messages.empty()) {
            auto& p = robot_messages.front();
            auto current_timestamp = static_cast<unsigned long long>(duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            if (p.timestamp()-sync_timestamp <= (current_timestamp-initial_timestamp)*speedup) {
                publisher->put(p);
                robot_messages.pop_front();
                CONCLOG_PRINTLN("Sending robot message with timestamp " << p.timestamp() << " at " << current_timestamp)
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CONCLOG_PRINTLN("Robot messages completed")
        delete publisher;
    },"rb_p");

    while(not human_messages.empty() or not robot_messages.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    CONCLOG_PRINTLN("Analysis completed: processed " << runtime.__num_processed() << " jobs, completed " << runtime.__num_completed() << " look-aheads (of which " <<
                      runtime.__num_collisions() << " were potential collisions).")

    delete cn_subscriber;

    for (SizeType i=0; i<collisions.size(); ++i)
        Serialiser<CollisionNotificationMessage>(collisions.at(i)).to_file("collisions/" + scenario_t + "/" + scenario_k + "/" + to_string(i) + ".json");

    CONCLOG_PRINTLN("Saved all collisions to JSON files in collisions/" + scenario_t + "/" + scenario_k + "/")
}

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    Logger::instance().configuration().set_thread_name_printing_policy(ThreadNamePrintingPolicy::BEFORE);
    String const scenario_t = "dynamic";
    String const scenario_k = "bad1";
    SizeType const speedup = 1;
    SizeType const concurrency = 16;
    BrokerAccess memory_access = MemoryBrokerAccess();
    BrokerAccess mqtt_access = MqttBrokerAccess(Environment::get("MQTT_BROKER_URI"), atoi(Environment::get("MQTT_BROKER_PORT")));
    BrokerAccess kafka_access = KafkaBrokerAccessBuilder(Environment::get("KAFKA_BROKER_URI"))
            .set_sasl_mechanism(Environment::get("KAFKA_SASL_MECHANISM"))
            .set_security_protocol(Environment::get("KAFKA_SECURITY_PROTOCOL"))
            .set_sasl_username(Environment::get("KAFKA_USERNAME"))
            .set_sasl_password(Environment::get("KAFKA_PASSWORD"))
            .build();
    //LookAheadJobFactory job_factory = ReuseLookAheadJobFactory(AddWhenDifferentMinimumDistanceBarrierSequenceUpdatePolicy(),ReuseEquivalence::STRONG);
    LookAheadJobFactory job_factory = DiscardLookAheadJobFactory();
    process({memory_access,BodyPresentationTopic::DEFAULT},
            {kafka_access,std::string(Environment::get("KAFKA_TOPIC_PREFIX"))+"opera_human_state"},
            {memory_access,RobotStateTopic::DEFAULT},
            {memory_access,{"opera_data_collision_prediction"}},
            scenario_t,scenario_k,speedup,concurrency,job_factory);
}
