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

// ROS parameter loading
#include <rosparam_shortcuts/rosparam_shortcuts.h>

// this package
#include <curie_demos/cart_path_planner.h>
#include <curie_demos/curie_demos.h>

// moveit_boilerplate
#include <moveit_boilerplate/namespaces.h>

namespace curie_demos
{
CartPathPlanner::CartPathPlanner(CurieDemos* parent) : name_("cart_path_planner"), nh_("~"), parent_(parent)
{
  jmg_ = parent_->jmg_;

  // Load planning state
  imarker_state_.reset(new moveit::core::RobotState(*parent_->moveit_start_));

  // Create cartesian start pose interactive marker
  imarker_cartesian_.reset(
      new IMarkerRobotState(parent_->getPlanningSceneMonitor(), "cart", jmg_, parent_->ee_link_, rvt::BLUE));
  imarker_cartesian_->setIMarkerCallback(
      std::bind(&CartPathPlanner::processIMarkerPose, this, std::placeholders::_1, std::placeholders::_2));

  // Set visual tools
  visual_tools_ = imarker_cartesian_->getVisualTools();
  visual_tools_->setMarkerTopic(nh_.getNamespace() + "/cartesian_trajectory");
  visual_tools_->loadMarkerPub(true);
  visual_tools_->deleteAllMarkers();
  visual_tools_->setManualSceneUpdating(true);
  visual_tools_->loadTrajectoryPub(nh_.getNamespace() + "/display_trajectory");

  // Load Descartes ------------------------------------------------

  // loading parameters
  loadParameters();

  // initializing descartes
  initDescartes();

  ROS_INFO_STREAM_NAMED(name_, "CartPathPlanner Ready.");
}

void CartPathPlanner::processIMarkerPose(const visualization_msgs::InteractiveMarkerFeedbackConstPtr& feedback,
                                         const Eigen::Affine3d& feedback_pose)
{
  imarker_state_ = imarker_cartesian_->getRobotState();
  Eigen::Affine3d start_pose = imarker_state_->getGlobalLinkTransform(parent_->ee_link_);

  // visualizeMoveItCartPath(start_pose);
  visualizeDescartesCartPath(start_pose);
}

bool CartPathPlanner::visualizeDescartesCartPath(const Eigen::Affine3d& start_pose)
{
  ROS_DEBUG_STREAM_NAMED(name_, "visualizeDescartesCartPath()");

  // generating trajectory
  curie_demos::DescartesTrajectory traj;
  generateCartTrajectory(traj, start_pose);

  return true;
}

bool CartPathPlanner::generateCartGraph(ompl::tools::bolt::TaskGraphPtr task_graph)
{
  imarker_state_ = imarker_cartesian_->getRobotState();
  Eigen::Affine3d start_pose = imarker_state_->getGlobalLinkTransform(parent_->ee_link_);

  // Generating trajectory
  curie_demos::DescartesTrajectory traj;
  if (!generateCartTrajectory(traj, start_pose))
  {
    ROS_ERROR_STREAM_NAMED(name_, "Failed to generate full cart trajectory");
    return false;
  }

  // Benchmark runtime
  ros::Time start_time = ros::Time::now();

  // planning robot path
  bool result = planner_.insertGraph(traj);

  if (result)
  {
    std::cout << std::endl;
    std::cout << "-------------------------------------------------------" << std::endl;
    std::cout << "-------------------------------------------------------" << std::endl;
    double duration = (ros::Time::now() - start_time).toSec();
    ROS_INFO_STREAM_NAMED(name_, "Graph generated in " << duration << " seconds");
    std::cout << std::endl;
  }
  else
  {
    ROS_ERROR_STREAM("Could not create graph");
    exit(-1);
  }

  // Convert graph formats from Descartes to Bolt
  using namespace descartes_planner;
  using namespace descartes_core;
  using namespace descartes_trajectory;

  // Iterate through vertices
  std::size_t new_vertex_count = 0;
  PlanningGraph& g = planner_.getPlanningGraph();
  std::pair<VertexIterator, VertexIterator> vi = vertices(g.getGraph());
  for (VertexIterator vert_iter = vi.first; vert_iter != vi.second; ++vert_iter)
  {
    JointGraph::vertex_descriptor jv = *vert_iter;
    g.getVertexData(jv);

    // Get the joint values for this vertex and convert to a MoveIt! robot state
    TrajectoryPt::ID tajectory_id = g.getGraph()[jv].id;
    JointMap& joint_map = g.getJointMap();
    JointTrajectoryPt &pt = joint_map[tajectory_id];

    std::vector<double> joint_pose;
    std::vector<double> empty_seed;
    pt.getNominalJointPose(empty_seed, *ur5_robot_model_, joint_pose);

    std::cout << "joint pose: ";
    std::copy(joint_pose.begin(), joint_pose.end(), std::ostream_iterator<double>(std::cout, ", "));
    std::cout << std::endl;

    // Convert the MoveIt! robot state into an OMPL state

    // Add vertex to task graph
    // task_graph->addVertex();

    new_vertex_count++;
  }
  ROS_DEBUG_STREAM_NAMED(name_, "Added " << new_vertex_count << " new vertices");

  std::cout << "done for now " << std::endl;
  exit(0);

  return true;
}

void CartPathPlanner::initDescartes()
{
  ROS_INFO_STREAM_NAMED(name_, "initDescartes()");

  // Instantiating a robot model
  const std::string prefix = "right_";
  ur5_robot_model_.reset(new ur5_demo_descartes::UR5RobotModel(prefix));

  /*  Fill Code:
   * Goal:
   * - Initialize the "robot_model_ptr" variable by passing the required application parameters
   *    into its "initialize" method.
   * Hint:
   * - The config_ structure contains the variables needed by the robot model
   * - The "initialize" method takes the following arguments in this order
   *    a - robot description string
   *    b - group_name string.
   *    c - world_frame string
   *    d - tip_link string.
   */
  if (ur5_robot_model_->initialize(ROBOT_DESCRIPTION_PARAM, config_.group_name, config_.world_frame, config_.tip_link))
  {
    ROS_INFO_STREAM("Descartes Robot Model initialized");
  }
  else
  {
    ROS_ERROR_STREAM("Failed to initialize Robot Model");
    exit(-1);
  }

  // Set collision checking.
  ur5_robot_model_->setCheckCollisions(check_collisions_);
  if (!check_collisions_)
    ROS_WARN_STREAM_NAMED(name_, "Collision checking disabled");

  bool succeeded = planner_.initialize(ur5_robot_model_);
  if (succeeded)
  {
    ROS_INFO_STREAM("Descartes Dense Planner initialized");
  }
  else
  {
    ROS_ERROR_STREAM("Failed to initialize Dense Planner");
    exit(-1);
  }
}

void CartPathPlanner::loadParameters()
{
  ROS_INFO_STREAM_NAMED(name_, "loadParameters()");

  // Load rosparams
  ros::NodeHandle rpnh(nh_, name_);
  std::size_t error = 0;
  error += !rosparam_shortcuts::get(name_, rpnh, "group_name", config_.group_name);
  error += !rosparam_shortcuts::get(name_, rpnh, "tip_link", config_.tip_link);
  error += !rosparam_shortcuts::get(name_, rpnh, "base_link", config_.base_link);
  error += !rosparam_shortcuts::get(name_, rpnh, "world_frame", config_.world_frame);
  error += !rosparam_shortcuts::get(name_, rpnh, "check_collisions", check_collisions_);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/time_delay", config_.time_delay);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/foci_distance", config_.foci_distance);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/radius", config_.radius);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/num_points", config_.num_points);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/num_lemniscates", config_.num_lemniscates);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/center", config_.center);
  error += !rosparam_shortcuts::get(name_, rpnh, "trajectory/seed_pose", config_.seed_pose);
  error += !rosparam_shortcuts::get(name_, rpnh, "visualization/min_point_distance", config_.min_point_distance);
  error += !rosparam_shortcuts::get(name_, rpnh, "controller_joint_names", config_.joint_names);
  rosparam_shortcuts::shutdownIfError(name_, error);
}

moveit_msgs::RobotTrajectory CartPathPlanner::runPath(const DescartesTrajectory& path)
{
  ROS_INFO_STREAM_NAMED(name_, "runPath()");

  std::vector<double> seed_pose(ur5_robot_model_->getDOF());
  std::vector<double> start_pose;

  descartes_core::TrajectoryPtPtr first_point_ptr = path[0];
  first_point_ptr->getNominalJointPose(seed_pose, *ur5_robot_model_, start_pose);

  // creating Moveit trajectory from Descartes Trajectory
  moveit_msgs::RobotTrajectory moveit_traj;
  fromDescartesToMoveitTraj(path, moveit_traj.joint_trajectory);

  // sending robot path to server for execution
  return moveit_traj;
}

void CartPathPlanner::publishPosesMarkers(const EigenSTL::vector_Affine3d& poses)
{
  ROS_DEBUG_STREAM_NAMED(name_, "publishPosesMarkers() world_frame: " << visual_tools_->getBaseFrame());

  // creating rviz markers
  visualization_msgs::Marker z_axes, y_axes, x_axes, line;
  visualization_msgs::MarkerArray markers_msg;

  z_axes.type = y_axes.type = x_axes.type = visualization_msgs::Marker::LINE_LIST;
  z_axes.ns = y_axes.ns = x_axes.ns = "axes";
  z_axes.action = y_axes.action = x_axes.action = visualization_msgs::Marker::ADD;
  z_axes.lifetime = y_axes.lifetime = x_axes.lifetime = ros::Duration(0);
  z_axes.header.frame_id = y_axes.header.frame_id = x_axes.header.frame_id = visual_tools_->getBaseFrame();
  z_axes.scale.x = y_axes.scale.x = x_axes.scale.x = AXIS_LINE_WIDTH;

  // z properties
  z_axes.id = 0;
  z_axes.color.r = 0;
  z_axes.color.g = 0;
  z_axes.color.b = 1;
  z_axes.color.a = 1;

  // y properties
  y_axes.id = 1;
  y_axes.color.r = 0;
  y_axes.color.g = 1;
  y_axes.color.b = 0;
  y_axes.color.a = 1;

  // x properties
  x_axes.id = 2;
  x_axes.color.r = 1;
  x_axes.color.g = 0;
  x_axes.color.b = 0;
  x_axes.color.a = 1;

  // line properties
  line.type = visualization_msgs::Marker::LINE_STRIP;
  line.ns = "line";
  line.action = visualization_msgs::Marker::ADD;
  line.lifetime = ros::Duration(0);
  line.header.frame_id = visual_tools_->getBaseFrame();
  line.scale.x = LINE_WIDTH;
  line.id = 0;
  line.color.r = 1;
  line.color.g = 1;
  line.color.b = 0;
  line.color.a = 1;

  // creating axes markers
  z_axes.points.reserve(2 * poses.size());
  y_axes.points.reserve(2 * poses.size());
  x_axes.points.reserve(2 * poses.size());
  line.points.reserve(poses.size());
  geometry_msgs::Point p_start, p_end;
  double distance = 0;
  Eigen::Affine3d prev = poses[0];
  for (unsigned int i = 0; i < poses.size(); i++)
  {
    const Eigen::Affine3d& pose = poses[i];
    distance = (pose.translation() - prev.translation()).norm();

    tf::pointEigenToMsg(pose.translation(), p_start);

    if (distance > config_.min_point_distance)
    {
      Eigen::Affine3d moved_along_x = pose * Eigen::Translation3d(AXIS_LINE_LENGTH, 0, 0);
      tf::pointEigenToMsg(moved_along_x.translation(), p_end);
      x_axes.points.push_back(p_start);
      x_axes.points.push_back(p_end);

      Eigen::Affine3d moved_along_y = pose * Eigen::Translation3d(0, AXIS_LINE_LENGTH, 0);
      tf::pointEigenToMsg(moved_along_y.translation(), p_end);
      y_axes.points.push_back(p_start);
      y_axes.points.push_back(p_end);

      Eigen::Affine3d moved_along_z = pose * Eigen::Translation3d(0, 0, AXIS_LINE_LENGTH);
      tf::pointEigenToMsg(moved_along_z.translation(), p_end);
      z_axes.points.push_back(p_start);
      z_axes.points.push_back(p_end);

      // saving previous
      prev = pose;
    }

    line.points.push_back(p_start);
  }

  markers_msg.markers.push_back(x_axes);
  markers_msg.markers.push_back(y_axes);
  markers_msg.markers.push_back(z_axes);
  markers_msg.markers.push_back(line);

  visual_tools_->publishMarkers(markers_msg);
  visual_tools_->triggerBatchPublish();
}

bool CartPathPlanner::createLemniscateCurve(double foci_distance, double sphere_radius, int num_points,
                                            int num_lemniscates, const Eigen::Vector3d& sphere_center,
                                            EigenSTL::vector_Affine3d& poses)
{
  ROS_DEBUG_STREAM_NAMED(name_, "createLemniscateCurve()");

  double a = foci_distance;
  double ro = sphere_radius;
  int npoints = num_points;
  int nlemns = num_lemniscates;
  Eigen::Vector3d offset(sphere_center[0], sphere_center[1], sphere_center[2]);
  Eigen::Vector3d unit_z, unit_y, unit_x;

  // checking parameters
  if (a <= 0 || ro <= 0 || npoints < 10 || nlemns < 1)
  {
    ROS_ERROR_STREAM("Invalid parameters for lemniscate curve were found");
    return false;
  }

  // generating polar angle values
  std::vector<double> theta(npoints);

  // interval 1 <-pi/4 , pi/4 >
  double d_theta = 2 * M_PI_2 / (npoints - 1);
  for (unsigned int i = 0; i < static_cast<std::size_t>(npoints) / 2; i++)
  {
    theta[i] = -M_PI_4 + i * d_theta;
  }
  theta[0] = theta[0] + EPSILON;
  theta[npoints / 2 - 1] = theta[npoints / 2 - 1] - EPSILON;

  // interval 2 < 3*pi/4 , 5 * pi/4 >
  for (unsigned int i = 0; i < static_cast<std::size_t>(npoints) / 2; i++)
  {
    theta[npoints / 2 + i] = 3 * M_PI_4 + i * d_theta;
  }
  theta[npoints / 2] = theta[npoints / 2] + EPSILON;
  theta[npoints - 1] = theta[npoints - 1] - EPSILON;

  // generating omega angle (lemniscate angle offset)
  std::vector<double> omega(nlemns);
  double d_omega = M_PI / (nlemns);
  for (unsigned int i = 0; i < static_cast<std::size_t>(nlemns); i++)
  {
    omega[i] = i * d_omega;
  }

  Eigen::Affine3d pose;
  double x, y, z, r, phi;

  poses.clear();
  poses.reserve(nlemns * npoints);
  for (unsigned int j = 0; j < static_cast<std::size_t>(nlemns); j++)
  {
    for (unsigned int i = 0; i < static_cast<std::size_t>(npoints); i++)
    {
      r = std::sqrt(std::pow(a, 2) * std::cos(2 * theta[i]));
      phi = r < ro ? std::asin(r / ro) : (M_PI - std::asin((2 * ro - r) / ro));

      x = ro * std::cos(theta[i] + omega[j]) * std::sin(phi);
      y = ro * std::sin(theta[i] + omega[j]) * std::sin(phi);
      z = ro * std::cos(phi);

      // determining orientation
      unit_z << -x, -y, -z;
      unit_z.normalize();

      unit_x = (Eigen::Vector3d(0, 1, 0).cross(unit_z)).normalized();
      unit_y = (unit_z.cross(unit_x)).normalized();

      Eigen::Matrix3d rot;
      rot << unit_x(0), unit_y(0), unit_z(0), unit_x(1), unit_y(1), unit_z(1), unit_x(2), unit_y(2), unit_z(2);

      pose = Eigen::Translation3d(offset(0) + x, offset(1) + y, offset(2) + z) * rot;

      // DTC: Rotate -90 so that the x axis points down
      pose = pose * Eigen::AngleAxisd(-M_PI / 2.0, Eigen::Vector3d::UnitY());

      poses.push_back(pose);
    }
  }

  return true;
}

void CartPathPlanner::fromDescartesToMoveitTraj(const DescartesTrajectory& in_traj,
                                                trajectory_msgs::JointTrajectory& out_traj)
{
  ROS_INFO_STREAM_NAMED(name_, "fromDescartesToMoveitTraj()");

  // Fill out information about our trajectory
  out_traj.header.stamp = ros::Time::now();
  out_traj.header.frame_id = config_.world_frame;
  out_traj.joint_names = config_.joint_names;

  // For keeping track of time-so-far in the trajectory
  double time_offset = 0.0;

  // Loop through the trajectory
  for (unsigned int i = 0; i < in_traj.size(); i++)
  {
    // Find nominal joint solution at this point
    std::vector<double> joints;

    // getting joint position at current point
    const descartes_core::TrajectoryPtPtr& joint_point = in_traj[i];
    joint_point->getNominalJointPose(std::vector<double>(), *ur5_robot_model_, joints);

    // Fill out a ROS trajectory point
    trajectory_msgs::JointTrajectoryPoint pt;
    pt.positions = joints;
    // velocity, acceleration, and effort are given dummy values
    // we'll let the controller figure them out
    pt.velocities.resize(joints.size(), 0.0);
    pt.accelerations.resize(joints.size(), 0.0);
    pt.effort.resize(joints.size(), 0.0);
    // set the time into the trajectory
    pt.time_from_start = ros::Duration(time_offset);
    // increment time
    time_offset += config_.time_delay;

    out_traj.points.push_back(pt);
  }
}

void CartPathPlanner::fromDescartesToMoveitTraj(const DescartesTrajectory& in_traj,
                                                std::vector<moveit::core::RobotStatePtr>& out_traj)
{
  ROS_INFO_STREAM_NAMED(name_, "fromDescartesToMoveitTraj()");

  BOOST_ASSERT_MSG(!in_traj.empty(), "Input trajectory is empty!");

  // Loop through the trajectory
  for (unsigned int i = 0; i < in_traj.size(); i++)
  {
    // Find nominal joint solution at this point
    std::vector<double> joints;

    // getting joint position at current point
    const descartes_core::TrajectoryPtPtr& joint_point = in_traj[i];
    joint_point->getNominalJointPose(std::vector<double>(), *ur5_robot_model_, joints);

    // Create new robot state
    moveit::core::RobotStatePtr newRobotState(new moveit::core::RobotState(*visual_tools_->getSharedRobotState()));
    newRobotState->setJointGroupPositions(jmg_, joints);

    // Add to trajectory
    out_traj.push_back(newRobotState);
  }
}

bool CartPathPlanner::generateCartTrajectory(DescartesTrajectory& traj, const Eigen::Affine3d& start_pose)
{
  ROS_DEBUG_STREAM_NAMED(name_, "generateCartTrajectory()");

  using namespace descartes_core;
  using namespace descartes_trajectory;

  // generating trajectory using a lemniscate curve function.
  EigenSTL::vector_Affine3d poses;

  // Eigen::Vector3d sphere_center(config_.center[0], config_.center[1], config_.center[2]);
  Eigen::Vector3d sphere_center = start_pose.translation();
  sphere_center.z() -= 0.1;  // move center down a bit from end effector

  if (createLemniscateCurve(config_.foci_distance, config_.radius, config_.num_points, config_.num_lemniscates,
                            sphere_center, poses))
  {
    ROS_DEBUG_STREAM("Trajectory with " << poses.size() << " points was generated");
  }
  else
  {
    ROS_ERROR_STREAM("Trajectory generation failed");
    exit(-1);
  }

  // TODO(davetcoleman): remove
  // Remove most of trajectory just to save time
  std::size_t use_first_count = 5;  // only save the front of the vector
  ROS_DEBUG_STREAM_NAMED(name_, "Truncating trajectory to size " << use_first_count
                                                                 << " reduce computation time during "
                                                                    "testing");
  poses.erase(poses.begin() + use_first_count, poses.end());

  // publishing trajectory poses for visualization
  publishPosesMarkers(poses);

  // creating descartes trajectory points
  traj.clear();
  traj.reserve(poses.size());
  for (unsigned int i = 0; i < poses.size(); i++)
  {
    const Eigen::Affine3d& pose = poses[i];

    // Get all possible solutions
    std::vector<std::vector<double> > joint_poses;
    if (!ur5_robot_model_->getAllIK(pose, joint_poses))
    {
      ROS_ERROR_STREAM_NAMED(name_, "getAllIK returned false for pose " << i);
      return false;
    }

    // Error check
    if (joint_poses.empty())
    {
      ROS_ERROR_STREAM_NAMED(name_, "getAllIK returned no solutions");
      return false;
    }

    // Show all possible configurations
    if (false)
    {
      for (std::vector<double> pose : joint_poses)
      {
        visual_tools_->publishRobotState(pose, jmg_);
        ros::Duration(0.001).sleep();

        if (!ros::ok())
          exit(0);

        break;  // do not show all possible configs
      }
    }

    // Create AxialSymetricPt objects in order to define a trajectory cartesian point with rotational freedom about the
    // tool's z axis.
    {
      using namespace descartes_core;
      TrajectoryPtPtr pt = TrajectoryPtPtr(new descartes_trajectory::AxialSymmetricPt(
          pose, ORIENTATION_INCREMENT, descartes_trajectory::AxialSymmetricPt::FreeAxis::Z_AXIS,
          TimingConstraint(0.5)));
      // saving points into trajectory
      traj.push_back(pt);
    }
  }

  ROS_INFO_STREAM_NAMED(name_, "getAllIK found all solutions for trajectory");
  return true;
}

}  // namespace curie_demos
