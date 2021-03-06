/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Robert Bosch LLC.
 *  Copyright (c) 2015-2016, Jiri Horner.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Jiri Horner nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************/

#include <explore/explore.h>
#include <explore/frontier_search.h>
#include <explore/utils.h>

#include <thread>

inline static bool operator==(const geometry_msgs::Point& one,
                              const geometry_msgs::Point& two)
{
  double dx = one.x - two.x;
  double dy = one.y - two.y;
  double dist = sqrt(dx * dx + dy * dy);
  return dist < 0.01;
}

namespace explore
{
Explore::Explore()
  : private_nh_("~")
  , tf_listener_(ros::Duration(10.0))
  , costmap_client_(private_nh_, relative_nh_, &tf_listener_)
  , prev_distance_(0)
  , last_markers_count_(0)
{
  double timeout;
  double min_frontier_size;
  private_nh_.param("planner_frequency", planner_frequency_, 1.0);
  private_nh_.param("progress_timeout", timeout, 30.0);
  progress_timeout_ = ros::Duration(timeout);
  private_nh_.param("visualize", visualize_, false);
  private_nh_.param("potential_scale", potential_scale_, 1e-3);
  private_nh_.param("orientation_scale", orientation_scale_, 0.0);
  private_nh_.param("gain_scale", gain_scale_, 1.0);
  private_nh_.param("min_frontier_size", min_frontier_size, 0.5);
  private_nh_.param("robot_namespaces", robot_namespaces_,
                    std::vector<std::string>());

  // load the points that define the exploration boundary
  std::vector<float> p1, p2, p3, p4;
  relative_nh_.param("exploration_boundary/p1", p1, std::vector<float>());
  relative_nh_.param("exploration_boundary/p2", p2, std::vector<float>());
  relative_nh_.param("exploration_boundary/p3", p3, std::vector<float>());
  relative_nh_.param("exploration_boundary/p4", p4, std::vector<float>());
  boundary_points_.push_back(p1);
  boundary_points_.push_back(p2);
  boundary_points_.push_back(p3);
  boundary_points_.push_back(p4);
  exploration_bbox_ = explore::pointsToBBox(boundary_points_);

  for (const auto& ns : robot_namespaces_) {
    auto action_name = std::string("/") + ns + std::string("/move_base");
    move_base_clients_.push_back(
        std::unique_ptr<
            actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>>(
            new actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>(
                action_name)));

    if (visualize_) {
      marker_array_publishers_.push_back(
          private_nh_.advertise<visualization_msgs::MarkerArray>(
              ns + std::string("/frontiers"), 10));
      exploration_boundary_publisher_ =
          private_nh_.advertise<visualization_msgs::Marker>(
              ns + std::string("/exploration_boundary"), 10, true);
      visualizeBoundary();
    }
  }

  search_ = frontier_exploration::FrontierSearch(costmap_client_.getCostmap(),
                                                 potential_scale_, gain_scale_,
                                                 min_frontier_size);

  // ROS_INFO("Waiting to connect to move_base server");
  // for (auto& mbc : move_base_clients_) {
  //   mbc->waitForServer();
  // }
  ROS_INFO("Connected to move_base server");

  exploring_timer_ =
      relative_nh_.createTimer(ros::Duration(1. / planner_frequency_),
                               [this](const ros::TimerEvent&) { makePlan(); });
}

Explore::~Explore()
{
  stop();
}

void Explore::visualizeBoundary()
{
  std_msgs::ColorRGBA blue;
  blue.r = 0;
  blue.g = 0;
  blue.b = 1.0;
  blue.a = 1.0;

  visualization_msgs::Marker marker;
  marker.header.frame_id = costmap_client_.getGlobalFrameID();
  marker.header.stamp = ros::Time::now();
  marker.color.b = 1.0;
  marker.color.a = 255;
  marker.lifetime = ros::Duration(0);
  marker.frame_locked = true;
  marker.type = visualization_msgs::Marker::LINE_STRIP;
  marker.scale.x = 0.1;

  auto points = boundary_points_;
  points.push_back(points.front());
  for (const auto& p : points) {
    geometry_msgs::Point gp;
    gp.x = p[0];
    gp.y = p[1];
    gp.z = 0.1;
    marker.points.push_back(gp);
  }

  exploration_boundary_publisher_.publish(marker);
}

void Explore::visualizeFrontiers(
    const int& pub_index,
    const std::vector<frontier_exploration::Frontier>& frontiers,
    const bool& randomize_colors = true)
{
  std_msgs::ColorRGBA blue;
  blue.r = 0;
  blue.g = 0;
  blue.b = 1.0;
  blue.a = 1.0;
  std_msgs::ColorRGBA red;
  red.r = 1.0;
  red.g = 0;
  red.b = 0;
  red.a = 1.0;
  std_msgs::ColorRGBA green;
  green.r = 0;
  green.g = 1.0;
  green.b = 0;
  green.a = 1.0;

  ROS_DEBUG("visualising %lu frontiers", frontiers.size());
  visualization_msgs::MarkerArray markers_msg;
  std::vector<visualization_msgs::Marker>& markers = markers_msg.markers;
  visualization_msgs::Marker m;

  m.header.frame_id = costmap_client_.getGlobalFrameID();
  m.header.stamp = ros::Time::now();
  m.ns = "frontiers";
  m.scale.x = 1.0;
  m.scale.y = 1.0;
  m.scale.z = 1.0;
  if (randomize_colors) {
    m.color.r = rand() % 255;
    m.color.g = rand() % 255;
    m.color.b = rand() % 255;
  } else {
    m.color.r = 0;
    m.color.g = 0;
    m.color.b = 255;
  }
  m.color.a = 255;
  auto color = m.color;
  // lives forever
  m.lifetime = ros::Duration(0);
  m.frame_locked = true;

  // weighted frontiers are always sorted
  double min_cost = frontiers.empty() ? 0. : frontiers.front().cost;

  m.action = visualization_msgs::Marker::ADD;
  size_t id = 0;
  for (auto& frontier : frontiers) {
    m.type = visualization_msgs::Marker::POINTS;
    m.id = int(id);
    m.pose.position = {};
    m.scale.x = 0.1;
    m.scale.y = 0.1;
    m.scale.z = 0.1;
    m.points = frontier.points;
    if (goalOnBlacklist(frontier.centroid)) {
      m.color = red;
    }
    markers.push_back(m);
    ++id;
    m.type = visualization_msgs::Marker::SPHERE;
    m.id = int(id);
    m.pose.position = frontier.initial;
    // scale frontier according to its cost (costier frontiers will be smaller)
    double scale = std::min(std::abs(min_cost * 0.4 / frontier.cost), 0.5);
    m.scale.x = scale;
    m.scale.y = scale;
    m.scale.z = scale;
    m.color = color;
    m.points = {};
    markers.push_back(m);
    ++id;
  }
  size_t current_markers_count = markers.size();

  // delete previous markers, which are now unused
  m.action = visualization_msgs::Marker::DELETE;
  for (; id < last_markers_count_; ++id) {
    m.id = int(id);
    markers.push_back(m);
  }

  last_markers_count_ = current_markers_count;
  marker_array_publishers_[pub_index].publish(markers_msg);
}

void Explore::makePlan()
{
  // find frontiers
  std::vector<frontier_exploration::Frontier> other_robot_frontiers;
  for (int i = 0; i < robot_namespaces_.size(); ++i) {
    const auto& mbc = move_base_clients_[i];
    if (!mbc->isServerConnected()) {
      continue;
    }

    auto pose = costmap_client_.getRobotPose(robot_namespaces_[i]);

    // get frontiers sorted according to cost
    auto frontiers = search_.searchFrom(pose.position);
    ROS_DEBUG("found %lu frontiers", frontiers.size());
    for (size_t i = 0; i < frontiers.size(); ++i) {
      ROS_DEBUG("frontier %zd cost: %f", i, frontiers[i].cost);
    }
    frontier_exploration::Frontier output;
    output.centroid.x = -4;
    output.centroid.y = 0;
    frontiers.push_back(output);
    // remove frontiers that lie outside the boundary
    frontiers.erase(
        std::remove_if(frontiers.begin(), frontiers.end(),
                       [this](const frontier_exploration::Frontier& f) {
                         std::cout << "f.centroid:" << f.centroid << std::endl;
                         if (!exploration_bbox_.contains(
                                 cv::Point(f.centroid.x, f.centroid.y))) {
                           std::cout << "Removed:" << std::endl;
                         }
                         return !exploration_bbox_.contains(
                             cv::Point(f.centroid.x, f.centroid.y));
                       }),
        frontiers.end());

    if (frontiers.empty()) {
      stop();
      continue;
    }

    // publish frontiers as visualization markers
    if (visualize_) {
      visualizeFrontiers(i, frontiers);
    }

    // find non blacklisted and non allocated frontier
    constexpr static size_t tolerance = 5;
    auto frontier =
        std::find_if_not(frontiers.begin(), frontiers.end(),
                         [this, &other_robot_frontiers,
                          &frontiers](const frontier_exploration::Frontier& f) {
                           if (goalOnBlacklist(f.centroid)) {
                             return true;
                           }
                           return false;
                         });

    if (frontier == frontiers.end()) {
      std::cout << "robot_namespaces:" << robot_namespaces_[i] << std::endl;
      std::cout << "no frontier available" << std::endl;
      stop();
      continue;
    }
    std::vector<frontier_exploration::Frontier> goal_frontiers;
    goal_frontiers.push_back(*frontier);
    visualizeFrontiers(i, goal_frontiers, false);

    other_robot_frontiers.push_back(*frontier);
    geometry_msgs::Point target_position = frontier->centroid;

    // time out if we are not making any progress
    bool same_goal = prev_goal_ == target_position;
    prev_goal_ = target_position;
    if (!same_goal || prev_distance_ > frontier->min_distance) {
      // we have different goal or we made some progress
      last_progress_ = ros::Time::now();
      prev_distance_ = frontier->min_distance;
    }
    // black list if we've made no progress for a long time
    if (ros::Time::now() - last_progress_ > progress_timeout_) {
      frontier_blacklist_.push_back(target_position);
      ROS_DEBUG("Adding current goal to black list");
      makePlan();
      continue;
    }

    // we don't need to do anything if we still pursuing the same goal
    if (same_goal) {
      continue;
    }

    // send goal to move_base if we have something new to pursue
    move_base_msgs::MoveBaseGoal goal;
    goal.target_pose.pose.position = target_position;
    goal.target_pose.pose.orientation.w = 1.;
    goal.target_pose.header.frame_id = costmap_client_.getGlobalFrameID();
    goal.target_pose.header.stamp = ros::Time::now();
    mbc->sendGoal(
        goal, [this, target_position](
                  const actionlib::SimpleClientGoalState& status,
                  const move_base_msgs::MoveBaseResultConstPtr& result) {
          reachedGoal(status, result, target_position);
        });
  }
}

bool Explore::goalOnBlacklist(const geometry_msgs::Point& goal)
{
  constexpr static size_t tolerance = 5;
  costmap_2d::Costmap2D* costmap2d = costmap_client_.getCostmap();

  // check if a goal is on the blacklist for goals that we're pursuing
  for (auto& frontier_goal : frontier_blacklist_) {
    double x_diff = fabs(goal.x - frontier_goal.x);
    double y_diff = fabs(goal.y - frontier_goal.y);

    if (x_diff < tolerance * costmap2d->getResolution() &&
        y_diff < tolerance * costmap2d->getResolution())
      return true;
  }
  return false;
}

bool Explore::frontierDuplicate(
    const frontier_exploration::Frontier& frontier,
    const std::vector<frontier_exploration::Frontier>& other_frontiers)
{
  constexpr static size_t tolerance = 5;
  costmap_2d::Costmap2D* costmap2d = costmap_client_.getCostmap();

  // check if a frontier already exists in other_frontiers
  for (auto& other_f : other_frontiers) {
    double x_diff = fabs(frontier.centroid.x - other_f.centroid.x);
    double y_diff = fabs(frontier.centroid.y - other_f.centroid.y);

    if (x_diff < tolerance * costmap2d->getResolution() &&
        y_diff < tolerance * costmap2d->getResolution())
      return true;
  }
  return false;
}

void Explore::reachedGoal(const actionlib::SimpleClientGoalState& status,
                          const move_base_msgs::MoveBaseResultConstPtr&,
                          const geometry_msgs::Point& frontier_goal)
{
  ROS_DEBUG("Reached goal with status: %s", status.toString().c_str());
  if (status == actionlib::SimpleClientGoalState::ABORTED) {
    frontier_blacklist_.push_back(frontier_goal);
    ROS_DEBUG("Adding current goal to black list");
  }

  // find new goal immediatelly regardless of planning frequency.
  // execute via timer to prevent dead lock in move_base_client (this is
  // callback for sendGoal, which is called in makePlan). the timer must live
  // until callback is executed.
  oneshot_ = relative_nh_.createTimer(
      ros::Duration(0, 0), [this](const ros::TimerEvent&) { makePlan(); },
      true);
}

void Explore::start()
{
  exploring_timer_.start();
}

void Explore::stop()
{
  for (auto& mbc : move_base_clients_) {
    if (!mbc->isServerConnected()) {
      continue;
    }
    mbc->cancelAllGoals();
  }
  exploring_timer_.stop();
  ROS_INFO("Exploration stopped.");
}

}  // namespace explore

int main(int argc, char** argv)
{
  ros::init(argc, argv, "explore");
  if (ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME,
                                     ros::console::levels::Debug)) {
    ros::console::notifyLoggerLevelsChanged();
  }
  explore::Explore explore;
  ros::spin();

  return 0;
}
