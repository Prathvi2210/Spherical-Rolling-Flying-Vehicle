Background:
I have installed FastLio2 in ROS2.
Installed synexens CS20 drivers in ROS2 and the data is verified.
Installed Mavros2 and pixhawk cube orange data is verified, keep IMU stream rate to 200.

Now I'll be launching fastlio2 with synexens pointcloud data and IMU data from pixhawk via Mavros.
All terminals should be sourced.  
```bash
source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash
```

1) Pixhak + Mavros2
```bash
ros2 run mavros mavros_node --ros-args \
-p fcu_url:=/dev/ttyACM0:57600 \
-p target_system:=1 \
-p target_component:=1 \
-p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml
```
Set the IMU stream rate- In different terminal
```bash
ros2 service call /mavros/set_stream_rate mavros_msgs/srv/StreamRate \
"{stream_id: 0, message_rate: 200, on_off: true}"
```
For IMU only, stream_id=6
Confirm IMU topic:
```bash
ros2 topic echo /mavros/imu/data --once
```
2) Synexens CS20
Launch only pointcloud, depth feed not required for LIO, this reduces processing
```bash
ros2 launch synexens_camera cs20.launch.py
```
Confirm pointcloud topic
```bash
ros2 topic echo /camera1/points2 --once
```
Minimal is 10-15 Hz
3) FAST-LIO2 parameter sanity check: edit the FAST-LIO2 config (usually fastlio.yaml):
```bash
nano ~/ros2_ws/src/FAST_LIO_ROS2/config/cs20.yaml
```
cs20.yaml is created previously, FAST-LIO2 has sensor specific config files. To make the cs20.yaml file:
```bash
cd ~/ros2_ws/src/FAST_LIO_ROS2/config
cp mid360.yaml cs20.yaml
nano cs20.yaml
```
Replace/ verify only these fields:
```YAML
# ---------- Topics ----------
pointCloudTopic: /camera1/points2
imuTopic: /mavros/imu/data

# ---------- Sensor type ----------
lidar_type: 0          # 0 = generic PointCloud2
scan_line: 1           # Synexens is ToF, not spinning lidar
blind: 0.1

# ---------- Time ----------
time_sync_en: false    # MAVROS timestamps already synced

# ---------- IMU ----------
acc_cov: 0.01
gyr_cov: 0.01
b_acc_cov: 0.0001
b_gyr_cov: 0.0001

# ---------- Mapping ----------
map_publish_freq: 10
filter_size_map: 0.3
filter_size_surf: 0.3
mapping:
    acc_cov: 0.01
    gyr_cov: 0.01
```
Also change the range and FoV parameters as applicable.
Initially keep the pcd_save section as false, then enable it when launch is stable.

Tip: Verify where FAST-LIO expects configs:
```bash
grep -R "declare_parameter.*config" \
~/ros2_ws/src/FAST_LIO_ROS2 -n
```
It should be FAST_LIO_ROS2/config/
If it is something else, move the file there.

If it shows no response, it means the node does not declare a ROS parameter named config. The YAML file is not loaded via ROS parameters, it is loaded manually inside the launch file /C++ code.
mapping.launch.py hardcodes a default YAML. Open the file mapping.launch.py.
Look for:
```python
declare_config_file_cmd = DeclareLaunchArgument(
    'config_file', default_value='mid360.yaml',
    description='Config file'
)
```
and later:
```python
arguments=['--ros-args', '--params-file', default_config_file]
```
In this case the fastest solution is to change the default to cs20.yaml or edit the mid360.yaml itself and make it into the cs20.yaml configuration. Then rebuild:
```bash
cd ~/ros2_ws
colcon build --packages-select fast_lio
source install/setup.bash
```

4) FAST-LIO2 launch
```bash
ros2 launch fast_lio mapping.launch.py
```
if FAST-LIO expects the config file as parameter, and you haven't made changes in the mapping.launch.py, then add config:=cs20.yaml to the launch command.
Immediately after launching, verify this in terminal: p_pre->lidar_type 0.
If this value is 1, the config is still not loaded and FAST-LIO is falling back to livox.

To check which YAML file is actually loaded:
```bash
ros2 param dump /fastlio_mapping
```

5) Validate FAST-LIO2 is consuming correct topics, after 10 seconds:
Topics that must be alive
```bash
ros2 topic list | grep -E "lio|imu|points"
```
Expected:
/camera1/points2
/mavros/imu/data
/lio_odom
/lio_map
/tf

Check these:
```bash
ros2 topic echo /lio_odom --once
ros2 topic hz /camera1/points2
ros2 topic hz /mavros/imu/data
```
If /lio topics are empty check the whole topic list, there maybe some mismatch in topic names. Example names in my system: 
Odometry= /Odometry
Point clouds= /cloud_registered
              /cloud_registered_body
              /cloud_effected
              /Laser_map
TF= /tf
    /tf_static

Odometry initial values will be large, this is because; camera_init(map) iis initialized at first valid LiDAR-IMU alignment, no constraints yet(GPS/map/EKF fusion), Z-axis sign depends on IMU mounting + gravity alignment, scale is metric but origin is arbitrary. 
FAST-LIO odometry is locally consistent, not globally meaningful (yet). So initial large position values is normal for raw LIO output. 
The velocities in twist will have zero values. FAST-LIO2 focuses on pose+covariance, so it does not publish velocity unless explicitly enabled/ extended. You'll get velocity later via EKF fusion or numerical differentiation.
6) Visualization:
```bash
rviz2
```
Add:  PointCloud2 → cloud_registerd for pcd and laser_map for optional map view
Odometry 
TF

FAST-LIO does not publish a full robot TF tree like Nav2, it publishes only what is needed:
camera_init(map)
 └── body
     └── lidar(camera)

# Errors to watch out for:
No /lio_odom- IMU topic mismatch
Map drifting- Wrong timestamp/IMU rate
Segfault at start- scan_line != 1
"No point received"- Wrong pointcloud topic added

NEXT: Fuse FAST-LIO + MAVROS in robot_localization, ArduPilot EKF (vision_pose / odom), static gravity alignment tuning, LIO-SAM/Nav2, Hard-sync IMU-CS20

Currently the aalgorithm is working, so next immediate milestone is to make it robust and deploy it for a simple AGV. It should be able to do navigation, then later reusable on a drone. First lets test the jetson+CS20+pixhawk setup then move to drone stage problems of gravity tuning and EKF.

What FAST-LIO gives you:
Accurate local LiDAR–IMU odometry.
High-quality dense map.
Frame: camera_init → body.

What FAST-LIO does not give you:
A standard map → odom → base_link tree..
Velocity suitable for planners.
Integration with Nav.
That’s exactly what robot_localization is for.

Target architecture currently:
FAST-LIO2
 └─ /Odometry (camera_init → body)
        ↓
robot_localization (EKF)
 ├─ publishes /odometry/filtered
 ├─ publishes TF: map → odom → base_link
        ↓
Nav2
 ├─ AMCL / Localization
 ├─ Global planner
 ├─ Local planner
        ↓
AGV base controller

Now we need to add robot_localization next and use FAST-LIO /Odometry as the primary source. Produce /odometry/filtered and proper TF tree. This immediately unlocks stable RViz, Nav2 compatibility, Map saving.
With Nav2 compatible TFs then we can drive the AGV manually, record the /map or /laser_map, save them and validate loop consistency.

Only after this stage can we look at object avoidance, planner tuning, flight specific tuning.
