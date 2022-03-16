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
        BodyPresentationMessage hp = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/human/presentation.json")).make();

        auto bp_publisher = access.make_body_presentation_publisher();
        bp_publisher->put(rp);
        bp_publisher->put(hp);
        std::this_thread::sleep_for(std::chrono::milliseconds (10));
        delete bp_publisher;
    }

    List<BodyStateMessage> load_state_messages() {
        List<BodyStateMessage> result;

        List<BodyStateMessage> robot_messages;
        SizeType robot_idx = 0;
        while (true) {
            auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(robot_idx++)+".json");
            if (not exists(filepath)) break;
            robot_messages.push_back(Deserialiser<BodyStateMessage>(filepath).make());
        }

        List<BodyStateMessage> human_messages;
        SizeType human_idx = 0;
        while (true) {
            auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/"+std::to_string(human_idx++)+".json");
            if (not exists(filepath)) break;
            human_messages.push_back(Deserialiser<BodyStateMessage>(filepath).make());
        }

        auto human_it = human_messages.cbegin();
        auto robot_it = robot_messages.cbegin();
        while (human_it != human_messages.cend() or robot_it != robot_messages.cend()) {
            if (human_it == human_messages.cend()) { result.push_back(*robot_it); ++robot_it; }
            else if (robot_it == robot_messages.cend()) { result.push_back(*human_it); ++human_it; }
            else if (human_it->timestamp() > robot_it->timestamp()) { result.push_back(*robot_it); ++robot_it; }
            else { result.push_back(*human_it); ++human_it; }
        }

        return result;
    }

    void compared_processing(Runtime const& discard_runtime, Runtime const& reuse_runtime, List<BodyStateMessage> const& state_messages, List<BodyStateMessage>::const_iterator msg_it, PublisherInterface<BodyStateMessage>* const& bs_publisher) {
        CONCLOG_SCOPE_CREATE
        SizeType num_state_messages_sent = discard_runtime.__num_state_messages_received();
        SizeType num_remaining_messages = state_messages.size()-num_state_messages_sent;

        ProgressIndicator indicator(static_cast<FloatType>(num_remaining_messages));

        FloatType i=0;
        while (msg_it != state_messages.cend()) {
            indicator.update_current(i);
            CONCLOG_PRINTLN_VAR_AT(1,i)
            ++num_state_messages_sent;
            bs_publisher->put(*msg_it);
            while(not(discard_runtime.__num_state_messages_received() == num_state_messages_sent) or not(reuse_runtime.__num_state_messages_received() == num_state_messages_sent)) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }

            while (not discard_runtime.__all_done() or discard_runtime.num_sleeping_jobs() == 0 or not reuse_runtime.__all_done() or reuse_runtime.num_sleeping_jobs() == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }

            OPERA_ASSERT(discard_runtime.num_sleeping_jobs() > 0)
            OPERA_ASSERT(reuse_runtime.num_sleeping_jobs() > 0)
            OPERA_ASSERT_EQUAL(discard_runtime.__num_collisions(),reuse_runtime.__num_collisions())
            ++msg_it;
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

        OPERA_ASSERT_EQUAL(discard_runtime.num_pending_human_robot_pairs(),0)
        OPERA_ASSERT_EQUAL(reuse_runtime.num_pending_human_robot_pairs(),0)

        present_bodies(access);

        OPERA_ASSERT_EQUAL(discard_runtime.num_pending_human_robot_pairs(),1)
        OPERA_ASSERT_EQUAL(reuse_runtime.num_pending_human_robot_pairs(),1)

        OPERA_ASSERT_EQUAL(discard_runtime.num_segment_pairs(),reuse_runtime.num_segment_pairs())

        SizeType num_segment_pairs = discard_runtime.num_segment_pairs();

        CONCLOG_PRINTLN_VAR_AT(1,num_segment_pairs)

        CONCLOG_PRINTLN("Body state messages loading in temporal order")

        auto state_messages = load_state_messages();

        CONCLOG_PRINTLN("Body state messages publishing until working jobs can be created")

        auto bs_publisher = access.make_body_state_publisher();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        SizeType num_state_messages_received = 0;

        auto msg_it = state_messages.cbegin();

        while (discard_runtime.num_pending_human_robot_pairs() > 0 or reuse_runtime.num_pending_human_robot_pairs() > 0) {
            bs_publisher->put(*msg_it);
            ++msg_it;
            while(discard_runtime.__num_state_messages_received() < num_state_messages_received or reuse_runtime.__num_state_messages_received() < num_state_messages_received) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            num_state_messages_received++;

            if (msg_it == state_messages.cend()) {
                OPERA_ERROR("Got to the end of the messages without resolving the pending human robot pairs")
            }
        }

        while(discard_runtime.num_sleeping_jobs() < num_segment_pairs or reuse_runtime.num_sleeping_jobs() < num_segment_pairs) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }

        OPERA_ASSERT_EQUAL(discard_runtime.__num_state_messages_received(),reuse_runtime.__num_state_messages_received())
        CONCLOG_PRINTLN_VAR(num_state_messages_received)

        OPERA_ASSERT_EQUAL(discard_runtime.__num_processed(),reuse_runtime.__num_processed())
        SizeType num_processed = discard_runtime.__num_processed();
        CONCLOG_PRINTLN_VAR(num_processed)
        
        CONCLOG_PRINTLN("Body state messages publishing until completion")

        compared_processing(discard_runtime,reuse_runtime,state_messages,msg_it,bs_publisher);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        delete bs_publisher;
    }
};

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    Logger::instance().configuration().set_thread_name_printing_policy(ThreadNamePrintingPolicy::BEFORE);
    Logger::instance().use_blocking_scheduler();
    String const scenario_t = "static";
    String const scenario_k = "long_l";
    ScenarioCheck(scenario_t,scenario_k).check_compared_processing();
}
