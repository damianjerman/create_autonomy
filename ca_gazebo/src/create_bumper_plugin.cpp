/*
 *  Gazebo - Outdoor Multi-Robot Simulator
 *  Copyright (C) 2003
 *     Nate Koenig & Andrew Howard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/*
 * Desc: Bumper controller
 * Author: Nate Koenig
 * Date: 09 Setp. 2008
 */

#include "ca_gazebo/create_bumper_plugin.h"

namespace gazebo
{

CreateBumperPlugin::CreateBumperPlugin()
: SensorPlugin()
, contact_connect_count_(0)
, bumper_left_was_pressed_(false)
, bumper_center_was_pressed_(false)
, bumper_right_was_pressed_(false)
{
}

CreateBumperPlugin::~CreateBumperPlugin()
{
  this->rosnode_.reset();
  this->callback_queue_thread_.join();
  this->update_connection_.reset();
}

void CreateBumperPlugin::Load(sensors::SensorPtr _parent, sdf::ElementPtr _sdf)
{
    ROS_INFO_STREAM("Loading gazebo bumper");
    
    this->bumper_ =
      std::dynamic_pointer_cast<sensors::ContactSensor>(_parent);
    if (!this->bumper_)
    {
        gzerr << "ContactPlugin requires a ContactSensor.\n";
        return;
    }
    
    this->robot_namespace_ = "";
    if (_sdf->HasElement("robotNamespace"))
        this->robot_namespace_ = _sdf->Get<std::string>("robotNamespace") + "/";
    
    // "publishing contact/collisions to this topic name: " << this->bumper_topic_name_ << std::endl;
    this->bumper_topic_name_ = "bumper_base";
    if (_sdf->HasElement("bumperTopicName"))
        this->bumper_topic_name_ = _sdf->Get<std::string>("bumperTopicName");
    
    // "transform contact/collisions pose, forces to this body (link) name: " << this->frame_name_ << std::endl;
    if (!_sdf->HasElement("frameName"))
    {
        ROS_INFO("bumper plugin missing <frameName>, defaults to world");
        this->frame_name_ = "world";
    }
    else
        this->frame_name_ = _sdf->Get<std::string>("frameName");
    
    this->bumper_event_.header.frame_id = this->frame_name_;
    
    ROS_INFO("Loaded with values:   robotNamespace = %s, bumperTopicName = %s, frameName = %s",
             this->robot_namespace_.c_str(), this->bumper_topic_name_.c_str(),this->frame_name_.c_str());
    
    if (!ros::isInitialized())
    {
        int argc = 0;
        char** argv = NULL;
        ros::init(argc,argv,"bumper_node",ros::init_options::AnonymousName);
    }
    
    this->rosnode_.reset(new ros::NodeHandle(this->robot_namespace_));
    
    // resolve tf prefix
    std::string prefix;
    this->rosnode_->getParam(std::string("tf_prefix"), prefix);
    this->frame_name_ = tf::resolve(prefix, this->frame_name_);
    
    ros::AdvertiseOptions ao =
        ros::AdvertiseOptions::create<ca_msgs::Bumper>(
            std::string(this->bumper_topic_name_),1,
            boost::bind(&CreateBumperPlugin::ContactConnect, this),
            boost::bind(&CreateBumperPlugin::ContactDisconnect, this),
            ros::VoidPtr(), &this->contact_queue_);
    this->contact_pub_ = this->rosnode_->advertise(ao);

    this->gts_sub_ = this->rosnode_->subscribe("gts", 1, &CreateBumperPlugin::GtsCb, this);
    
    this->callback_queue_thread_ = boost::thread(boost::bind( &CreateBumperPlugin::ContactQueueThread,this ) );
    
    // Listen to the update event. This event is broadcast every
    // simulation iteration.
    this->update_connection_ = this->bumper_->ConnectUpdated(
      boost::bind(&CreateBumperPlugin::OnUpdate, this));
    
    // Make sure the parent sensor is active.
    this->bumper_->SetActive(true);
    
    ROS_INFO("bumper loaded");
}

void CreateBumperPlugin::ContactConnect()
{
  this->contact_connect_count_++;
}

void CreateBumperPlugin::ContactDisconnect()
{
  this->contact_connect_count_--;
}

void CreateBumperPlugin::OnUpdate()
{
  if (this->contact_connect_count_ <= 0)
    return;

  // reset flags
  this->bumper_left_is_pressed_ = false;
  this->bumper_center_is_pressed_ = false;
  this->bumper_right_is_pressed_ = false;
  
  // Get all the contacts.
  msgs::Contacts contacts;
  contacts = this->bumper_->Contacts();

  // https://github.com/yujinrobot/kobuki_desktop/blob/devel/kobuki_gazebo_plugins/src/gazebo_ros_kobuki_updates.cpp#L313
  // https://github.com/pal-robotics-graveyard/reem_msgs/blob/master/msg/Bumper.msg
  // https://github.com/yujinrobot/kobuki_msgs/blob/devel/msg/BumperEvent.msg

  for (int i = 0; i < contacts.contact_size(); ++i)
  {
    // using the force normals below, since the contact position is given in world coordinates
    // also negating the normal, because it points from contact to robot centre
    const double global_contact_angle = std::atan2(-contacts.contact(i).normal(0).y(), -contacts.contact(i).normal(0).x());
    const double relative_contact_angle = global_contact_angle - this->robot_heading_;

    if ((relative_contact_angle <= (M_PI/2)) && (relative_contact_angle > (M_PI/6)))
    {
      this->bumper_left_is_pressed_ = true;
    }
    else if ((relative_contact_angle <= (M_PI/6)) && (relative_contact_angle >= (-M_PI/6)))
    {
      this->bumper_center_is_pressed_ = true;
    }
    else if ((relative_contact_angle < (-M_PI/6)) && (relative_contact_angle >= (-M_PI/2)))
    {
      this->bumper_right_is_pressed_ = true;
    }
  }

  // check for bumper state change
  if (bumper_left_is_pressed_ && !bumper_left_was_pressed_)
  {
    bumper_left_was_pressed_ = true;
    this->bumper_event_.is_left_pressed = ca_msgs::Bumper::PRESSED;
    PublishBumperMsg();
  }
  else if (!bumper_left_is_pressed_ && bumper_left_was_pressed_)
  {
    bumper_left_was_pressed_ = false;
    this->bumper_event_.is_left_pressed = ca_msgs::Bumper::RELEASED;
    PublishBumperMsg();
  }
  if (bumper_center_is_pressed_ && !bumper_center_was_pressed_)
  {
    bumper_center_was_pressed_ = true;
    this->bumper_event_.is_left_pressed = ca_msgs::Bumper::PRESSED;
    this->bumper_event_.is_right_pressed = ca_msgs::Bumper::PRESSED;
    PublishBumperMsg();
  }
  else if (!bumper_center_is_pressed_ && bumper_center_was_pressed_)
  {
    bumper_center_was_pressed_ = false;
    this->bumper_event_.is_left_pressed = ca_msgs::Bumper::RELEASED;
    this->bumper_event_.is_right_pressed = ca_msgs::Bumper::RELEASED;
    PublishBumperMsg();
  }
  if (bumper_right_is_pressed_ && !bumper_right_was_pressed_)
  {
    bumper_right_was_pressed_ = true;
    this->bumper_event_.is_right_pressed = ca_msgs::Bumper::PRESSED;
    PublishBumperMsg();
  }
  else if (!bumper_right_is_pressed_ && bumper_right_was_pressed_)
  {
    bumper_right_was_pressed_ = false;
    this->bumper_event_.is_right_pressed = ca_msgs::Bumper::RELEASED;
    PublishBumperMsg();
  }
}

void CreateBumperPlugin::PublishBumperMsg()
{
  this->bumper_event_.header.seq++;
  this->bumper_event_.header.stamp = ros::Time::now();
  this->contact_pub_.publish(this->bumper_event_);
}

void CreateBumperPlugin::GtsCb(const nav_msgs::Odometry::ConstPtr& msg)
{
  // Get current robot heading (yaw angle)
  const tf::Quaternion q(
      msg->pose.pose.orientation.x,
      msg->pose.pose.orientation.y,
      msg->pose.pose.orientation.z,
      msg->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch;
  m.getRPY(roll, pitch, this->robot_heading_);
}

void CreateBumperPlugin::ContactQueueThread()
{
  static const double timeout = 0.01;
  
  while (this->rosnode_->ok())
  {
    this->contact_queue_.callAvailable(ros::WallDuration(timeout));
  }
}

// Register this plugin with the simulator
GZ_REGISTER_SENSOR_PLUGIN(CreateBumperPlugin);

}
