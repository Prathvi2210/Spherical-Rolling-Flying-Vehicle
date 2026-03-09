This is a hardware project, where I am using a jetson orin nano which has ROS2 Humble- cyclone DDS middleware and I am using mavros2 for connecting and collecting data from pixhawk.
I am launching using mavros_node instead of launch.py file.
The problem is that there are too many topics being published which are unnecessary for my application of LiDAR Inertial Odometry which mainly cares about IMU and time sync.
A total of 164 topics are being listed under /mavros, Mavros publishes one topic per MAVlink message and this data influx is clogging DDS.

1) Stop using stream_id = 0
   This sets all MAVlink messages on the same rate, which typically we set at 200 Hz for IMU data.
   Replace the stream call with selective streams only
   stream_id = 6 ; IMU only (RAW_SENSORS)- set high rate (200)
   stream_id = 10 ; attitude (EXTRA1)- set low rate (30)

   Do not enable 0(All), 1(Status), 2(Position), 3(Extra2) if not required.

2) Disable MAVROS plugins- these are independent publishers.
   Create a minimal plugin allowlist: mavros_minimal.yaml.
   Paste this:
```yaml
/**:
  ros__parameters:

    mavros:
      plugin_denylist:
        - actuator_control
        - adsb
        - altitude
        - cam_imu_sync
        - camera
        - cellular_status
        - companion_process_status
        - debug_value
        - distance_sensor
        - esc_status
        - esc_telemetry
        - fake_gps
        - ftp
        - geofence
        - global_position
        - gps_rtk
        - gps_status
        - guided_target
        - hil
        - home_position
        - landing_target
        - local_position
        - log_transfer
        - manual_control
        - mission
        - mount_control
        - obstacle_distance
        - onboard_computer_status
        - open_drone_id
        - param
        - play_tune
        - px4flow
        - rc_io
        - rc_status
        - safety_area
        - setpoint_accel
        - setpoint_attitude
        - setpoint_position
        - setpoint_raw
        - setpoint_velocity
        - sys_status
        - time_estimator
        - trajectory
        - tunnel
        - vfr_hud
        - vibration
        - vision_pose_estimate
        - vision_speed_estimate
        - wheel_odometry
        - wind_estimation

      plugin_allowlist:
        - imu
        - time_sync
        - system_time

    imu:
      publish_tf: false
```
Use spaces not tabs. In nano Ctrl + Shift + Tab → converts tabs to spaces. Or run
```bash
sed -i 's/\t/  /g' /home/srfv/ros2_ws/mavros_config/mavros_minimal.yaml
```
Sanity check, before launching.
```bash
python3 - <<'EOF'
import yaml
with open("/home/srfv/ros2_ws/mavros_config/mavros_minimal.yaml") as f:
    yaml.safe_load(f)
print("YAML syntax OK")
EOF
```
After launching mavros
```bash
ros2 param dump /mavros
```
or list loaded params
```bash
ros2 param list /mavros
```
or check a specific param
```bash
ros2 param get /mavros mavros.plugin_allowlist
```
You should see: mavros.plugin_blacklist, mavros.plugin_whitelist, imu.publish_tf
If this fails, YAML is invalid
3) Launch mavros with this param file explicitly mentioned:
```bash
ros2 run mavros mavros_node --ros-args \
  -p fcu_url:=/dev/ttyACM0:57600 \
  -p target_system:=1 \
  -p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml \
  --params-file /home/srfv/ros2_ws/mavros_config/mavros_minimal.yaml
```
ROS 2 does not expand (~) in --params-file, use no relative paths
After MAVROS starts, cofirm topic reduction worked.
```bash
ros2 topic list | grep mavros | wc -l
```
This shows number of publishing topics
4) Reduce IMU publisher overhead- Mavros publishes multiple IMU variants by default- data_raw, data, data_mag
   Add this to mavros_minimal.yaml
```yaml
imu:
  publish_tf: false
  linear_acceleration_stdev: 0.0003
  angular_velocity_stdev: 0.0003
  orientation_stdev: 0.0
```
5) Kill DDS overhead at RMW level (jetson-specific): Fast DDS default settings are too chatty. Switch to CycloneDDS as default
```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```
Optional tuning
```bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><NetworkInterfaceAddress>lo</NetworkInterfaceAddress></General></Domain></CycloneDDS>'
```

Optional:
# Create a tiny wrapper to avoid typing file paths everytime
```bash
nano ~run_mavros_minimal.sh
```
```bash
#!/bin/bash
source /opt/ros/humble/setup.bash

ros2 run mavros mavros_node --ros-args \
  -p fcu_url:=/dev/ttyACM0:57600 \
  -p target_system:=1 \
  -p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml \
  --params-file /home/srfv/ros2_ws/mavros_config/mavros_minimal.yaml
```
```bash
chmod +x ~/run_mavros_minimal.sh
```
Run this wrapper with
```bash
./run_mavros_minimal.sh
```

This whole try did not work, the topics did not reduce. In ROS2 humble, plugin filtering is only wired through the launch system or source-level changes.
Using ros2 run mavros mavros_node, it can't be done.
Instead, we can do this at MAVLink level. On ardupilot, set these parameters on the FCU
```code
SR0_RAW_SENS   = 1
SR0_EXT_STAT  = 0
SR0_POSITION  = 0
SR0_EXTRA1    = 0
SR0_EXTRA2    = 0
SR0_EXTRA3    = 0
```
Now mavros still creates topics but never publish, DDS load drops dramatically
Other options are to use ros2 launch mavros_launch.py,
Or rebuild mavros from source and patch plugin_allowlist to be declared parameters, recompile.
