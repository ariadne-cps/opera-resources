/***************************************************************************
 *            ice_demo.cpp
 *
 *  Copyright  2022  Luca Geretti
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

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    Logger::instance().configuration().set_thread_name_printing_policy(ThreadNamePrintingPolicy::BEFORE);

    BrokerAccess memory_access = MemoryBrokerAccess();
    BrokerAccess mqtt_access = MqttBrokerAccess(Environment::get("MQTT_BROKER_URI"), atoi(Environment::get("MQTT_BROKER_PORT")));
    BrokerAccess kafka_access = KafkaBrokerAccessBuilder(Environment::get("KAFKA_BROKER_URI"))
            .set_sasl_mechanism(Environment::get("KAFKA_SASL_MECHANISM"))
            .set_security_protocol(Environment::get("KAFKA_SECURITY_PROTOCOL"))
            .set_sasl_username(Environment::get("KAFKA_USERNAME"))
            .set_sasl_password(Environment::get("KAFKA_PASSWORD"))
            .build();
    LookAheadJobFactory job_factory = DiscardLookAheadJobFactory();
    //LookAheadJobFactory job_factory = ReuseLookAheadJobFactory(AddWhenDifferentMinimumDistanceBarrierSequenceUpdatePolicy(),ReuseEquivalence::STRONG);
    SizeType concurrency = std::thread::hardware_concurrency();
    Runtime runtime({memory_access,BodyPresentationTopic::DEFAULT},
                    {kafka_access,{"opera_data_human_pose_aggregator"}},
                    {mqtt_access, {"ice_cell4_lbr_iiwa_arm"}},
                    {kafka_access,{"opera_data_collision_prediction"}},job_factory,concurrency);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    BodyPresentationMessage rp = Deserialiser<BodyPresentationMessage>(ScenarioResources::path("ice/robot.json")).make();
    auto* presentation_publisher = memory_access.make_body_presentation_publisher();
    presentation_publisher->put(rp);

    SizeType num_human_messages = 0;
    auto* human_subscriber = kafka_access.make_human_state_subscriber([&](auto const& p){
        Serialiser<HumanStateMessage>(p).to_file("input/human/" + to_string(num_human_messages) + ".json");
        ++num_human_messages;
    },{"opera_data_human_pose_aggregator"});

    SizeType num_robot_messages = 0;
    auto* robot_subscriber = mqtt_access.make_robot_state_subscriber([&](auto const& p){
        Serialiser<RobotStateMessage>(p).to_file("input/robot/" + to_string(num_robot_messages) + ".json");
        ++num_robot_messages;
    },{"ice_cell4_lbr_iiwa_arm"});

    std::this_thread::sleep_until(std::chrono::system_clock::now() + std::chrono::hours(std::numeric_limits<int>::max()));
    delete human_subscriber;
    delete robot_subscriber;
}
