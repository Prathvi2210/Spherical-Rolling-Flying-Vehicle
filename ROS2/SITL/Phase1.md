Activity 2: Global Path planner:
Currently python ROS2 node for A*, later we will port to JPS3D/C++ when wirin gin EGO Planner.

Copy global_planner_astar.py into your package's module dir (~/srfv_ws/src/<srfv_flight>/srfv_flight/), add the entry point to that package's setup.py under console_scripts:
'global_planner_astar = srfv_flight.global_planner_astar:main',
then colcon build --packages-select srfv_flight && source ~/srfv_ws/install/setup.bash.
Launching the built A* search Node
```bash
ros2 run srfv_flight global_planner_astar --ros-args \
  -p pcd_path:=/home/prime/srfv_maps/phase1_20260619_164447/GlobalMap.pcd \
  -p resolution:=0.15 -p inflation_radius:=0.3 -p start:="[0.0, 0.0, 1.0]"
```
RViz, Fixed Frame map: add PointCloud2 /srfv/global/occupied (the map), Path /srfv/global/path, 
MarkerArray /srfv/global/endpoints. Click 2D Goal Pose and pick a spot — a path should appear and the log prints waypoint count, length, and solve time.
This was static, later I renamed this script but didn't include it in the setup.py console.

Dynamic-live planner deployment:
```bash
ros2 run srfv_flight global_planner_astar --ros-args \
  -p live_map:=true \
  -p resolution:=0.15 -p inflation_radius:=0.3 \
```
-p start:="[0.0, 0.0, 1.0]": this argument causes the drone origin to be hardcoded, we want the path to be from where the drone currently is.
To launch the dynamic global planner with an existing map:
```bash
ros2 run srfv_flight global_planner_astar --ros-args \
  -p pcd_path:=/home/prime/srfv_maps/phase1_20260619_164447/GlobalMap.pcd \
  -p live_map:=true \
  -p resolution:=0.15 -p inflation_radius:=0.3
```
Launch the waypoint sequencer- to make the drone follow the given path by path planner:
```bash
ros2 run srfv_flight waypoint_sequencer
```
or to run directly:
```bash
python3 ~/srfv_ws/src/srfv_flight/srfv_flight/waypoint_sequencer.py
```
# ATE error measurement:
Ground truth bridge:
```bash
ros2 run ros_gz_bridge parameter_bridge /world/iris_house/dynamic_pose/info@tf2_msgs/msg/TFMessage[gz.msgs.Pose_V
```
Recording:
```bash
ros2 bag record -o ate_run /lio_sam/mapping/odometry /world/iris_house/dynamic_pose/info
```
Upon Ctrl+, it will save the map in whatever directory the command is run in. It will make a folder 'ate_run'.
Convert+evaluate:
```python3 bag_to_tum.py ~/srfv_ws/ate_run
evo_ape tum ground_truth.tum slam_estimate.tum -a --save_results phase1_ate.zip
```
This code is in srfv_ws and need to point to the ate_run folder.
Current metrics: 
      max    0.870110
      mean    0.458870
    median    0.443093
       min    0.075664
      rmse    0.494003
       sse    114.942562
       std    0.182970
