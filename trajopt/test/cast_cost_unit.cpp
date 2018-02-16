#include <gtest/gtest.h>
#include <trajopt_utils/stl_to_string.hpp>
#include <trajopt/common.hpp>
#include <trajopt/problem_description.hpp>
#include <trajopt_sco/optimizers.hpp>
#include <ctime>
#include <trajopt_utils/eigen_conversions.hpp>
#include <trajopt_utils/clock.hpp>
#include <boost/foreach.hpp>
#include <boost/assign.hpp>
#include <trajopt_utils/config.hpp>
#include <trajopt/plot_callback.hpp>
#include <trajopt_test_utils.hpp>
#include <trajopt/collision_terms.hpp>
#include <trajopt_utils/logging.hpp>

#include <trajopt/ros_kin.h>
#include <trajopt/ros_env.h>

#include <ros/ros.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_model/joint_model_group.h>
#include <moveit/collision_plugin_loader/collision_plugin_loader.h>

using namespace trajopt;
using namespace std;
using namespace util;
using namespace boost::assign;

const std::string ROBOT_DESCRIPTION_PARAM = "robot_description"; /**< Default ROS parameter for robot description */
bool plotting=false;

class CastTest : public testing::TestWithParam<const char*> {
public:
  robot_model_loader::RobotModelLoaderPtr loader_;  /**< Used to load the robot model */
  moveit::core::RobotModelPtr robot_model_;         /**< Robot model */
  planning_scene::PlanningScenePtr planning_scene_; /**< Planning scene for the current robot model */
  ROSEnvPtr env_;                                   /**< Trajopt Basic Environment */

  virtual void SetUp()
  {
    loader_.reset(new robot_model_loader::RobotModelLoader(ROBOT_DESCRIPTION_PARAM));
    robot_model_ = loader_->getModel();
    env_ = ROSEnvPtr(new ROSEnv);
    ASSERT_TRUE(robot_model_ != nullptr);
    ASSERT_NO_THROW(planning_scene_.reset(new planning_scene::PlanningScene(robot_model_)));

    //Now assign collision detection plugin
    collision_detection::CollisionPluginLoader cd_loader;
    std::string class_name = "BULLET";
    ASSERT_TRUE(cd_loader.activate(class_name, planning_scene_, true));

    ASSERT_TRUE(env_->init(planning_scene_));

    gLogLevel = util::LevelInfo;
  }
};

TEST_F(CastTest, boxes) {
  ROS_DEBUG("CastTest, boxes");
  Json::Value root = readJsonFile(string(DATA_DIR) + "/box_cast_test.json");

  robot_state::RobotState &rs = planning_scene_->getCurrentStateNonConst();
  std::map<std::string, double> ipos;
  ipos["boxbot_x_joint"] = -1.9;
  ipos["boxbot_y_joint"] = 0;
  rs.setVariablePositions(ipos);

  TrajOptProbPtr prob = ConstructProblem(root, env_);
  ASSERT_TRUE(!!prob);

  std::vector<trajopt::BasicEnv::DistanceResult> collisions;
  std::vector<std::string> joint_names, link_names;
  prob->GetKin()->getJointNames(joint_names);
  prob->GetKin()->getLinkNames(link_names);

  env_->continuousCollisionCheckTrajectory(joint_names, link_names, prob->GetInitTraj(), collisions);
  ROS_DEBUG("Initial trajector number of continuous collisions: %i\n", collisions.size());
  ASSERT_NE(collisions.size(), 0);

  BasicTrustRegionSQP opt(prob);
  if (plotting) opt.addCallback(PlotCallback(*prob));
  opt.initialize(trajToDblVec(prob->GetInitTraj()));
  opt.optimize();

  if (plotting) prob->GetEnv()->plotClear();

  collisions.clear();
  env_->continuousCollisionCheckTrajectory(joint_names, link_names, getTraj(opt.x(), prob->GetVars()), collisions);
  ROS_DEBUG("Final trajectory number of continuous collisions: %i\n", collisions.size());
  ASSERT_EQ(collisions.size(), 0);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  ros::init(argc, argv, "trajopt_cast_cost_unit");
  ros::NodeHandle pnh("~");

  pnh.param("plotting", plotting, false);
  return RUN_ALL_TESTS();
}
