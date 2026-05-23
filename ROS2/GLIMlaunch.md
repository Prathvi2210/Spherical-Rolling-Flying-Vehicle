```bash
ros2 launch synexens_ros2 driver_launch.py
```
```bash
source ~/ros2_ws/install/setup.bash
ros2 run cs20_restamper restamper_node
```
```bash
ros2 run mavros mavros_node --ros-args \
-p fcu_url:=/dev/ttyACM0:57600 \
-p target_system:=1 \
-p target_component:=1 \
-p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml
```
```bash
ros2 service call /mavros/set_stream_rate mavros_msgs/srv/StreamRate \
"{stream_id: 0, message_rate: 200, on_off: true}"
```
```bash
source /opt/ros/humble/setup.bash
ros2 run tf2_ros static_transform_publisher \
  0.08 0.02 0.0 0 0 0 1 \
  base_link depth_camera_link
```
```bash
source /opt/ros/humble/setup.bash
ros2 run glim_ros glim_rosnode \
  --ros-args -p config_path:=/home/$USER/glim_ws/config
```
