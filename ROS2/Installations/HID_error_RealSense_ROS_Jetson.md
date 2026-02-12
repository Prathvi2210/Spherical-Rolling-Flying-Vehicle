Even after correct installations the realsense ROS launch for Realsense camera may fail with IMU functionality
The error is of the form:
```bash
ros2 launch realsense2_camera rs_launch.py
>>>[INFO] [launch]:Default logging verbosity is set to INFO 
[INFO] [launch.user]: 🚀 Launching as Normal ROS Node 
[INFO] [realsense2_camera_node-1]: process started with pid [34860] 
[realsense2_camera_node-1] [INFO] 
[1768738381.523154451] [camera.camera]: RealSense ROS v4.56.4 
[realsense2_camera_node-1] [INFO] 
[1768738381.523654200] [camera.camera]: Built with LibRealSense v2.56.4 
[realsense2_camera_node-1]  [INFO] 
[1768738381.523739704] [camera.camera]: Running with LibRealSense v2.56.4 
[realsense2_camera_node-1] 18/01 17:43:01,582 WARNING [281472505985248] (ds-motion-common.cpp:452) No HID info provided, IMU is disabled 
[realsense2_camera_node-1] 18/01 17:43:01,585 ERROR [281472505985248] (rs.cpp:256) [rs2_create_device( info_list:0xffff480271b0, index:0 ) UNKNOWN] bad optional access 
[realsense2_camera_node-1] 18/01 17:43:01,585 ERROR [281472505985248] (rs.cpp:256) [rs2_delete_device( device:nullptr ) UNKNOWN] null pointer passed for argument "device" 
[realsense2_camera_node-1] 18/01 17:43:01,585 WARNING [281472505985248] (rs.cpp:392) null pointer passed for argument "device" 
[realsense2_camera_node-1] [WARN] [1768738381.585297414] [camera.camera]: Device 1/1 failed with exception: bad optional access 
[realsense2_camera_node-1] [ERROR] [1768738381.585605897] [camera.camera]: The requested device with is NOT found. Will Try again. 

[realsense2_camera_node-1] 18/01 17:43:07,873 WARNING [281472505985248] (ds-motion-common.cpp:452) No HID info provided, IMU is disabled 
[realsense2_camera_node-1] 18/01 17:43:07,874 ERROR [281472505985248] (rs.cpp:256) [rs2_create_device( info_list:0xffff48025b40, index:0 ) UNKNOWN] bad optional access 
[realsense2_camera_node-1] 18/01 17:43:07,874 ERROR [281472505985248] (rs.cpp:256) [rs2_delete_device( device:nullptr ) UNKNOWN] null pointer passed for argument "device" 
[realsense2_camera_node-1] 18/01 17:43:07,874 WARNING [281472505985248] (rs.cpp:392) null pointer passed for argument "device" 
[realsense2_camera_node-1] [WARN] [1768738387.874646571] [camera.camera]: Device 1/1 failed with exception: bad optional access 
[realsense2_camera_node-1] [ERROR] [1768738387.874881741] [camera.camera]: The requested device with is NOT found. Will Try again. 

^C[WARNING] [launch]: user interrupted with ctrl-c (SIGINT) 
[realsense2_camera_node-1] [INFO] 
[1768738409.413367474] [rclcpp]: signal_handler(SIGINT/SIGTERM) 
[INFO] [realsense2_camera_node-1]: process has finished cleanly [pid 34860]
```
Key inference from this is that now librealsense can see the camera, but ROS is failing when it tries to access the IMU(HID interfaces)
i.e the permissions for HID devices are not handed to ROS

If you dont want to use the IMU, temporary workaround is available:
```bash
ros2 launch realsense2_camera rs_launch.py enable_gyro:=false enable_accel:=false
```

1) Verify the problem:
```bash
ls -l /dev/hidraw*
```
expected output
```bash
crw------- root root ...
crw-rw---- root plugdev ...
```
The device has multiple HID interfaces and ROS/librealsense needs access to all

plugdev and video must me in groups, if not add it
```bash
sudo usermod -aG plugdev,video $USER
```
relogin

2) Ensure ROS librealsense udev rules exist
   For ROS-installed librealsense, the correct udev rules are here:
   ```bash
   ls /etc/udev/rules.d | grep realsense
   ```
   Expected result: 99-realsense-libusb.rules
   If not present, reinstall the package 
   ```bash
   sudo apt reinstall ros-humble-librealsense2
   ```
   Then reload
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```
   Unplug and replug the camera once

3) Fix HID permissions immediately (non-destructive)
   ```bash
   sudo chmod 666 /dev/hidraw*
   ```
   Now test ROS launch again
4) If earlier step works, make the fix permanent:
   Create a dedicated udev rule
   ```bash
   sudo nano /etc/udev/rules.d/99-realsense-hid.rules
   ```
   Paste content:
   ```bash
   KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="8086", MODE="0666"
   ```
   Save and reload the rules and replug the camera
