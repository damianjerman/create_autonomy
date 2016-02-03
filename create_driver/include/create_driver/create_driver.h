#ifndef CREATE_AUTONOMY_CREATE_DRIVER_H
#define CREATE_AUTONOMY_CREATE_DRIVER_H

#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include "create/create.h"

class CreateDriver {
  private:
    create::Create* robot;
    nav_msgs::Odometry odom;
    ros::Time lastCmdVelTime;
    double loopHz;
    std::string dev;
    int baud;
    double latchDuration;

    void cmdVelCallback(const geometry_msgs::TwistConstPtr& msg);
    bool update();
    void publishOdom();
    
  protected:
    ros::NodeHandle nh;
    ros::NodeHandle priv_nh;
    ros::Subscriber cmdVelSub;
    ros::Publisher odomPub;

  public:
    CreateDriver(ros::NodeHandle& nh_);
    ~CreateDriver();
    virtual void spin();
    virtual void spinOnce();
}; // class CreateDriver

#endif // CREATE_AUTONOMY_CREATE_DRIVER_H