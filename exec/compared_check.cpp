/***************************************************************************
 *            test_scenario.cpp
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

#include "memory.hpp"
#include "message.hpp"
#include "deserialisation.hpp"
#include "serialisation.hpp"
#include "runtime.hpp"
#include "conclog/include/logging.hpp"
#include "conclog/include/progress_indicator.hpp"
#include "command_line_interface.hpp"
#include "scenario_utility.hpp"

using namespace Opera;
using namespace ConcLog;

class ScenarioCheck {
    String const scenario_t;
    String const scenario_k;
  public:

    ScenarioCheck(String const& t, String const& k) : scenario_t(t), scenario_k(k) { }

    void present_bodies(BrokerAccess const& access) {
        BodyPresentationMessage rp = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/robot/presentation.json")).make();
        auto bp_publisher = access.make_body_presentation_publisher();
        bp_publisher->put(rp);
        std::this_thread::sleep_for(std::chrono::milliseconds (10));
        delete bp_publisher;
    }

    void load_state_messages(List<HumanStateMessage>& human_messages, List<RobotStateMessage>& robot_messages) {
        SizeType robot_idx = 0;
        while (true) {
            auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(robot_idx++)+".json");
            if (not exists(filepath)) break;
            robot_messages.push_back(Deserialiser<RobotStateMessage>(filepath).make());
        }

        SizeType human_idx = 0;
        while (true) {
            auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/"+std::to_string(human_idx++)+".json");
            if (not exists(filepath)) break;
            human_messages.push_back(Deserialiser<HumanStateMessage>(filepath).make());
        }
    }

    void compared_processing(Runtime const& discard_runtime, Runtime const& reuse_runtime, List<HumanStateMessage> const& human_messages, List<RobotStateMessage> const& robot_messages, PublisherInterface<HumanStateMessage>* const& hs_publisher, PublisherInterface<RobotStateMessage>* const& rs_publisher) {
        CONCLOG_SCOPE_CREATE
        SizeType num_state_messages_sent = 0;
        SizeType num_remaining_messages = human_messages.size()+robot_messages.size();
        CONCLOG_PRINTLN_VAR_AT(1,num_remaining_messages)
        ProgressIndicator indicator(static_cast<FloatType>(num_remaining_messages));

        auto human_msg_it = human_messages.cbegin();
        auto robot_msg_it = robot_messages.cbegin();

        FloatType i=0;
        while (human_msg_it != human_messages.cend() or robot_msg_it != robot_messages.cend() ) {
            indicator.update_current(i);
            CONCLOG_PRINTLN_VAR_AT(1,i)
            ++num_state_messages_sent;
            if (human_msg_it == human_messages.cend()) {
                rs_publisher->put(*robot_msg_it);
                ++robot_msg_it;
            } else if (robot_msg_it == robot_messages.cend()) {
                hs_publisher->put(*human_msg_it);
                ++human_msg_it;
            } else if (robot_msg_it->timestamp() < human_msg_it->timestamp()) {
                rs_publisher->put(*robot_msg_it);
                ++robot_msg_it;
            } else {
                hs_publisher->put(*human_msg_it);
                ++human_msg_it;
            }
            while(discard_runtime.__num_state_messages_received() != num_state_messages_sent or reuse_runtime.__num_state_messages_received() != num_state_messages_sent) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            OPERA_ASSERT_EQUAL(discard_runtime.__num_collisions(),reuse_runtime.__num_collisions())
            ++i;
            CONCLOG_SCOPE_PRINTHOLD("[" << indicator.symbol() << "] " << indicator.percentage() << "%");
        }
    }

    void check_compared_processing() {

        BrokerAccess access = MemoryBrokerAccess();

        LookAheadJobFactory discard_factory = DiscardLookAheadJobFactory();
        LookAheadJobFactory reuse_factory = ReuseLookAheadJobFactory(KeepOneMinimumDistanceBarrierSequenceUpdatePolicy(),ReuseEquivalence::STRONG);

        CONCLOG_RUN_AT(2,Runtime discard_runtime(access,discard_factory))
        CONCLOG_RUN_AT(2,Runtime reuse_runtime(access,reuse_factory))

        std::this_thread::sleep_for(std::chrono::milliseconds (10));

        CONCLOG_PRINTLN("Bodies presentations loading and publishing")

        present_bodies(access);

        List<HumanStateMessage> human_messages;
        List<RobotStateMessage> robot_messages;
        load_state_messages(human_messages,robot_messages);

        CONCLOG_PRINTLN("Body state messages publishing until working jobs can be created")

        auto hs_publisher = access.make_human_state_publisher();
        auto rs_publisher = access.make_robot_state_publisher();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        compared_processing(discard_runtime,reuse_runtime,human_messages,robot_messages,hs_publisher,rs_publisher);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        delete hs_publisher;
        delete rs_publisher;
    }
};

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    Logger::instance().configuration().set_thread_name_printing_policy(ThreadNamePrintingPolicy::BEFORE);
    Logger::instance().use_blocking_scheduler();
    String const scenario_t = "dynamic";
    String const scenario_k = "bad1";
    ScenarioCheck(scenario_t,scenario_k).check_compared_processing();
}
