// Copyright 2022 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mrm_emergency_stop_operator/mrm_emergency_stop_operator_core.hpp"

namespace mrm_emergency_stop_operator
{

MrmEmergencyStopOperator::MrmEmergencyStopOperator(const rclcpp::NodeOptions & node_options)
: Node("mrm_emergency_stop_operator", node_options)
{
  // Parameter
  params_.update_rate = static_cast<int>(declare_parameter<int>("update_rate", 30));
  params_.target_acceleration = declare_parameter<double>("target_acceleration", -2.5);
  params_.target_jerk = declare_parameter<double>("target_jerk", -1.5);
  params_.steering_handling_type = static_cast<int>(declare_parameter<int>("steering_handling_type", 0));

  // Subscriber
  sub_control_cmd_ = create_subscription<AckermannControlCommand>(
    "~/input/control/control_cmd", 1,
    std::bind(&MrmEmergencyStopOperator::onControlCommand, this, std::placeholders::_1));

  // Server
  service_operation_ = create_service<OperateMrm>(
    "~/input/mrm/emergency_stop/operate", std::bind(
                                            &MrmEmergencyStopOperator::operateEmergencyStop, this,
                                            std::placeholders::_1, std::placeholders::_2));

  // Publisher
  pub_status_ = create_publisher<MrmBehaviorStatus>("~/output/mrm/emergency_stop/status", 1);
  pub_control_cmd_ =
    create_publisher<AckermannControlCommand>("~/output/mrm/emergency_stop/control_cmd", 1);

  // Timer
  const auto update_period_ns = rclcpp::Rate(params_.update_rate).period();
  timer_ = rclcpp::create_timer(
    this, get_clock(), update_period_ns, std::bind(&MrmEmergencyStopOperator::onTimer, this));

  // Initialize
  status_.state = MrmBehaviorStatus::AVAILABLE;
  is_prev_control_cmd_subscribed_ = false;
}

void MrmEmergencyStopOperator::onControlCommand(AckermannControlCommand::ConstSharedPtr msg)
{
  if (status_.state != MrmBehaviorStatus::OPERATING) {
    prev_control_cmd_ = *msg;
    is_prev_control_cmd_subscribed_ = true;
  }
}

void MrmEmergencyStopOperator::operateEmergencyStop(
  const OperateMrm::Request::SharedPtr request, const OperateMrm::Response::SharedPtr response)
{
  if (request->operate == true) {
    if (is_prev_control_cmd_subscribed_) {
      lateral_cmd_at_start_of_emergency_stop_ = prev_control_cmd_.lateral;
    } else {
      lateral_cmd_at_start_of_emergency_stop_.steering_tire_angle = 0.0;
      lateral_cmd_at_start_of_emergency_stop_.steering_tire_rotation_rate = 0.0;
    }
    status_.state = MrmBehaviorStatus::OPERATING;
    response->response.success = true;
  } else {
    status_.state = MrmBehaviorStatus::AVAILABLE;
    response->response.success = true;
  }
}

void MrmEmergencyStopOperator::publishStatus() const
{
  auto status = status_;
  status.stamp = this->now();
  pub_status_->publish(status);
}

void MrmEmergencyStopOperator::publishControlCommand(const AckermannControlCommand & command) const
{
  pub_control_cmd_->publish(command);
}

void MrmEmergencyStopOperator::onTimer()
{
  if (status_.state == MrmBehaviorStatus::OPERATING) {
    auto control_cmd = calcTargetAcceleration(prev_control_cmd_);
    publishControlCommand(control_cmd);
    prev_control_cmd_ = control_cmd;
  } else {
    publishControlCommand(prev_control_cmd_);
  }
  publishStatus();
}

AckermannControlCommand MrmEmergencyStopOperator::calcTargetAcceleration(
  const AckermannControlCommand & prev_control_cmd) const
{
  auto control_cmd = AckermannControlCommand();

  if (!is_prev_control_cmd_subscribed_) {
    control_cmd.stamp = this->now();
    control_cmd.longitudinal.stamp = this->now();
    control_cmd.longitudinal.speed = 0.0;
    control_cmd.longitudinal.acceleration = static_cast<float>(params_.target_acceleration);
    control_cmd.longitudinal.jerk = 0.0;
    control_cmd.lateral.stamp = this->now();
    control_cmd.lateral.steering_tire_angle = 0.0;
    control_cmd.lateral.steering_tire_rotation_rate = 0.0;
    return control_cmd;
  }

  control_cmd = prev_control_cmd;
  const auto dt = (this->now() - prev_control_cmd.stamp).seconds();

  control_cmd.stamp = this->now();
  control_cmd.longitudinal.stamp = this->now();
  control_cmd.longitudinal.speed = static_cast<float>(std::max(
    prev_control_cmd.longitudinal.speed + prev_control_cmd.longitudinal.acceleration * dt, 0.0));
  control_cmd.longitudinal.acceleration = static_cast<float>(std::max(
    prev_control_cmd.longitudinal.acceleration + params_.target_jerk * dt,
    params_.target_acceleration));
  if (prev_control_cmd.longitudinal.acceleration == params_.target_acceleration) {
    control_cmd.longitudinal.jerk = 0.0;
  } else {
    control_cmd.longitudinal.jerk = static_cast<float>(params_.target_jerk);
  }

  return control_cmd;
}

}  // namespace mrm_emergency_stop_operator

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(mrm_emergency_stop_operator::MrmEmergencyStopOperator)
