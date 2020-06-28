/*
This node is used to Bridge between DJI SDK and Gazebo.
The simulated drone pose in Gazebo is updated based on drone local position form DJI SDK.
To use this node, you need to connect to the dji drone and run the dji ros sdk node.
The brdige works for both DJI simulator and actual drone flights.
Developers:
  - Tareq ALqutami tareqaziz2010@gmail.com
*/

// ROS headers
#include <ros/ros.h>
#include <ros/console.h>

// ROS messages
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/QuaternionStamped.h>

// Gazebo messages
#include <gazebo_msgs/ModelState.h>
#include <gazebo_msgs/SetModelState.h>

#include <tf/transform_listener.h>

// DJI headers
#include <dji_sdk/SetLocalPosRef.h>

// c++ headers
#include <cmath>
#include <string>

// global variables for subscribed topics
geometry_msgs::Pose target_pose;
geometry_msgs::Twist target_twist;
geometry_msgs::Pose target_roll_pose;
geometry_msgs::Pose target_yaw_pose;
geometry_msgs::Pose target_pitch_pose;
gazebo_msgs::ModelState target_model_state;
gazebo_msgs::SetModelState set_model_state;

// variables for node arguments
std::string model_name = "dji_drone";
std::string reference_frame = "world";

// ros subscribers
ros::Subscriber attitude_subscriber;
ros::Subscriber velocity_subscriber;
ros::Subscriber local_position_subscriber;

// ros services
ros::ServiceClient model_state_client;
ros::ServiceClient set_local_pos_reference;

//=============================== Callbacks ===============================
// callback for /dji_sdk/attitude topic
void attitudeCallback(const geometry_msgs::QuaternionStamped::ConstPtr& attitude_msg)
{
  target_pose.orientation.w = -attitude_msg->quaternion.w;
  target_pose.orientation.y = attitude_msg->quaternion.x;
  target_pose.orientation.x = attitude_msg->quaternion.y;
  target_pose.orientation.z = attitude_msg->quaternion.z;

  target_twist.angular.x = 0;
  target_twist.angular.y = 0;
  target_twist.angular.z = 0;
}

// callback for /dji_sdk/velocity topic
void velocityCallback(const geometry_msgs::Vector3Stamped::ConstPtr& velocity_msg)
{
  target_twist.linear.x  = velocity_msg->vector.y;
  target_twist.linear.y  = velocity_msg->vector.x;
  target_twist.linear.z  = velocity_msg->vector.z;
}

// callback for /dji_sdk/local_position topic
void localPositionCallback(const geometry_msgs::PointStamped::ConstPtr& position_msg)
{
  // convert drone frame from ENU >> to XYZ. 0 degree Yaw points to North (positive X)
  target_pose.position.x =  position_msg->point.y;
  target_pose.position.y =  position_msg->point.x;
  target_pose.position.z =  position_msg->point.z;
}
//=========================================================================


bool set_local_position()
{
  dji_sdk::SetLocalPosRef localPosReferenceSetter;
  set_local_pos_reference.call(localPosReferenceSetter);
  return (bool) localPosReferenceSetter.response.result;
}



//=============================== main function ===============================
int main(int argc, char **argv)
{
  /***
    dji_gazebo_bridge Node
    arguments:
      drone_model (optional): the name of the gazebo model to bridge dji_sdk to.
                              It should be the drone model name (default: dji_drone)
      reference_frame (optional): the name of the reference frame (default: world)
      set_local_pose (optional): true/false wether to reset the drone local position
                                 it should be true unless there is another node
                                 that will set the initial local pos reference
  ***/

  //Initialize the node
  ros::init(argc, argv, "dji_gazebo_bridge");
  ros::NodeHandle n; //create a node handle.
  ros::NodeHandle pnh("~"); //create a private node handle.
  ros::spinOnce();

  // wait forever until gazebo services are available
  ros::service::waitForService("/gazebo/spawn_urdf_model",-1);

  // update parameter values from node arguments if any
  if(pnh.hasParam("drone_model")){
    // update drone gazebo model
    pnh.getParam("drone_model",model_name);
    ROS_INFO("Gazebo drone model is set to %s",model_name.c_str());
  }
  if(pnh.hasParam("reference_frame")){
    // update reference frame (default to world)
    pnh.getParam("reference_frame",reference_frame);
    ROS_INFO("drone reference frame is set to %s",reference_frame.c_str());
  }

  bool set_local_pose = 1;
  if(pnh.hasParam("set_local_pose")){
    // update the enable for setting local position using dji sdk
    pnh.getParam("set_local_pose",set_local_pose);
    ROS_INFO("set_local_pose is set to %i",set_local_pose);
  }

  // Wait for tf to publish drone model transforms, this will mean we can Start
  // brdiging. The transforms are performed by message_to_tf or any localization node
  tf::TransformListener listener;
  try{
    tf::StampedTransform transform;
    ros::spinOnce();
    listener.waitForTransform(model_name+"_base_link","led_link",ros::Time::now(),ros::Duration(100.0));
    ROS_INFO("TF transforms ready");
  }
  catch (tf::TransformException ex){
    ROS_ERROR("%s",ex.what());
  }

  // required services from gazebo and dji_sdk
  model_state_client        = n.serviceClient<gazebo_msgs::SetModelState>("/gazebo/set_model_state", true);
  set_local_pos_reference   = n.serviceClient<dji_sdk::SetLocalPosRef>("dji_sdk/set_local_pos_ref");
  model_state_client.waitForExistence();

  // Subscribe to topics from dji_sdk
  attitude_subscriber        = n.subscribe("/dji_sdk/attitude", 10, attitudeCallback);
  velocity_subscriber        = n.subscribe("/dji_sdk/velocity", 10, velocityCallback);
  local_position_subscriber  = n.subscribe("/dji_sdk/local_position", 10, localPositionCallback);

  ros::Rate spin_rate(50);

  // if enabled >> set reset local position refernce to current gps position
  if (set_local_pose){
    set_local_pos_reference.waitForExistence();
    if (!set_local_position())
    {
      ROS_ERROR("GPS health insufficient - No local frame reference for height. Exiting.");
      return 1;
    }
    else{
      ROS_INFO("Local reference was set.");
    }
  }

  ROS_INFO("Bridge between DJI SDK and gazebo is established");

  while(ros::ok())
  {
    ros::spinOnce();
    if(model_state_client.exists())
    {
      target_model_state.model_name = model_name;
      target_model_state.reference_frame = reference_frame;
      target_model_state.pose = target_pose;
      target_model_state.twist = target_twist;
      set_model_state.request.model_state = target_model_state;

      if(!model_state_client.call(set_model_state))
      {
        ROS_ERROR("Could not update %s model states",model_name.c_str());
        ROS_ERROR("status: %i, message %s",set_model_state.response.success,set_model_state.response.status_message.c_str());
      }
    }
    else
    {
      ROS_ERROR(" /gazebo/set_model_state service is not available... Existing");
      return 1;
    }

    spin_rate.sleep();
  }

  return 0;
}
//============================ End of main ===================================
