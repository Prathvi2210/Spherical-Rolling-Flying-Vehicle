Incase of any previous failed builds, clean the space, remove the directory
Also verify no ROS owned sdk path from previous installs
```bash
which realsense-viewer
```
Correct output: /usr/local/bin/realsense-viewer.
Wrong output: /opt/ros/humble. remove the packages

Create a workspace and clone the repo
```bash
cd ~/ros2_ws/src
git clone https://github.com/IntelRealSense/realsense-ros.git
cd realsense-ros
git checkout 4.56.4
source /opt/ros/humble/setup.bash
colcon build --symlink-install
```
Avoid using git clone https://github.com/IntelRealSense/realsense-ros.git -b ros2-development
ros2-development is a moving target. It tracks the latest librealsense, not a secure checkedout version
Never install ros-*-realsense2-* via apt on Jetson

Verify correct linking:
```bash
ldd install/realsense2_camera/lib/realsense2_camera/realsense2_camera_node | grep librealsense
```
Expected output: /usr/local/lib/librealsense2.so
Source only your ws do not resource opt/ros/humble/setup.bash after this:
Launch with IMU disabled. Many jetson US controllers break HID endpoints, causing device creation to fail
```bash
source ~/ros2_ws/install/setup.bash
ros2 launch realsense2_camera rs_launch.py enable_gyro:=false enable_accel:=false
```
