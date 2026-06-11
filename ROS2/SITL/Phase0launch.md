Terminal 1: Gazebo (physics + world, headless)
```bash
gz sim -v4 -r iris_indoor.sdf
```
Changed the runway world to add indoor features and limit drift in SLAM Map
Wait for gazebo window showing the iris quad on a runway. Leave it running.

Terminal 2: ROS bridge (sensors+clock)
```bash
ros2 run ros_gz_bridge parameter_bridge \
  /scan/points@sensor_msgs/msg/PointCloud2[gz.msgs.PointCloudPacked \
  /imu@sensor_msgs/msg/Imu[gz.msgs.IMU \
  /clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock \
  --ros-args -r /scan/points:=/points -r /imu:=/imu/data
```

Terminal 3: LiDAR time-field adapter
```bash
source ~/srfv_ws/install/setup.bash
ros2 run srfv_flight lidar_time_field
```

Terminal 4: LIO SAM
```bash
source ~/srfv_ws/install/setup.bash
ros2 launch ~/srfv_ws/srfv_slam.launch.py
```

Terminal 5: RViz (sim time)
```bash
ros2 run rviz2 rviz2 --ros-args -p use_sim_time:=true
```
RViz setup: Fixed Frame = map
Add PointCloud2 → /lio_sam/mapping/map_global — Reliability Reliable, Size 0.05 (the growing map)
Add PointCloud2 → /lio_sam/mapping/cloud_registered_raw — Reliability Reliable (live scan — best for watching it fly)
Add Odometry → /lio_sam/mapping/odometry — Reliability Best Effort
(optional) Add TF
Save this config as srfv.rviz, future launch commands:
```bash
rviz2 -d ~/srfv_ws/srfv.rviz --ros-args -p use_sim_time:=true
```

Terminal 6: ArduCopter SITL (Flight controller, connects to gazebo)
```bash
cd ~/ardupilot/ArduCopter
sim_vehicle.py -v ArduCopter -f gazebo-iris --model JSON --console
```
--map argument is deleted here, we wont be using the ardupilot map
Wait for SITL to connect, then EKF IMU0 origin set and EKF IMU0 is using GPS

Check the params, so you don't run into pre arm check wall
```bash
param show FRAME_CLASS
param show FRAME_TYPE
```
Both should show '1'
If any of them is set to 0, relaunch ardupilot with '-w' appended to wipe+reload the gazebo-iris defaults, then recheck
If not done before set the velocity and acceleration, fast motion will not allow it to update and build the map. Default GUIDED speed is set at 10m
```
param set WPNAV_SPEED 150
param set WPNAV_SPEED_UP 75
param set WPNAV_SPEED_DN 75
param set WPNAV_ACCEL 100
```
That's 1.5 m/s horizontal, 0.75 m/s vertical, gentle accel — LIO-SAM-friendly.

Terminal 7: mavros (MAVLink-->ROS2 bridge, APM config)
```bash
# source /opt/ros/humble/setup.bash   # only if not already in your bashrc
pkill -f mavros          # clear any stale instance first
ros2 launch mavros apm.launch fcu_url:="tcp://127.0.0.1:5762"
```
Using apm.launch is what loads ArduPilot's stream-rate config so the position topics actually publish.

Terminal 8: verify working and fly from waypoint from the terminal
Set the mavros message interval service:
```bash
ros2 service call /mavros/set_message_interval mavros_msgs/srv/MessageInterval \
  "{message_id: 32, message_rate: 10.0}"
```
mavros has a service /mavros/set_message_interval for this issue.
```bash
ros2 topic echo /mavros/state --once                 # connected: true
ros2 service list | grep -E 'set_mode|arming|takeoff'  # the 3 control services
ros2 topic hz /mavros/local_position/pose             # should now report ~2-10 Hz, Ctrl-C to stop
```
Control sequence(GUIDED->arm->takeoff)
```bash
ros2 service call /mavros/set_mode mavros_msgs/srv/SetMode "{base_mode: 0, custom_mode: 'GUIDED'}"
ros2 service call /mavros/cmd/arming mavros_msgs/srv/CommandBool "{value: true}"
ros2 service call /mavros/cmd/takeoff mavros_msgs/srv/CommandTOL \
  "{min_pitch: 0.0, yaw: 0.0, latitude: 0.0, longitude: 0.0, altitude: 5.0}"
```

Confirm and reset:
```bash
ros2 topic echo /mavros/local_position/pose --once    # pose.position.z ~ 5.0
ros2 service call /mavros/set_mode mavros_msgs/srv/SetMode "{base_mode: 0, custom_mode: 'RTL'}"
```

Waypoint navigation
```bash
source install/setup.bash
ros2 run srfv_flight guided_waypoint
```
To go to a custom waypoint:
```bash
ros2 run srfv_flight guided_waypoint --ros-args -p target_x:=10.0 -p target_y:=0.0 -p takeoff_alt:=2.0.
```


