/***************************************************************************
 *            scenario_output_plot.cpp
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

void aggregate_collision_packets(String const& scenario_t, String const& scenario_k, SizeType const human_segment_to_focus, SizeType const robot_segment_to_focus) {

    SizeType num_samples = 0;
    TimestampType initial_time = Deserialiser<BodyStateMessage>(ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/0.json")).make().timestamp();
    while (true) {
        auto filepath = ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/" + std::to_string(++num_samples) + ".json");
        if (not exists(filepath)) break;
    }
    TimestampType final_time = Deserialiser<BodyStateMessage>(ScenarioResources::path(scenario_t+"/human/"+scenario_k+"/"+std::to_string(num_samples-1)+".json")).make().timestamp();

    List<CollisionNotificationMessage> collisions;
    SizeType file = 0;
    while (true) {
        auto filepath = FilePath("collisions/"+scenario_t+"/"+scenario_k+"/"+std::to_string(file++)+".json");
        if (not exists(filepath)) break;
        collisions.push_back(Deserialiser<CollisionNotificationMessage>(filepath).make());
    }

    CONCLOG_PRINTLN_VAR_AT(1,initial_time)
    CONCLOG_PRINTLN_VAR_AT(1,final_time)

    CONCLOG_PRINTLN("Collision files acquired")

    TimestampType time_interval = (final_time-initial_time)/num_samples;

    List<SizeType> time_points;
    List<Interval<FloatType>> collision_distance_bounds;
    FloatType max_upper_bound = 0;
    List<SizeType> num_different_segments;
    SizeType idx = 0;
    for (SizeType i=0; i < num_samples; ++i) {
        FloatType lower_bound = std::numeric_limits<FloatType>::max();
        FloatType upper_bound = 0;
        Set<SizeType> human_segments;
        TimestampType interval_time_bound = initial_time + time_interval*(i+1);
        while (idx < collisions.size() and collisions.at(idx).current_time() < interval_time_bound) {
            if (collisions.at(idx).human_segment_id() == human_segment_to_focus and collisions.at(idx).robot_segment_id() == robot_segment_to_focus) {
                CONCLOG_PRINTLN("segment processed with segment_distance " << collisions.at(idx).collision_distance())
                auto const& c = collisions.at(idx);
                lower_bound = std::min(lower_bound,static_cast<FloatType>(c.collision_distance().lower())/1e9);
                upper_bound = std::max(upper_bound,static_cast<FloatType>(c.collision_distance().upper())/1e9);
                human_segments.insert(c.human_segment_id());
            }
            ++idx;
        }
        max_upper_bound = std::max(max_upper_bound,upper_bound);
        if (lower_bound != std::numeric_limits<FloatType>::max()) {
            collision_distance_bounds.push_back({lower_bound, upper_bound});
            time_points.push_back(interval_time_bound);
            num_different_segments.push_back(human_segments.size());
        }
    }

    CONCLOG_PRINTLN("Acquired all collision bounds and number of different segments for each interval")

    std::ofstream output;
    output.open("scenario_" + scenario_t + "_" + scenario_k + "_collisions_" + to_string(human_segment_to_focus) + "_" + to_string(robot_segment_to_focus) + ".m");
    output << "figure(1);\n";

    output << "x = [";
    for (auto const& tp : time_points) output << tp << " ";
    output << "];\n";
    output << "x = (x - x(1))/1e9/60;\n";
    output << "xlabel(\"T_s(min)\");\n";
    output << "ylabel(\"T_{pr}(sec)\");\n";
    output << "hold on;\n";

    output << "yu = [";
    for (auto const& cdb : collision_distance_bounds) output << cdb.upper() << " ";
    output << "];\n";
    output << "plot(x,yu,'k.');\n";

    output << "yl = [";
    for (auto const& cdb : collision_distance_bounds) output << cdb.lower() << " ";
    output << "];\n";
    output << "plot(x,yl,'r.');\n";

    output << "hold off;";
    output.close();
}

int main(int argc, const char* argv[])
{
    if (not CommandLineInterface::instance().acquire(argc,argv)) return -1;
    String const scenario_t = "dynamic";
    String const scenario_k = "quadrants";
    SizeType const human_segment = 2;
    SizeType const robot_segment = 7;
    aggregate_collision_packets(scenario_t,scenario_k,human_segment,robot_segment);
}
