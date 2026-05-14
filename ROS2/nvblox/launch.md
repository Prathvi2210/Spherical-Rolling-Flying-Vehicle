```bash
 ros2 launch synexens_ros2 driver_launch.py
```

```bash
ros2 run synexens_depth_bridge points2_to_depth
```

```bash
ros2 launch kiss_icp odometry.launch.py \
  topic:=/camera1_HV0121115C0539/points2 \
  use_sim_time:=false \
  visualize:=false \
  base_frame:=depth_camera_link \ #base_link
  odom_frame:= odom \
  publish_odom_tf:=true \ #False
  invert_odom_tf:=false \
  deskew:=false \
  max_range:=4.5 \
  min_range:=0.3 \
  voxel_size:=0.05 
```
```bash
ros2 run mavros mavros_node --ros-args -p fcu_url:=/dev/ttyACM0:57600 -p target_system:=1 -p target_component:=1 -p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml
```
```bash
ros2 service call /mavros/set_stream_rate mavros_msgs/srv/StreamRate \ \
```
```bash
ros2 launch sensor_fusion ekf.launch.py
```
```bash
ros2 run tf2_ros static_transform_publisher \
> --x 0.04 --y 0.02 --z 0.0 \
> --roll 0 --pitch 0 --yaw 0 \
> --frame-id base_link --child-frame-id depth_camera_link
```
```bash
ros2 run tf2_tools view_frames
```
stuck here 12/05/2026
```bash
cd ${ISAAC_ROS_WS}/src/isaac_ros_common
./scripts/run_dev.sh
```
```bash
ros2 launch /workspaces/isaac_ros-dev/src/nvblox_synexens/launch/nvblox_synexens.launch.py
```
```bash
rviz2
```
