```bash
ros2 run synexens_depth_bridge points2_to_depth
```
```bash
 ros2 launch synexens_ros2 driver_launch.py
```
```bash
ros2 launch kiss_icp odometry.launch.py \
  topic:=/camera1_HV0121115C0539/points2 \
  use_sim_time:=false \
  visualize:=false \
  base_frame:=depth_camera_link \
  publish_odom_tf:=true \
  invert_odom_tf:=false \
  deskew:=false \
  max_range:=4.5 \
  min_range:=0.3 \
  voxel_size:=0.05
```
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
