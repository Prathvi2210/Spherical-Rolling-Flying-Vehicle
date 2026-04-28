We need to tune the config for the synexens CS20 sensor. Resolution: 320x240= 76800 points per frame, no timestamp, range: 0.2-4.5m.
The file is located at: /ros2_ws/src/kiss-icp/ros/config/config.yaml.
Contents:
```bash
kiss_icp_node:
  ros__parameters:
    base_frame: "camera_base"
    lidar_odom_frame: "odom_lidar"
    publish_odom_tf: true
    invert_odom_tf: false
    position_covariance: 0.1
    orientation_covariance: 0.1

    data:
      deskew: false        # CS20 has no per-point timestamps
      max_range: 4.5       # CS20 max effective range
      min_range: 0.3       # CS20 min range, filters out noise close to sensor

    mapping:
      voxel_size: 0.05     # 5cm voxels, appropriate for short-range ToF
      max_points_per_voxel: 20

    adaptive_threshold:
      initial_threshold: 0.5   # lower than default, CS20 is short range
      min_motion_th: 0.05      # detect smaller motions for indoor/slow driving

    registration:
      max_num_iterations: 500
      convergence_criterion: 0.0001
      max_num_threads: 0
```
Rebuild to deploy the new config
```bash
cd ~/ros2_ws
colcon build --packages-select kiss_icp --parallel-workers 1
source install/setup.bash
```
The launch command is (given is the full command for redundancy):
```bash
ros2 launch kiss_icp odometry.launch.py \
  topic:=/camera1_HV0121115C0359/points2 \
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
Here we have used:
use_sim_time:=false => to run on live hardware.
invert_odom_tf:=false => for live sensor data and put true for bagfile playback.
base_frame:=base_link => change this according to your system.
deskew:=false => becasue CS20 sensor doesn't include per-point timstamps, if not specified it will start then give warning and disable on its own.
visualize:=true => put true if you want to use RViz. 
  Add these displays:  
| Display Type   |   Topic           |  What you'll see              |
|----------------|-------------------|-------------------------------|
| Odometry       |   /kiss/odometry  |  Position + orientation arrow |
| PointCloud2    |   /kiss/local_map |  Built map of environment     |
| PointCloud2    |   /kiss/frame     |  Current scan frame           |
| PointCloud2    |   /kiss/keypoints |  Feature points used for ICP  |
| TF             |    —              |  Coordinate frames            |

RViz's odometry has a keep parameter. Set it to something like 200 in the display properties and it will draw a trail of arrows showing the path history.
Fixed Frame -> odom_lidar

