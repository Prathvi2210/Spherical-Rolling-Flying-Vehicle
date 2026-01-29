Many mistakes happen when installing the SDK and ROS wrapper for Intel Realsense. 
In my case it was a version mismatch, the SDk and ROS wrapper both contain their own version of librealsense.
My specific error started when I installed 'ros-humble-librealsense2' while I already had a source built librealsense with USB access on jetson.
This specific action created two competing owners of the same hardware stack causing camera not being recognized in the realsense-viewer and many other errors such as the 'bad optionaal access'
ros-humble-realsense2 installs librealsense binaries with its own udev rules, own HID expectations and is not built for jetson-specific behaviour.
This causes confusion in the system often requiring clean slate to proceed again with different approach.
Cleaning the system is especially hard for source build which are mostly the case in version mismatch cases.
This set of instructions is to delete all traces on intel software from the device and go back to a system with just ROS installed:
Phase 1- Remove ROS realsense wrapper completely
```bash
cd ~/ros2_ws/src
rm -rf realsense-ros
```
Remove all workspace build artifacts
```bash
cd ~/ros2_ws
rm -rf build install log
```
Remove any binary installed ROS Realsense Package
```bash
apt list --installed | grep realsense
```
If output comes, something like
```bash
ros-humble-realsense2-camera
```
remove it
```bash
sudo apt purge ros-humble-realsense2-camera -y
sudo apt purge ros-humble-realsense2 -y
sudo apt autoremove -y
```
Phase 2- Remove librealsense (full system clean)
Remove installed libraries and headers
```bash
sudo rm -rf /usr/local/lib/librealsense*
sudo rm -rf /usr/local/include/librealsense2
sudo rm -rf /usr/local/bin/realsense-*
sudo rm -rf /usr/local/bin/rs-*
```
Update linker cache
```
sudo Idconfig
```
Remove librealsense source tree (optional)
```bash
rm -rf ~/librealsense
```
Phase 3- Remove Realsense UDEV rules (Important)
```bash
sudo rm -f /etc/udev/rules.d/99-realsense*
```
Reload udev
```bash
sudo udevadm control --reload rules
sudo udevadm trigger
```

Phase 4- Clean User environment
Reset shell environment completely
Close the terminal or run:
```bash
exec bash
```
Source only ROS 2 Humble:
```bash
source /opt/ros/humble/setup.bash
```

Verify environment is clean
```bash
ros2 pkg list | grep realsense
apt list --installed | grep realsense

which realsense-viewer

rs-enumerate-devices
```
Expected: no output for all and enumerate command not found

Phase 5-Reboot

To avoid similar problems in reinstallation stick to only one path:
1) ROS binaries only- No source build, no custom udev rules
2) Source build only(recommended)- Build librealsense from source then build ROS wrapper from source. NEVER install ros-humble-librealsense2
