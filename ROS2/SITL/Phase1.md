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
  -p start:="[0.0, 0.0, 1.0]"
```
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
