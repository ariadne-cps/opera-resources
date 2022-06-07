/***************************************************************************
 *            scenario_input_check.cpp
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
#include "mqtt.hpp"
#include "memory.hpp"
#include "barrier.hpp"
#include "conclog/include/logging.hpp"
#include "runtime.hpp"
#include "command_line_interface.hpp"

using namespace Opera;
using namespace ConcLog;

void acquire_human_scenario_samples(String const& scenario_t, String const& scenario_k) {
    CONCLOG_SCOPE_CREATE
    BodyPresentationMessage p0 = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/human/presentation.json")).make();
    CONCLOG_PRINTLN_VAR(p0.segment_pairs().size())
    CONCLOG_PRINTLN_VAR(p0.thicknesses().size())
    Human human(p0.id(),p0.segment_pairs(),p0.thicknesses());
    OPERA_ASSERT_EQUAL(human.num_points(),18)

    SizeType file = 0;
    List<BodyStateMessage> human_messages;
    while (true) {
        CONCLOG_PRINTLN_VAR(file)
        auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/" + std::to_string(file++) + ".json");
        if (not exists(filepath)) break;
        human_messages.push_back(Deserialiser<BodyStateMessage>(filepath).make());
    }

    List<HumanStateInstance> instances;
    for (auto pkt: human_messages) {
        instances.emplace_back(human, pkt.points(), pkt.timestamp());
    }
}

void acquire_robot_scenario_samples(String const& scenario_t, String const& scenario_k) {
    CONCLOG_SCOPE_CREATE
    BodyPresentationMessage p0 = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/robot/presentation.json")).make();
    Robot robot(p0.id(),p0.message_frequency(),p0.point_ids(),p0.thicknesses());
    OPERA_ASSERT_EQUAL(robot.num_points(),9)

    SizeType file = 0;
    RobotStateHistory history(robot);
    TimestampType current_timestamp = 0;
    while (true) {
        CONCLOG_PRINTLN_VAR(file)
        auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(file++)+".json");
        if (not exists(filepath)) break;
        auto pkt = Deserialiser<BodyStateMessage>(filepath).make();
        OPERA_ASSERT(pkt.timestamp() > current_timestamp)
        current_timestamp = pkt.timestamp();
        history.acquire(pkt.mode(),pkt.points(),pkt.timestamp());
    }
}

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    String const scenario_t = "static";
    String const scenario_k = "long_r";
    CONCLOG_PRINTLN("Acquiring human scenario samples")
    acquire_human_scenario_samples(scenario_t,scenario_k);
    CONCLOG_PRINTLN("Acquiring robot scenario samples")
    acquire_robot_scenario_samples(scenario_t,scenario_k);
}
