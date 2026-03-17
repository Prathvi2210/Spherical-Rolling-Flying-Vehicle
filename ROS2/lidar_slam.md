```bash
cd ~/ros2_ws/src
git clone --recursive https://github.com/rsasaki0109/lidarslam_ros2
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-select lidarslam scanmatcher --cmake-args -DCMAKE_BUILD_TYPE=Release --parallel-workers 1
source install/setup.bash
```
In case of any error it may be due to missing dependencies. I am putting the dependencies needed on my system jetson orin nano super with JetPack 6.2.1.
Run these commands and then build the lidar_slam package
```bash
colcon build --packages-select lidarslam_msgs ndt_omp_ros2 --cmake-args -DCMAKE_BUILD_TYPE=Release --parallel-workers 1
colcon build --packages-select graph_based_slam --cmake-args -DCMAKE_BUILD_TYPE=Release --parallel-workers 1
```

Now we need to edit the param file. It will be located at: /ros2_ws/src/lidarslam_ros2/lidarslam/param/lidarslam.yaml.
Change the content to:
```bash
scan_matcher:
  ros__parameters:
    global_frame_id: "map"
    robot_frame_id: "camera_base"      # changed from base_link
    odom_frame_id: "odom_lidar"
    registration_method: "GICP"        # Changed from NDT, because GICP doesn't require intensity input
    ndt_resolution: 0.2                # changed from 2.0, CS20 short range
    ndt_num_threads: 2
    gicp_corr_dist_threshold: 1.0      # changed from 5.0, CS20 short range
    trans_for_mapupdate: 0.2           # changed from 1.5, detect smaller movements
    vg_size_for_input: 0.05            # changed from 0.5, matches kiss-icp voxel
    vg_size_for_map: 0.05             # changed from 0.1
    use_min_max_filter: true
    scan_min_range: 0.3                # changed from 1.0, CS20 min range
    scan_max_range: 4.5                # changed from 200.0, CS20 max range
    scan_period: 0.033                 # changed from 0.2, CS20 runs at 30fps
    map_publish_period: 15.0
    num_targeted_cloud: 20
    set_initial_pose: true
    initial_pose_x: 0.0
    initial_pose_y: 0.0
    initial_pose_z: 0.0
    initial_pose_qx: 0.0
    initial_pose_qy: 0.0
    initial_pose_qz: 0.0
    initial_pose_qw: 1.0
    use_imu: false
    use_odom: true                     # changed to true, use kiss-icp odometry
    debug_flag: false

graph_based_slam:
    ros__parameters:
      registration_method: "GICP"
      ndt_resolution: 0.2              # changed from 1.0
      ndt_num_threads: 2
      voxel_leaf_size: 0.05            # changed from 0.1
      loop_detection_period: 3000
      threshold_loop_closure_score: 0.7
      distance_loop_closure: 10.0     # changed from 100.0, shorter range sensor
      range_of_searching_loop_closure: 5.0  # changed from 20.0
      search_submap_num: 2
      num_adjacent_pose_cnstraints: 5
      use_save_map_in_loop: true
      debug_flag: true
```
Edit the launch file. Found at: /ros2_ws/src/lidarslam_ros2/lidarslam/launch/lidarslam.launch.py.
Find this line:
```bash
remappings=[('/input_cloud','/velodyne_points')],
```
change to:
```bash
remappings=[('/input_cloud', '/points_with_intensity'),
            ('/odom', '/kiss/odometry')],
```
Also update the static transform and frame name. Find this line:
```bash
arguments=['0','0','0','0','0','0','1','base_link','velodyne']
```
Change it to:
```bash
arguments=['0','0','0','0','0','0','1','camera_base','depth_camera_link']
```
Fix the intensity issue by writing a small ROS 2 node that adds a fake intensity field to the CS20 pointcloud.
The code is written in /ros2_ws/src/lidarslam_ros2/scripts/add_intensity.py.
Add the relay node to the launch description:
```bash
add_intensity = launch_ros.actions.Node(
    package='lidarslam',
    executable='add_intensity.py',
    name='add_intensity',
    output='screen'
)
```
And add add_intensity to the LaunchDescription list in launch.LaunchDescription.
Register the script in CMakeLists.txt. Add this before the ament_package() line:
```bash
install(PROGRAMS
  scripts/add_intensity.py
  DESTINATION lib/${PROJECT_NAME}
)
```
Rebuild and deploy new config:
```bash
cd ~/ros2_ws
colcon build --packages-select lidarslam --cmake-args -DCMAKE_BUILD_TYPE=Release --parallel-workers 1
source install/setup.bash
```
Incase you have been debugging errors, try to do a clean rebuild as sometimes the changes in config and launch files dont take effect. 
```bash
rm -rf ~/ros2_ws/install/lidarslam
```
Then launch with
```bash
ros2 launch lidarslam lidarslam.launch.py
```
This will auto launch RViz with its own configuration, so you can turn it off in kiss-icp launch.
Set fixed frame to map if not by default
