/***************************************************************************
 *            scenario_input_plot.cpp
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

void plot_samples(String const& scenario_t, String const& scenario_k, SizeType const& human_keypoint, SizeType const& robot_keypoint) {
    BodyPresentationMessage p0 = Deserialiser<BodyPresentationMessage>(ScenarioResources::path(scenario_t+"/human/presentation.json")).make();
    Human human(p0.id(),p0.point_ids(),p0.thicknesses());
    OPERA_ASSERT_EQUAL(human.num_points(),18)

    CONCLOG_PRINTLN("Getting samples")

    SizeType file = 0;
    List<BodyStateMessage> human_messages;
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/" + std::to_string(file++) + ".json");
        if (not exists(filepath)) break;
        auto msg = Deserialiser<BodyStateMessage>(filepath).make();
        human_messages.push_back(msg);
    }

    file = 0;
    List<BodyStateMessage> robot_messages;
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/robot/"+scenario_k+"/"+std::to_string(file++)+".json");
        if (not exists(filepath)) break;
        robot_messages.push_back(Deserialiser<BodyStateMessage>(filepath).make());
    }

    CONCLOG_PRINTLN("Ordering samples")

    TimestampType initial_time = std::max(human_messages.at(0).timestamp(),robot_messages.at(0).timestamp());
    TimestampType final_time = std::min(human_messages.at(human_messages.size()-1).timestamp(),robot_messages.at(robot_messages.size()-1).timestamp());

    CONCLOG_PRINTLN_VAR(initial_time)
    CONCLOG_PRINTLN_VAR(final_time)

    SizeType human_idx = 0, robot_idx = 0;
    bool human_at_initial = false, robot_at_initial = false;
    while (not human_at_initial or not robot_at_initial) {
        CONCLOG_PRINTLN_AT(1,"human_idx = " << human_idx << ", robot_idx = " << robot_idx)
        if (human_messages.at(human_idx).timestamp() < initial_time) human_idx++;
        else human_at_initial = true;
        if (robot_messages.at(robot_idx).timestamp() < initial_time) robot_idx++;
        else robot_at_initial = true;
    }

    List<List<Point>> human_points;
    List<Point> robot_points;
    human_points.push_back(human_messages.at(human_idx++).points().at(human_keypoint));
    robot_points.push_back(robot_messages.at(robot_idx++).points().at(robot_keypoint).at(0));
    List<TimestampType> times;
    times.push_back(0);
    List<SizeType> num_camera_samples;
    num_camera_samples.push_back(human_points.at(0).size());

    TimestampType current_time = initial_time;
    while (current_time < final_time) {
        auto const human_timestamp = human_messages.at(human_idx).timestamp();
        auto const robot_timestamp = robot_messages.at(robot_idx).timestamp();
        if (human_timestamp > robot_timestamp) {
            human_points.push_back(human_points.at(human_points.size()-1));
            robot_points.push_back(robot_messages.at(robot_idx++).points().at(robot_keypoint).at(0));
            current_time = robot_timestamp;
        } else if (human_timestamp < robot_timestamp) {
            human_points.push_back(human_messages.at(human_idx++).points().at(human_keypoint));
            robot_points.push_back(robot_points.at(robot_points.size()-1));
            current_time = human_timestamp;
        } else {
            human_points.push_back(human_messages.at(human_idx++).points().at(human_keypoint));
            robot_points.push_back(robot_messages.at(robot_idx++).points().at(robot_keypoint).at(0));
            current_time = human_timestamp;
        }
        num_camera_samples.push_back(human_points.at(human_points.size()-1).size());
        times.push_back(current_time-initial_time);
    }

    CONCLOG_PRINTLN("Writing superimposed points MATLAB file")

    {
        std::ofstream output;
        output.open("scenario_" + scenario_t + "_" + scenario_k + "_superimposed_points_" + to_string(human_keypoint) + "_" + to_string(robot_keypoint) + ".m");
        output << "figure(1);\n";

        auto num_instants = times.size();

        output << "human_samples = zeros(" << num_instants << ",3,3);\n";
        output << "robot_samples = zeros(" << num_instants << ",3);\n";

        for (SizeType i=0; i<num_instants; ++i) {
            for (SizeType j=0; j<num_camera_samples.at(i); ++j) {
                auto const& pt = human_points.at(i).at(j);
                output << "human_samples(" << i+1 << "," << j+1 << ",:) = [" << pt.x << " " << pt.y << " " << pt.z << "];\n";
            }
            auto const& pt = robot_points.at(i);
            output << "robot_samples(" << i+1 << ",:) = [" << pt.x << " " << pt.y << " " << pt.z << "];\n";
        }

        output << "hold on;\n";
        output << "xlabel('X'); ylabel('Y'); zlabel('Z')\n";
        output << "plot3(robot_samples(:,1),robot_samples(:,2),robot_samples(:,3),'b.');\n";
        output << "plot3(human_samples(:,1,1),human_samples(:,1,2),human_samples(:,1,3),'r.');\n";
        output << "plot3(human_samples(:,2,1),human_samples(:,2,2),human_samples(:,2,3),'r.');\n";
        output << "plot3(human_samples(:,3,1),human_samples(:,3,2),human_samples(:,3,3),'r.');\n";
        output << "hold off;\n";
        output.close();
    }

    CONCLOG_PRINTLN("Writing animated points MATLAB file")

    {
        double pause_time = 0.001;
        std::ofstream output;
        output.open("scenario_" + scenario_t + "_" + scenario_k + "_animated_points_" + to_string(human_keypoint) + "_" + to_string(robot_keypoint) + ".m");
        output << "figure(2);\n";
        output << "xlabel('X'); ylabel('Y'); zlabel('Z')\n";

        auto num_instants = times.size();
        output << "hold on;\n";
        for (SizeType i=0; i<num_instants; ++i) {
            output << "hdl = text(0,0,0,'t=" << static_cast<float>(times.at(i))/1000000000 << "');\n";

            for (SizeType j=0; j<num_camera_samples.at(i); ++j) {
                auto const& pt = human_points.at(i).at(j);
                output << "plot3([" << pt.x << "],[" << pt.y << "],[" << pt.z << "],'r.');\n";
            }
            auto const& pt = robot_points.at(i);
            output << "plot3([" << pt.x << "],[" << pt.y << "],[" << pt.z << "],'b.');\n";
            output << "pause(" << pause_time << ");\n";
            output << "delete(hdl);\n";
        }
        output << "hold off;\n";
        output.close();

    }

    CONCLOG_PRINTLN("MATLAB file closed.")
}

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    String const scenario_t = "static";
    String const scenario_k = "short_dx";
    SizeType const human_keypoint = 8;
    SizeType const robot_keypoint = 7;
    plot_samples(scenario_t,scenario_k,human_keypoint,robot_keypoint);
}
