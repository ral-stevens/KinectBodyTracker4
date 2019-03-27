#pragma once
#include <stdio.h>
#include "Config.h"

class RosPublisher
{
public:
	RosPublisher();
	~RosPublisher();
	void publish(float p[2]);
private:
	ros::NodeHandle nh;
	geometry_msgs::Twist    twist_msg;
	ros::Publisher          cmd_vel_pub;
	char*                   m_pszRosMaster;

};

