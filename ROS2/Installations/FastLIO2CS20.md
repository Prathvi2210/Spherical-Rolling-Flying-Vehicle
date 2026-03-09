FAST-LIO2 ROS2 instructions:
#----------------------------------------------------
```bash
sudo apt-get install libpcl-dev
```
install ros2 following the ros2_common.sh
inside ros workspace/src
```bash
mkdir -p ~/rosWS/src
```
Install PCL ros
Ubuntu, install from apt
```bash
sudo apt-get install ros-humble-pcl-conversions
sudo apt-get install ros-humble-pcl-ros
```
Raspberry pi, build from source
```bash
cd ~/rosWS/src
git clone --branch ros2 --depth 1 https://github.com/ros-perception/pcl_msgs.git
git clone --branch ros2 --depth 1 https://github.com/ros-perception/perception_pcl.git
```

FastLio ROS2- Choose
```bash
#git clone --depth 1 -b ROS2 https://github.com/hku-mars/FAST_LIO.git  --recursive
git clone --depth 1 https://github.com/Ericsii/FAST_LIO_ROS2.git --recursive
```
modify all c++14 to c++17- If the repo needs it, check first
```bash
nano FAST_LIO_ROS2/CMakeLists.txt
```

```bash
cd ~/rosWS
rosdep install \
  --from-paths src \
  --ignore-src \
  --skip-keys="livox_ros_driver2 OpenCV" \
  -y
```
FAST_LIO_ROS2 still tries to find_package(livox_ros_driver2) at CMake time. We need to disable the dependency.
```bash
cd ~/ros2_ws/src/FAST_LIO_ROS2
nano CMakeLists.txt
```
locate: find_package(livox_ros_driver2 REQUIRED)
Comment it out
Look for ament_target_dependencies(....).
Remove the livox_ros_driver2 from dependencies

Add a compile definition
Locate this line near the top
```bash
add_definitions(-DROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/\")
```
Add this line below
```bash
add_definitions(-DNO_LIVOX)
```
This gives us a macro we can use to disable Livox Code

Guard Livox includes in source files
Edit src/preprocess.h
```bash
nano ~/ros2_ws/src/FAST_LIO_ROS2/src/preprocess.h
```
Find this line:
```C++
#include <livox_ros_driver2/msg/custom_msg.hpp>
```
Replace with:
```C++
#ifndef NO_LIVOX
#include <livox_ros_driver2/msg/custom_msg.hpp>
#endif
```
Edit src/laserMapping.cpp
```bash
nano ~/ros2_ws/src/FAST_LIO_ROS2/src/laserMapping.cpp
```
Find:
```C++
#include <livox_ros_driver2/msg/custom_msg.hpp>
```
Replace with:
```C++
#ifndef NO_LIVOX
#include <livox_ros_driver2/msg/custom_msg.hpp>
#endif
```
Edit src/preprocess.cpp if present. Do the same if the include appears there.

Ensure the PointCloud2 path is active. Search in preprocess.cpp for livox callbacks like:
```C++
void Preprocess::process(const livox_ros_driver2::msg::CustomMsg::SharedPtr& msg)
```
There will also be another overload like:
```C++
void Preprocess::process(const sensor_msgs::msg::PointCloud2::SharedPtr& msg)
```
just ensure compilation skips Livox types (NO_LIVOX)

FastLIO2 supports two LiDAR modes: Livox mode and  Generic LiDAR mode (uses sensor_msgs::msg::PointCloud2)
It is important to compile only the generic pointcloud2 path
Introduce a compile flag: NO_LIVOX. Completely exclude Livox: function declarations, callbacks, subscriptions, includes
In CMakeLists.txt, make sure this exists
```cmake
add_definitions(-DNO_LIVOX)
```
Now fix preprocess.h
After guarding Livox include like mentioned above, now we need to guard the Livox function dependencies:
Find these lines
```bash
void process(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg, PointCloudXYZI::Ptr &pcl_out);
void avia_handler(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg);
```
Wrap them up like this
```bash
#ifndef NO_LIVOX
void process(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg, PointCloudXYZI::Ptr &pcl_out);
void avia_handler(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg);
#endif
```

Fix ros2_ws/src/FAST_LIO_ROS2_/src/preprocess.cpp
Find these functions definitions
```C++
void Preprocess::process(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg, PointCloudXYZI::Ptr& pcl_out)
```
and
```bash
void Preprocess::avia_handler(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg)
```
Wrap each entire function like this:
```bash
#ifndef NO_LIVOX
void Preprocess::process(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg, PointCloudXYZI::Ptr& pcl_out)
{
  ...
}

void Preprocess::avia_handler(const livox_ros_driver2::msg::CustomMsg::UniquePtr &msg)
{
  ...
}
#endif
```
Make sure the whole function is inside the #ifndef NO_LIVOX and #endif

Now, fix the laserMapping.cpp
1. Guard the Livox include
   Find
   ```C++
   #include <livox_ros_driver2/msg/custom_msg.hpp>
   ```
   Change to
   ```C++
   #ifndef NO_LIVOX
   #include <livox_ros_driver2/msg/custom_msg.hpp>
   #endif
   ```
2. Guard Livox callback
   Find:
   ```C++
   void livox_pcl_cbk(const livox_ros_driver2::msg::CustomMsg::UniquePtr msg)
   ```
   Wrap it up:
   ```C++
   #ifndef NO_LIVOX
   void livox_pcl_cbk(const livox_ros_driver2::msg::CustomMsg::UniquePtr msg)
   {
   ...
   }
   #endif
   ```
3. Guard Livox subscription member
   Find this member declaration
   ```C++
   rclcpp::Subscription<livox_ros_driver2::msg::CustomMsg>::SharedPtr sub_pcl_livox_;
   ```
   Wrap it:
   ```C++
   #ifndef NO_LIVOX
   rclcpp::Subscription<livox_ros_driver2::msg::CustomMsg>::SharedPtr sub_pcl_livox_;
   #endif
   ```
4. Guard Livox subscription creation
   Find this block (around constructor)
   ```C++
   sub_pcl_livox_ = this->create_subscription<livox_ros_driver2::msg::CustomMsg>(
       lid_topic, 20, livox_pcl_cbk);
   ```
   Wrap it:
   ```C++
   #ifndef NO_LIVOX
   sub_pcl_livox_ = this->create_subscription<livox_ros_driver2::msg::CustomMsg>(
       lid_topic, 20, livox_pcl_cbk);
   #endif
   ```
   
Optional: the file sets C++17 but then forces C++14 again later
Replace
```bash
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread -std=c++0x -std=c++14 -fexceptions")
```
with
```bash
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -fexceptions")
```
This ensures pure C++ which ROS2 Humble expects

Run a clean build
```bash
export MAKEFLAGS="-j2"
colcon build --symlink-install --parallel-workers 1 --cmake-clean-cache --cmake-args -DCMAKE_BUILD_TYPE=Release -DROS_EDITION=ROS2 -DHUMBLE_ROS=humble --event-handlers console_cohesion+
```

config and run
```bash
sudo chmod a+x install/setup.bash
source ./install/setup.bash
echo "source ~/ros_ws/install/setup.bash" >> ~/.bashrc
```
Check
```bash
ros2 pkg list | grep fast_lio
```
Expected: fast_lio2

Verify the node exists
```bash
ros2 run fast_lio fastlio_mapping --help
```
Usage info should be visible here

config and launch fast_lio with map_file_path, dense_publish_en: true
```bash
nano dev/rosWS/install/fast_lio/share/fast_lio/launch/mapping.launch.py 
```
First launch the LiDAR nodes in a terminal, then:
```bash
ros2 launch fast_lio mapping.launch.py config_file:=cs20.yaml
```
take snapshot of pcd
```bash
ros2 service call /map_save std_srvs/srv/Trigger {}>>>check this code instructions. Is it compatible entirely for ROS humble 
```

NOW
FAST-LIO2 assumes: 
  Solid State LiDAR (non-rotating)
  known FOV
  known max range
  Reasonable point density
  Correct IMU-LiDAR timing

The Synexens CS20 although a solid state device is very different from the Livox devices assumed in build. 
It is extremely sensitive to FoV, range and build-zone settings

No changes in CS20 driver parameters as long as: /points2 is valid, timestamps are correct, IMU is time-aligned
Changes are to be done in FAST-LIO2 side:
A) LiDAR FoV: FAST LIO2 needs horizontal FoV only. In FAST-LIO2 config (fast_lio2.yaml):
fov_deg:70.0
without this change the KD-tree search explodes, feature association degrades badly.

B) Maximum sensing range: 
Set conservatively.
max_range: 5.0
min_range: 0.2
Without this, ToF noise explodes near max range.
FAST-LIO2 assumes monotonic noise growth.

C) Blind Zone:
Critical for ToF sensors, they have a near-field invalid zone.
blind: 0.25
If blind zone is too small, EKF gets corrupted by near-field noise and the map flickers.

D) Point rate/ Downsampling:
CS20 produces dense point clouds compared to Livox.
Use aggressive downsampling.
point_filter_num: 3 or 4
This stabilizes Scan Matching, IMU propogation.

E) Extrinsics:
Need to define LiDAR->IMU transform.
Example: extrinsic_est_en: false
extrinsic_T: [0.0, 0.0, 0.0]
extrinsic_R: [1.0, 0.0, 0.0,
              0.0, 1.0, 0.0,
              0.0, 0.0, 1.0]

Topics to remap:
FAST-LIO2 expects /cloud_registered is not a topic in CS20:
/cloud remaps to /camera1/points2

Create a new config(Do not reuse the mid360.yaml)
```bash
cp install/fast_lio2/share/fast_lio2/config/mid360.yaml \
   install/fast_lio2/share/fast_lio2/config/cs20.yaml
```
Edit that file
```YAML
fov_deg: 70.0
max_range: 8.0
min_range: 0.2

lidar_type: 0
scan_line: 1
blind: 0.1
feature_extract_enable: false 
point_filter_num: 3

lidar_topic: /camera1/points2
imu_topic: /camera1/imu

extrinsic_est_en: false
```
CS20 publishes intensity but FAST-LIO never uses it, not harmful but it spams the terminal with failed to read intensity messages. feature_extract_enable: false stops the sensor from publishing intensity.
Launch
terminal 1
```bash
ros2 launch synexens_cs20 cs20.launch.py
```
terminal 2
```bash
ros2 launch fast_lio2 mapping.launch.py config_file:=cs20.yaml
```
/imu remaps to /camera1/imu

These remappings are done in the launch file
