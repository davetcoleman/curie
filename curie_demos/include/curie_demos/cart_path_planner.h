/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2016, University of Colorado, Boulder
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
 *   * Neither the name of PickNik LLC nor the names of its
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
 *********************************************************************/

/* Author: Dave Coleman
   Desc:   Creates a cartesian path to be inserted into a planning roadmap
*/

#ifndef CURIE_DEMOS_CART_PATH_PLANNER_H
#define CURIE_DEMOS_CART_PATH_PLANNER_H

// MoveIt
#include <moveit/robot_state/robot_state.h>
#include <moveit_visual_tools/moveit_visual_tools.h>

// ROS
#include <ros/ros.h>

// this package
#include <curie_demos/imarker_robot_state.h>
#include <curie_demos/ur5_descartes_app.h>

namespace curie_demos
{
class CurieDemos;

class CartPathPlanner
{
public:
  /**
   * \brief Constructor
   */
  CartPathPlanner(CurieDemos* parent);

  void processIMarkerPose(const visualization_msgs::InteractiveMarkerFeedbackConstPtr& feedback,
                          const Eigen::Affine3d& feedback_pose);

  bool computeMoveItCartPath(const Eigen::Affine3d &start_pose);
  bool computeDescartesCartPath(const Eigen::Affine3d &start_pose);

  /**
   * \brief Get the cartesian trajectory
   * \param trajectory to populate
   * \return true on success
   */
  bool getTrajectory(std::vector<moveit::core::RobotStatePtr>& trajectory);

private:
  // --------------------------------------------------------

  // The short name of this class
  std::string name_;

  // A shared node handle
  ros::NodeHandle nh_;

  // Parent class
  CurieDemos* parent_;

  // State
  moveit::core::RobotStatePtr imarker_state_;

  // Trajetory
  std::vector<moveit::core::RobotStatePtr> trajectory_;

  // Interactive markers
  IMarkerRobotStatePtr imarker_cartesian_;

  // Descartes
  UR5DescartesAppPtr ur5_descartes_app_;
};  // end class

// Create boost pointers for this class
typedef boost::shared_ptr<CartPathPlanner> CartPathPlannerPtr;
typedef boost::shared_ptr<const CartPathPlanner> CartPathPlannerConstPtr;

}  // namespace curie_demos
#endif  // CURIE_DEMOS_CART_PATH_PLANNER_H
