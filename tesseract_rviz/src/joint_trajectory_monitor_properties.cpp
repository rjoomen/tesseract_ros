#include <tesseract_rviz/joint_trajectory_monitor_properties.h>
#include <tesseract_rviz/conversions.h>

#include <tesseract_qt/joint_trajectory/widgets/joint_trajectory_widget.h>

#include <tesseract_qt/common/events/joint_trajectory_events.h>
#include <tesseract_qt/common/joint_trajectory_set.h>
#include <tesseract_qt/common/component_info.h>
#include <tesseract_qt/common/component_info_manager.h>

#include <trajectory_msgs/JointTrajectory.h>
#include <tesseract_msgs/Trajectory.h>
#include <tesseract_rosutils/utils.h>

#include <tesseract_common/serialization.h>
#include <tesseract_environment/environment.h>
#include <tesseract_environment/command.h>
#include <tesseract_command_language/composite_instruction.h>
#include <tesseract_command_language/utils.h>

#include <rviz/display.h>
#include <rviz/properties/ros_topic_property.h>
#include <rviz/panel_dock_widget.h>

#include <ros/subscriber.h>
#include <unordered_map>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <QApplication>

namespace tesseract_rviz
{
struct JointTrajectoryMonitorProperties::Implementation
{
  ros::NodeHandle nh;
  rviz::Display* parent;
  rviz::Property* main_property;

  std::shared_ptr<const tesseract_gui::ComponentInfo> component_info;

  rviz::BoolProperty* legacy_main;
  rviz::RosTopicProperty* legacy_joint_trajectory_topic_property;

  rviz::BoolProperty* tesseract_main;
  rviz::RosTopicProperty* tesseract_joint_trajectory_topic_property;

  ros::Subscriber legacy_joint_trajectory_sub;
  ros::Subscriber tesseract_joint_trajectory_sub;

  void legacyJointTrajectoryCallback(const trajectory_msgs::JointTrajectory::ConstPtr& msg)
  {
    if (msg->joint_names.empty())
      return;

    if (msg->points.empty())
      return;

    std::unordered_map<std::string, double> initial_state;
    for (std::size_t i = 0; i < msg->joint_names.size(); ++i)
      initial_state[msg->joint_names[i]] = msg->points[0].positions[i];

    tesseract_common::JointTrajectorySet trajectory_set(initial_state);
    tesseract_common::JointTrajectory joint_trajectory = tesseract_rosutils::fromMsg(*msg);
    trajectory_set.setUUID(joint_trajectory.uuid);
    trajectory_set.setDescription(joint_trajectory.description);
    trajectory_set.appendJointTrajectory(joint_trajectory);
    tesseract_gui::events::JointTrajectoryAdd event(component_info, trajectory_set);
    QApplication::sendEvent(qApp, &event);
  }

  void tesseractJointTrajectoryCallback(const tesseract_msgs::Trajectory::ConstPtr& msg)
  {
    try
    {
      tesseract_common::JointTrajectorySet trajectory_set;

      // Get environment initial state
      std::unordered_map<std::string, double> initial_state;
      for (const auto& pair_msg : msg->initial_state)
        initial_state[pair_msg.first] = pair_msg.second;

      // Get environment information
      tesseract_environment::Environment::UPtr environment = tesseract_rosutils::fromMsg(msg->environment);
      tesseract_environment::Commands commands = tesseract_rosutils::fromMsg(msg->commands);

      if (environment != nullptr)
      {
        trajectory_set = tesseract_common::JointTrajectorySet(std::move(environment));
      }
      else if (!commands.empty())
      {
        trajectory_set = tesseract_common::JointTrajectorySet(initial_state, commands);
      }
      else
      {
        trajectory_set = tesseract_common::JointTrajectorySet(initial_state);
      }

      if (!msg->ns.empty())
        trajectory_set.setNamespace(msg->ns);

      if (!msg->instructions.empty())
      {
        auto ci = tesseract_common::Serialization::fromArchiveStringXML<tesseract_planning::CompositeInstruction>(
            msg->instructions);
        trajectory_set.setUUID(ci.getUUID());
        trajectory_set.setDescription(ci.getDescription());
        if (!ci.empty() && ci.front().isCompositeInstruction())
        {
          for (const auto& entry : ci)
          {
            const auto& sub_ci = entry.as<tesseract_planning::CompositeInstruction>();
            tesseract_common::JointTrajectory joint_trajectory = tesseract_planning::toJointTrajectory(sub_ci);
            trajectory_set.appendJointTrajectory(joint_trajectory);
          }

          tesseract_gui::events::JointTrajectoryAdd event(component_info, trajectory_set);
          QApplication::sendEvent(qApp, &event);
        }
        else
        {
          tesseract_common::JointTrajectory joint_trajectory = tesseract_planning::toJointTrajectory(ci);
          trajectory_set.appendJointTrajectory(joint_trajectory);
          tesseract_gui::events::JointTrajectoryAdd event(component_info, trajectory_set);
          QApplication::sendEvent(qApp, &event);
        }
      }
      else
      {
        trajectory_set.setUUID(boost::lexical_cast<boost::uuids::uuid>(msg->joint_trajectories_uuid));
        trajectory_set.setDescription(msg->joint_trajectories_description);
        for (const auto& joint_trajectory_msg : msg->joint_trajectories)
        {
          tesseract_common::JointTrajectory joint_trajectory = tesseract_rosutils::fromMsg(joint_trajectory_msg);
          trajectory_set.appendJointTrajectory(joint_trajectory);
          tesseract_gui::events::JointTrajectoryAdd event(component_info, trajectory_set);
          QApplication::sendEvent(qApp, &event);
        }
      }
    }
    catch (...)
    {
      parent->setStatus(rviz::StatusProperty::Error, "Tesseract", "Failed to process trajectory message!");
    }
  }
};

JointTrajectoryMonitorProperties::JointTrajectoryMonitorProperties(rviz::Display* parent, rviz::Property* main_property)
  : data_(std::make_unique<Implementation>())
{
  data_->parent = parent;
  data_->component_info = tesseract_gui::ComponentInfoManager::create("rviz_scene");

  data_->main_property = main_property;
  if (data_->main_property == nullptr)
    data_->main_property = data_->parent;

  data_->legacy_main = new rviz::BoolProperty("Legacy Joint Trajectory",
                                              true,
                                              "This will monitor this topic for trajectory_msgs::JointTrajectory "
                                              "messages.",
                                              data_->main_property,
                                              SLOT(onLegacyJointTrajectoryChanged()),
                                              this);
  data_->legacy_main->setDisableChildrenIfFalse(true);

  data_->legacy_joint_trajectory_topic_property =
      new rviz::RosTopicProperty("topic",
                                 "/joint_trajectory",
                                 ros::message_traits::datatype<trajectory_msgs::JointTrajectory>(),
                                 "This will monitor this topic for trajectory_msgs::JointTrajectory messages.",
                                 data_->legacy_main,
                                 SLOT(onLegacyJointTrajectoryTopicChanged()),
                                 this);

  data_->tesseract_main = new rviz::BoolProperty("Tesseract Joint Trajectory",
                                                 true,
                                                 "This will monitor this topic for trajectory_msgs::JointTrajectory "
                                                 "messages.",
                                                 data_->main_property,
                                                 SLOT(onTesseractJointTrajectoryChanged()),
                                                 this);
  data_->tesseract_main->setDisableChildrenIfFalse(true);

  data_->tesseract_joint_trajectory_topic_property =
      new rviz::RosTopicProperty("Joint Trajectory Topic",
                                 "/tesseract_trajectory",
                                 ros::message_traits::datatype<tesseract_msgs::Trajectory>(),
                                 "This will monitor this topic for tesseract_msgs::Trajectory messages.",
                                 data_->tesseract_main,
                                 SLOT(onTesseractJointTrajectoryTopicChanged()),
                                 this);
}

JointTrajectoryMonitorProperties::~JointTrajectoryMonitorProperties() = default;

void JointTrajectoryMonitorProperties::onInitialize()
{
  onLegacyJointTrajectoryTopicConnect();
  onTesseractJointTrajectoryTopicConnect();
}

void JointTrajectoryMonitorProperties::setComponentInfo(
    std::shared_ptr<const tesseract_gui::ComponentInfo> component_info)
{
  data_->component_info = std::move(component_info);
}

std::shared_ptr<const tesseract_gui::ComponentInfo> JointTrajectoryMonitorProperties::getComponentInfo() const
{
  return data_->component_info;
}

void JointTrajectoryMonitorProperties::load(const rviz::Config& config)
{
  QString topic;
  if (config.mapGetString("tesseract::LegacyJointTrajectoryTopic", &topic))
    data_->legacy_joint_trajectory_topic_property->setString(topic);

  if (config.mapGetString("tesseract::TesseractJointTrajectoryTopic", &topic))
    data_->tesseract_joint_trajectory_topic_property->setString(topic);
}

void JointTrajectoryMonitorProperties::save(rviz::Config config) const
{
  config.mapSetValue("tesseract::LegacyJointTrajectoryTopic",
                     data_->legacy_joint_trajectory_topic_property->getString());
  config.mapSetValue("tesseract::TesseractJointTrajectoryTopic",
                     data_->tesseract_joint_trajectory_topic_property->getString());
}

void JointTrajectoryMonitorProperties::onLegacyJointTrajectoryTopicConnect()
{
  data_->legacy_joint_trajectory_sub =
      data_->nh.subscribe(data_->legacy_joint_trajectory_topic_property->getStdString(),
                          20,
                          &JointTrajectoryMonitorProperties::Implementation::legacyJointTrajectoryCallback,
                          data_.get());
}

void JointTrajectoryMonitorProperties::onTesseractJointTrajectoryTopicConnect()
{
  data_->tesseract_joint_trajectory_sub =
      data_->nh.subscribe(data_->tesseract_joint_trajectory_topic_property->getStdString(),
                          20,
                          &JointTrajectoryMonitorProperties::Implementation::tesseractJointTrajectoryCallback,
                          data_.get());
}

void JointTrajectoryMonitorProperties::onLegacyJointTrajectoryTopicDisconnect()
{
  data_->legacy_joint_trajectory_sub.shutdown();
}

void JointTrajectoryMonitorProperties::onTesseractJointTrajectoryTopicDisconnect()
{
  data_->tesseract_joint_trajectory_sub.shutdown();
}

void JointTrajectoryMonitorProperties::onLegacyJointTrajectoryTopicChanged()
{
  onLegacyJointTrajectoryTopicDisconnect();
  onLegacyJointTrajectoryTopicConnect();
}

void JointTrajectoryMonitorProperties::onTesseractJointTrajectoryTopicChanged()
{
  onTesseractJointTrajectoryTopicDisconnect();
  onTesseractJointTrajectoryTopicConnect();
}

void JointTrajectoryMonitorProperties::onLegacyJointTrajectoryChanged()
{
  if (data_->legacy_main->getBool())
    onLegacyJointTrajectoryTopicConnect();
  else
    onLegacyJointTrajectoryTopicDisconnect();
}

void JointTrajectoryMonitorProperties::onTesseractJointTrajectoryChanged()
{
  if (data_->tesseract_main->getBool())
    onTesseractJointTrajectoryTopicConnect();
  else
    onTesseractJointTrajectoryTopicDisconnect();
}
}  // namespace tesseract_rviz
