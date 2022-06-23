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
    Human human(p0.id(),p0.segment_pairs(),p0.thicknesses());
    OPERA_ASSERT_EQUAL(human.num_points(),16)
    SizeType file = 0;
    List<HumanStateMessage> human_messages;
    CONCLOG_PRINTLN("Acquiring files")
    while (true) {
        CONCLOG_PRINTLN_AT(1,"File " << file)
        auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/" + std::to_string(file++) + ".json");
        if (not exists(filepath)) break;
        auto deserialiser = Deserialiser<HumanStateMessage>(filepath);
        human_messages.push_back(deserialiser.make());
    }

    List<HumanStateInstance> instances;
    CONCLOG_PRINTLN("Creating instances")
    for (auto const& pkt: human_messages) {
        CONCLOG_PRINTLN_AT(1,"Instance " << instances.size() << " with " << pkt.bodies().size() << " bodies")
        for (auto const& bd : pkt.bodies())
            instances.emplace_back(human, bd.second, pkt.timestamp());
    }
}

void acquire_robot_scenario_samples(String const& scenario_t, String const& scenario_k) {
    CONCLOG_SCOPE_CREATE
    BodyPresentationMessage p0 = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/robot/presentation.json")).make();
    Robot robot(p0.id(),p0.message_frequency(),p0.segment_pairs(),p0.thicknesses());
    OPERA_ASSERT_EQUAL(robot.num_points(),9)

    SizeType file = 0;
    RobotStateHistory history(robot);
    TimestampType current_timestamp = 0;
    while (true) {
        CONCLOG_PRINTLN_AT(1, "File " << file)
        auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(file++)+".json");
        if (not exists(filepath)) break;
        auto pkt = Deserialiser<RobotStateMessage>(filepath).make();
        OPERA_ASSERT(pkt.timestamp() > current_timestamp)
        current_timestamp = pkt.timestamp();
        Map<KeypointIdType,List<Point>> points;
        for (SizeType i=0;i<robot.num_points();++i)
            points.insert(make_pair(to_string(i),pkt.points()[i]));
        history.acquire(pkt.mode(),points,pkt.timestamp());
    }
}

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    String const scenario_t = "dynamic";
    String const scenario_k = "bad1";
    CONCLOG_PRINTLN("Checking human scenario samples")
    acquire_human_scenario_samples(scenario_t,scenario_k);
    CONCLOG_PRINTLN("Checking robot scenario samples")
    acquire_robot_scenario_samples(scenario_t,scenario_k);
}
