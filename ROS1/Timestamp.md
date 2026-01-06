After launching the RTABMap algorithm in ROS1 environment the data not recieved error kept appearing

```bash
/rtabmap/rtabmap subscribed to (approx sync):
 /rtabmap/odom \
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info \
 /rtabmap/odom_info
 [INFO] [1758792595.935238529]: rtabmap 0.21.13 started...
 [WARN] [1758792598.468196102]: /rtabmap/rgbd_odometry: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 /rtabmap/rgbd_odometry subscribed to (approx sync):
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info
 [WARN] [1758792599.171975402]: /rtabmap/rtabmap: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 If topics are coming from different computers, make sure the clocks of the computers are synchronized ("ntpdate").
 If topics are not published at the same rate, you could increase "sync_queue_size" and/or "topic_queue_size" parameters (current=10 and 1 respectively).
 /rtabmap/rtabmap subscribed to (approx sync):
 /rtabmap/odom \
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info \
 /rtabmap/odom_info
 [WARN] [1758792604.468340331]: /rtabmap/rgbd_odometry: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 /rtabmap/rgbd_odometry subscribed to (approx sync):
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info
 [WARN] [1758792605.172153178]: /rtabmap/rtabmap: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 If topics are coming from different computers, make sure the clocks of the computers are synchronized ("ntpdate").
 If topics are not published at the same rate, you could increase "sync_queue_size" and/or "topic_queue_size" parameters (current=10 and 1 respectively).
 /rtabmap/rtabmap subscribed to (approx sync):
 /rtabmap/odom \
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info \
 /rtabmap/odom_info
 [WARN] [1758792610.468063587]: /rtabmap/rgbd_odometry: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 /rtabmap/rgbd_odometry subscribed to (approx sync):
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info
 [WARN] [1758792611.172329284]: /rtabmap/rtabmap: Did not receive data since 5 seconds! Make sure the input topics are published ("$ rostopic hz my_topic") and the timestamps in their header are set.
 If topics are coming from different computers, make sure the clocks of the computers are synchronized ("ntpdate").
 If topics are not published at the same rate, you could increase "sync_queue_size" and/or "topic_queue_size" parameters (current=10 and 1 respectively).
 /rtabmap/rtabmap subscribed to (approx sync):
 /rtabmap/odom\
 /camera/rgb/image_rect_color \
 /camera1/depth_raw \
 /camera1/depth_info \
 /rtabmap/odom_info
 ^C[rtabmap/rtabmap-2] killing on exit [rtabmap/rgbd_odometry-1] killing on exit rtabmap: Saving database/long-term memory...
 (located at /home/srfv/.ros/rtabmap.db) rtabmap: Saving database/long-term memory...done!
 (located at /home/srfv/.ros/rtabmap.db, 0 MB) shutting down processing monitor... ... shutting down processing monitor complete done
```

Once diagnosed as a timestamp issue due to the trouble shooting and warnings given in the error itself

Initial assessment: rgbd_odometry and rtabmap nodes are running but not receiving the sensor messages they subscribed to.
TF frames (depth_camera_link, odom, map) are configured.
Parameters are set properly (subscribe_depth:=true, subscribe_rgb:=false).
Indicating an issue with sensor data not the RTAB ros wrapper

Checking timestamps:
```bash
srfv@ubuntu:~$ rostopic echo /camera1/depth_info/header
 seq: 31471 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31472 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31473 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31474 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31475 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31476 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31477 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link" ---
 seq: 31478 stamp: secs: 0 nsecs: 0 frame_id: "depth_camera_link"
```
This continued for a long time.
The /camera1/depth_info topic is publishing, but the header.stamp is always zero (secs: 0, nsecs: 0).
RTAB-Map (and most ROS packages) use that timestamp to synchronize depth, RGB/IR, and camera_info messages.
Since the timestamp is invalid, the message synchronizer never finds a match, so RTAB-Map just keeps waiting → Did not receive data since 5 seconds!.

Need to fix this issue in the camera driver.
Locate the camera timestamp publisher.
In the synexens_ros1/src directory.
```bash
grep -R "CameraInfo" src
% MY OUTPUT
 src/SYRosDevice.cpp: mapRosPublisher.insert(std::pair<PUBLISHER_TYPE, ros::Publisher>(DEPTH, m_node.advertise<sensor_msgs::CameraInfo>(sTopicTempName.c_str(), 1)));
 src/SYRosDevice.cpp: mapRosPublisher.insert(std::pair<PUBLISHER_TYPE, ros::Publisher>(IR, m_node.advertise<sensor_msgs::CameraInfo>(sTopicTempName.c_str(), 1)));
 src/SYRosDevice.cpp: // mapRosPublisher.insert(std::pair<PUBLISHER_TYPE, ros::Publisher>(RGB, m_node.advertise<sensor_msgs::CameraInfo>(sTopicTempName.c_str(), 1)));
 src/SYRosDevice.cpp: m_calibrationData.getDepthCameraInfo(m_depthCameraInfo, &intrinsics); src/SYRosDevice.cpp: m_mapRosPublisher[nDeviceID][DEPTH].publish(m_depthCameraInfo);
 src/SYRosDevice.cpp: m_calibrationData.getDepthCameraInfo(m_depthCameraInfo, &intrinsics); src/SYRosDevice.cpp: m_mapRosPublisher[nDeviceID][DEPTH].publish(m_depthCameraInfo);
 src/SYRosDevice.cpp: m_calibrationData.getDepthCameraInfo(m_irCameraInfo, &intrinsics); src/SYRosDevice.cpp: m_mapRosPublisher[nDeviceID][IR].publish(m_irCameraInfo);
 src/SYCalibrationTransformData.cpp:void SYCalibrationTransformData::setCameraInfo(const SYIntrinsics &parameters, sensor_msgs::CameraInfo &camera_info)
 src/SYCalibrationTransformData.cpp:void SYCalibrationTransformData::getDepthCameraInfo(sensor_msgs::CameraInfo &camera_info, SYIntrinsics *intrinsics)
 src/SYCalibrationTransformData.cpp: setCameraInfo(intrinsics ? *intrinsics : m_depthCameraIntrinsics, camera_info);
 src/SYCalibrationTransformData.cpp:void SYCalibrationTransformData::getRgbCameraInfo(sensor_msgs::CameraInfo &camera_info, SYIntrinsics *intrinsics)
 src/SYCalibrationTransformData.cpp: setCameraInfo(intrinsics ? *intrinsics : m_rgbCameraIntrinsics, camera_info);
```
Important lines are those with DepthCameraInfo term
Open the SYRosDevice.cpp and add the following before the publishing code
```bash
nano src/SYRosDevice.cpp
# Find the section where m_mapRosPublisher[nDeviceID][DEPTH].publish(m_depthCameraInfo); is called.
# Just before publishing, set the timestamp:
m_depthCameraInfo.header.stamp = ros::Time::now();
m_irCameraInfo.header.stamp    = ros::Time::now();
```
For example, my code had m_depthCameraInfo.header.stamp not set so it was defaulting to zero
So the error was: depth image is fine, CameraInfo messages should copy the timestamp from image. RGB feed also needs a non-empty header
after the editing it becomes:
```bash
// Fix for depth CameraInfo
m_calibrationData.getDepthCameraInfo(m_depthCameraInfo, &intrinsics);
m_depthCameraInfo.header.stamp = depthImage->header.stamp;   // match image timestamp
m_depthCameraInfo.header.frame_id = depthImage->header.frame_id;
m_mapRosPublisher[nDeviceID][DEPTH].publish(m_depthCameraInfo);

// Fix for RGB image
std_msgs::Header rgb_header;
rgb_header.stamp = ros::Time::now();  // or capture_time if available
rgb_header.frame_id = "rgb_camera_link";

sensor_msgs::ImagePtr rgbImage = cv_bridge::CvImage(
    rgb_header,
    sensor_msgs::image_encodings::BGR8,
    bgrImgRGB
).toImageMsg();
m_mapImagePublisher[nDeviceID][RGB].publish(rgbImage);
```
After editing rebuild the packages in the workspace
Ensure the synexens_ros1 package is in the catkin_ws/src
synexens_ros1 is just the package source, not a full catkin workspace
```bash
cd ~/synexens_ros1
catkin_make
source devel/setup.bash
```
and check for timestamps

If above soultion is not working the edited driver is not in the ROS_PACKAGE_PATH. ROS maybe still picking up the old installed package from the system ROS_PACKAGE_PATH
Solution 1: Build the driver in a workspace and add it to ROS_PACKAGE_PATH
```bash
echo $ROS_PACKAGE_PATH
```
should give an output consisting of the catkin_ws, for ex:
```bash
/home/srfv/synexens_ros1/src:/home/srfv/catkin_ws/src:/opt/ros/noetic/share
```
Solution 2: Copy your edited driver into ~/catkin_ws/src
copy, clean, rebuild (catkin_make), source

If still not working, the error maybe in time capturing.
In the driver.cpp file
```bash
depthImage->header.stamp = capture_time;
```
Here if the capture_time is zero all messages have stamp: 0
So we need to initialize the capture_time
In the code you can see something like:
```bash
ros::Time capture_time = ros::Time::now();
```
This is where capture_time is assigned
If this is what you see, edit the previous line as follows:
```bash
depthImage->header.stamp = capture_time;
# changes to
depthImage->header.stamp = ros::Time::now();
```
Same goes for RGB image
