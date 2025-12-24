This file is for instructions to start using the Synexens CS20 Solid State ToF Camera in ROS2 environment.
The system I had used is Arm64 -RPi 4 model B (8GB) running Ubuntu 22.04 Focal Release with ROS2 Humble.

All official drivers, demos, installation instructions and sdk available at: https://support.tofsensors.com/resource/sdk/ros2.html
Now the supported versions are:
ROS version support: Foxy Galactic Humble
Support system: ubuntu20.04_x86 ubuntu18.04_x86 ubuntu22.04_x86

Issue: ROS demo is made for SDKs with x86 architecture, we need to surgically replace SDK headers + shared libs.
The ROS demo is a compressed folder that contains ROS2 wrapper + x86 SDK by default

Download the arm SDK separately, get latest version: SynexensSDK_4.2.4.0_armv8.tar

Extract the SDK"
```bash
cd ~/Downloads
tar -xvf SynexensSDK_4.2.4.0_armv8.tar
```
Prepare the ROS workspace
```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
```
Download the ROS demo compressed file here and then extract it
```bash
tar -xvf synexens_ros2.tar.gz
```
NOTE: The downloaded SDK should be compatible with the ROS demo, mentioned in its description on the website.
The replacement target files are at synexens_ros2/ext/sdk/
Inside it the directory is 
include/
lib/
opencv/

Replace SDK Headers
```bash
cp -r ~/Downloads/SynexensSDK_4.2.4.0_armv8/include/* \
   ~/ros2_ws/src/synexens_ros2/ext/sdk/include/
```
Replace SDK core libraries
```bash
cp -r ~/Downloads/SynexensSDK_4.2.4.0_armv8/lib/*.so \
   ~/ros2_ws/src/synexens_ros2/ext/sdk/lib/
```
Replace OpenCV SDK Binaries
```bash
cp -r ~/Downloads/SynexensSDK_4.2.4.0_armv8/thirdpart/opencv440/lib/*.so \
   ~/ros2_ws/src/synexens_ros2/ext/sdk/opencv/
```
camera_info_manager is a standard ROS2 package that the Synexens ROS2 demo depends on, but is not installed by default in many ROS2 Humble installations (especially minimal ones).
```bash
sudo apt update
sudo apt install ros-humble-camera-info-manager
```
Build with colcon
```bash
cd ~/ros2_ws
colcon build
```

Now after the build is complete devel/lib/*.so needs to be replaced at runtime
```bash
cp ~/Downloads/SynexensSDK_4.2.4.0_armv8/lib/*.so \
   ~/ros2_ws/install/synexens_ros2/lib/
```
Check: file type of setup file
```bash
file setup.sh
```
Expected output: Bourne-Again shell script, ASCII text executable
Problem output: ASCII text, with CRLF line terminators

If output as expected:
USB permissions to detect the sensor
```bash
cd ~/ros2_ws/src/synexens_ros2/script
sudo bash setup.sh
```

If you get the problem output
```bash
sudo apt install -y dos2unix
dos2unix setup.sh
chmod +x setup.sh
sudo ./setup.sh
```

Source and Run
```bash
source ~/ros2_ws/install/setup.bash
ros2 launch synexens_ros2 driver_launch.py
#or
ros2 launch synexens_ros2 viewer_launch.py
#to start with rviz visualization
```
Visualizing in Rviz:
Fixed Frame = camera_link   (or cs20_link)
Add displays: /camera/depth_raw
/camera/points2

Checking TF trees sanity (needed for SLAM applications)
```bash
ros2 run tf2_tools view_frames
evince frames.pdf
```
