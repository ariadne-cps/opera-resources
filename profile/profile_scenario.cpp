/***************************************************************************
 *            profile_scenario.cpp
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
#include "profile.hpp"
#include "runtime.hpp"
#include "scenario_utility.hpp"
#include "command_line_interface.hpp"
#include "conclog/include/logging.hpp"
#include "conclog/include/progress_indicator.hpp"

using namespace Opera;
using namespace ConcLog;

class ProfileScenario : public Profiler {
    String const scenario_t;
    String const scenario_k;
  public:

    ProfileScenario(String const& t, String const& k) : Profiler(1), scenario_t(t), scenario_k(k) { }

    void run() {
        profile_sequential();
    }

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
            if (human_idx % 3 == 0) {
                auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/"+std::to_string(human_idx)+".json");
                if (not exists(filepath)) break;
                human_messages.push_back(Deserialiser<BodyStateMessage>(filepath).make());
            }
            ++human_idx;
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

    void process_sequential(Runtime const& runtime, PublisherInterface<BodyStateMessage>* const& bs_publisher, List<BodyStateMessage>::const_iterator msg_it, List<BodyStateMessage>::const_iterator const& msg_it_end) {
        SizeType num_state_messages_sent = runtime.__num_state_messages_received();
        auto num_messages = static_cast<FloatType>(msg_it_end - msg_it);
        CONCLOG_SCOPE_CREATE
        ProgressIndicator indicator(num_messages);
        FloatType i = 0;
        while (msg_it != msg_it_end) {
            indicator.update_current(i);
            ++num_state_messages_sent;
            bs_publisher->put(*msg_it);
            while(runtime.__num_state_messages_received() != num_state_messages_sent) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            while (not runtime.__all_done() or runtime.num_sleeping_jobs() == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            ++msg_it;
            ++i;
            CONCLOG_SCOPE_PRINTHOLD("[" << indicator.symbol() << "] " << indicator.percentage() << "%");
        }
    }

    NsCount profile_sequential(String const& text, LookAheadJobFactory const& factory, List<BodyStateMessage> const& state_messages) {

        BrokerAccess access = MemoryBrokerAccess();
        CONCLOG_RUN_AT(1,Runtime runtime(access,factory));

        std::this_thread::sleep_for(std::chrono::milliseconds (10));

        present_bodies(access);

        auto bs_publisher = access.make_body_state_publisher();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        SizeType num_state_messages_received = 0;

        auto msg_it = state_messages.cbegin();

        while (runtime.num_pending_human_robot_pairs() > 0) {
            bs_publisher->put(*msg_it);
            ++msg_it;
            while(runtime.__num_state_messages_received() < num_state_messages_received) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            num_state_messages_received++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto result = profile("Sequential execution " + text,[&](SizeType){ process_sequential(runtime,bs_publisher,msg_it,state_messages.cend()); });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        delete bs_publisher;
        return result;
    }

    void profile_sequential() {

        auto state_messages = load_state_messages();
        TimestampType start_time = 0, end_time = 0;
        for (auto const& msg : state_messages) {
            if (msg.mode().is_empty()) {
                start_time = msg.timestamp();
                break;
            }
        }
        for (auto it = state_messages.crbegin(); it != state_messages.crend(); ++it) {
            if (it->mode().is_empty()) {
                end_time = it->timestamp();
                break;
            }
        }
        TimestampType duration = end_time - start_time;

        std::cout << "<" << scenario_t << "/" << scenario_k << ">" << std::endl;

        ReuseLookAheadJobFactory reuse_factory(AddWhenDifferentMinimumDistanceBarrierSequenceUpdatePolicy(),ReuseEquivalence::STRONG);
        auto discard_cnt = profile_sequential("not using reuse", DiscardLookAheadJobFactory(), state_messages);
        auto reuse_cnt = profile_sequential("using reuse", reuse_factory, state_messages);

        std::cout << "Duration: " << static_cast<FloatType>(duration)/60000000000 << " min" << std::endl;
        std::cout << "Speedup: " << static_cast<FloatType>(discard_cnt)/reuse_cnt << std::endl;
        std::cout << "Resource occupation when discarding: " << static_cast<FloatType>(discard_cnt)/duration << std::endl;
        std::cout << "Resource occupation when reusing: " << static_cast<FloatType>(reuse_cnt)/duration << std::endl;
    }
};

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    Logger::instance().configuration().set_thread_name_printing_policy(ThreadNamePrintingPolicy::BEFORE);
    Logger::instance().use_blocking_scheduler();
    String const scenario_t = "dynamic";
    String const scenario_k = "quadrants";
    ProfileScenario(scenario_t,scenario_k).run();

}
