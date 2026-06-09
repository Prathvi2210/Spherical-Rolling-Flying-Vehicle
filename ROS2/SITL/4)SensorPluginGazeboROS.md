Task 4 was to create plugins for sensor. We will be using a VLP-16 lidar and dedicated gazebo IMU (not the ardupilot EKF one) for SLAM.

4.1- Install ros_gz: we need to pair ROS humble and Gazebo Harmonic. ros-humble-ros-gz is built for gazebo fortress, for harmonic we need dedicated metapackage.
We already have the OSRF gazebo apt repo,so:
```bash
# guard: if the Fortress build is present it conflicts — check first
dpkg -l | grep -E 'ros-humble-ros-gz' || echo "none installed yet"
sudo apt update
sudo apt install -y ros-humble-ros-gzharmonic
```
After it completes successfully, verify
```bash
source /opt/ros/humble/setup.bash
ros2 pkg prefix ros_gz_bridge        # should print a path, not an error
ros2 run ros_gz_bridge parameter_bridge --help   # confirms the bridge runs
```
Expect some BrokenPipeError at the end, these are harmless. Nothing wrong with the install.

4.2 Mount the VLP-16+IMU on iris quadcopter
First check the actual model, so we can attach the links to the right base-link
```bash
cat ~/ardupilot_gazebo/models/iris_with_ardupilot/model.sdf
sed -n '1,60p' ~/ardupilot_gazebo/worlds/iris_runway.sdf
```
First one shows how the iris is assembled and base-link name.
Second shows the <world> header and existing <plugin> entries.
My model shows that iris_runway.sdf already declares gz-sim-sensors-system with ogre2 and gz-sim-imu-system. No need for world-plugin surgery. The sensors will render in gazebo world as soon as we attach them.
Also the ArduPilotPlugin reads iris_with_standoffs::imu_link::imu_sensor---there is an IMU present in the FC. We wont use it anyway and model a dedicated one and co-locate it with the lidar.
One check:
```bash
grep -n "<link name=" ~/ardupilot_gazebo/models/iris_with_standoffs/model.sdf
grep -n -A4 "<include" ~/ardupilot_gazebo/worlds/iris_runway.sdf
```
First tells us the real base-link name, the second shows how the world pulls in the iris model so we can point the new world at iris_with_lidar correctly.
The approach here is: copy iris_with_ardupilot -> a new iris_with_lidar. Add one link lidar_link carrying both gpu_lidar and imu. 
Copy iris_runway.sdf->iris_lidar.sdf, with its iris include pointed at iris_with_lidar.

The runway world spawns iris_with_gimbal(which is the one we have been flying) instead of iris_with_ardupilot. Now we drop it and use the iris_with_ardupilot.
Also confirm from above grep that base link is base_link, FCU's imu-link is at 236, there is no existing lidar_link to collide with.
Create the LiDAR model:
```bash
cd ~/ardupilot_gazebo/models
cp -r iris_with_ardupilot iris_with_lidar
sed -i 's|iris_with_ardupilot|iris_with_lidar|g' iris_with_lidar/model.config

python3 - << 'PYEOF'
path = "iris_with_lidar/model.sdf"
with open(path) as f:
    sdf = f.read()

sdf = sdf.replace('<model name="iris_with_ardupilot">',
                  '<model name="iris_with_lidar">')

lidar_block = """
    <!-- ===== perception sensor head: VLP-16 LiDAR + co-located IMU ===== -->
    <link name="lidar_link">
      <pose>0 0 0.30 0 0 0</pose>
      <inertial>
        <mass>0.1</mass>
        <inertia><ixx>1e-4</ixx><iyy>1e-4</iyy><izz>1e-4</izz>
          <ixy>0</ixy><ixz>0</ixz><iyz>0</iyz></inertia>
      </inertial>
      <visual name="lidar_visual">
        <geometry><cylinder><radius>0.05</radius><length>0.07</length></cylinder></geometry>
        <material><ambient>0.1 0.1 0.1 1</ambient><diffuse>0.2 0.2 0.2 1</diffuse></material>
      </visual>
      <sensor name="lidar" type="gpu_lidar">
        <pose>0 0 0 0 0 0</pose>
        <topic>scan</topic>
        <update_rate>10</update_rate>
        <always_on>1</always_on>
        <visualize>true</visualize>
        <lidar>
          <scan>
            <horizontal><samples>900</samples><resolution>1</resolution>
              <min_angle>-3.14159</min_angle><max_angle>3.14159</max_angle></horizontal>
            <vertical><samples>16</samples><resolution>1</resolution>
              <min_angle>-0.261799</min_angle><max_angle>0.261799</max_angle></vertical>
          </scan>
          <range><min>0.1</min><max>100.0</max><resolution>0.01</resolution></range>
          <noise><type>gaussian</type><mean>0.0</mean><stddev>0.008</stddev></noise>
        </lidar>
        <gz_frame_id>lidar_link</gz_frame_id>
      </sensor>
      <sensor name="lidar_imu" type="imu">
        <topic>imu</topic>
        <update_rate>200</update_rate>
        <always_on>1</always_on>
        <gz_frame_id>lidar_link</gz_frame_id>
      </sensor>
    </link>
    <joint name="lidar_joint" type="fixed">
      <parent>iris_with_standoffs::base_link</parent>
      <child>lidar_link</child>
    </joint>
"""

idx = sdf.rfind("</model>")
sdf = sdf[:idx] + lidar_block + sdf[idx:]
with open(path, "w") as f:
    f.write(sdf)
print("inserted lidar_link + imu into", path)
PYEOF
```
Now both sensors sit on lidar_link, fixed-jointed to base_link. 0.30 used in z direction as a placeholder above the rotor plane. 
Create the world:
```bash
cd ~/ardupilot_gazebo/worlds
cp iris_runway.sdf iris_lidar.sdf
sed -i 's|model://iris_with_gimbal|model://iris_with_lidar|' iris_lidar.sdf
sed -i 's|<world name="iris_runway">|<world name="iris_lidar">|' iris_lidar.sdf
```
Wipe and rebuild the model exactly once- fix for error of twice loading.
```bash
cd ~/ardupilot_gazebo/models
rm -rf iris_with_lidar
cp -r iris_with_ardupilot iris_with_lidar
sed -i 's|iris_with_ardupilot|iris_with_lidar|g' iris_with_lidar/model.config
grep -c '<link name="lidar_link">' iris_with_lidar/model.sdf   # expect 0 here
```
```bash
python3 - << 'PYEOF'
path = "iris_with_lidar/model.sdf"
with open(path) as f:
    sdf = f.read()

if 'name="lidar_link"' in sdf:
    print("lidar_link already present — skipping (nothing to do)")
else:
    sdf = sdf.replace('<model name="iris_with_ardupilot">',
                      '<model name="iris_with_lidar">')
    lidar_block = """
    <!-- ===== perception sensor head: VLP-16 LiDAR + co-located IMU ===== -->
    <link name="lidar_link">
      <pose>0 0 0.30 0 0 0</pose>
      <inertial>
        <mass>0.1</mass>
        <inertia><ixx>1e-4</ixx><iyy>1e-4</iyy><izz>1e-4</izz>
          <ixy>0</ixy><ixz>0</ixz><iyz>0</iyz></inertia>
      </inertial>
      <visual name="lidar_visual">
        <geometry><cylinder><radius>0.05</radius><length>0.07</length></cylinder></geometry>
        <material><ambient>0.1 0.1 0.1 1</ambient><diffuse>0.2 0.2 0.2 1</diffuse></material>
      </visual>
      <sensor name="lidar" type="gpu_lidar">
        <pose>0 0 0 0 0 0</pose>
        <topic>scan</topic>
        <update_rate>10</update_rate>
        <always_on>1</always_on>
        <visualize>true</visualize>
        <lidar>
          <scan>
            <horizontal><samples>900</samples><resolution>1</resolution>
              <min_angle>-3.14159</min_angle><max_angle>3.14159</max_angle></horizontal>
            <vertical><samples>16</samples><resolution>1</resolution>
              <min_angle>-0.261799</min_angle><max_angle>0.261799</max_angle></vertical>
          </scan>
          <range><min>0.1</min><max>100.0</max><resolution>0.01</resolution></range>
          <noise><type>gaussian</type><mean>0.0</mean><stddev>0.008</stddev></noise>
        </lidar>
        <gz_frame_id>lidar_link</gz_frame_id>
      </sensor>
      <sensor name="lidar_imu" type="imu">
        <topic>imu</topic>
        <update_rate>200</update_rate>
        <always_on>1</always_on>
        <gz_frame_id>lidar_link</gz_frame_id>
      </sensor>
    </link>
    <joint name="lidar_joint" type="fixed">
      <parent>iris_with_standoffs::base_link</parent>
      <child>lidar_link</child>
    </joint>
"""
    idx = sdf.rfind("</model>")
    sdf = sdf[:idx] + lidar_block + sdf[idx:]
    with open(path, "w") as f:
        f.write(sdf)
    print("inserted lidar_link + imu once")
PYEOF
```
Verify it's in exactly once:
```bash
grep -c '<link name="lidar_link">' iris_with_lidar/model.sdf   # expect 1
```
launch headless (GPU Lidar is heavy for processing)
```bash
gz sim -v4 -r -s iris_lidar.sdf
```
Once this loads cleanly, run the topic checks:
```bash
gz topic -l | grep -E 'scan|imu'
gz topic -i -t /scan/points
gz topic -e -t /imu -n 1
```
Check the IMU state, position near zero, gravity acceleration as 9.8 in z direction, angular velocity ~ 0 and entity_name:iris_with_lidar::lidar_link::lidar_imu.

4.4) next we bridge to ROS2
Check the gz topic names:
```bash
gz topic -l | grep scan
gz topic -i -t /scan/points        # expect type gz.msgs.PointCloudPacked
```
Then run the bridge with three topics: the cloud, the IMU, and the /clock (So every ROS node downstream runs on sim time):
```bash
source /opt/ros/humble/setup.bash
ros2 run ros_gz_bridge parameter_bridge \
  /scan/points@sensor_msgs/msg/PointCloud2[gz.msgs.PointCloudPacked \
  /imu@sensor_msgs/msg/Imu[gz.msgs.IMU \
  /clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock \
  --ros-args -r /scan/points:=/points -r /imu:=/imu/data
```
The [ means gz→ROS (read-only into ROS). You only need Gazebo running for this — SITL/mavros aren't required to verify sensors.
Verify in ROS 2 (another terminal):
```bash
ros2 topic list | grep -E 'points|imu/data|clock'
ros2 topic hz /points        # ~10 Hz
ros2 topic hz /imu/data       # ~200 Hz
ros2 topic echo /points --field header --once   # frame_id should be lidar_link
```
Lidar is set at 10Hz and IMU at 200Hz but it wont show that stream rate. If the rendering is GPU heavy, gazebo will implement an appropriate time factor between sim-clock and wall-clock.
```
gz topic -e -t /world/iris_lidar/stats -n 1   # look at real_time_factor (~0.48 expected)
```
Mine was 0.4568. So the stream rates were seen to be nearly 45.68 % of expected. It is correct in background just slow in showing.
Can be corrected later by dropping LiDAR <samples> 900->512, or IMU <update_rate> 200->100 which is enough for LIO-SAM, for now let it run.

4.5) make a static link for base_link->lidar_link
```bash
ros2 run tf2_ros static_transform_publisher \
  --x 0 --y 0 --z 0.30 --frame-id base_link --child-frame-id lidar_link \
  --ros-args -p use_sim_time:=true
```

optional visual check, might be heavy for GPU:
```bash
rviz2 --ros-args -p use_sim_time:=true
# Fixed Frame = base_link, Add → PointCloud2 → /points
```
You should see the runway/ground sweep as a 16-ring scan around the drone.
This completes Task4: 
  T1=iris_lidar gazebo headless launch: 
```bash
gz sim -v4 -r -s iris_lidar.sdf
```
T2= ros_gz bridge
T3= static transform
  
