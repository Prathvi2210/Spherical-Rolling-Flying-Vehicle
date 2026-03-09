Update package lists
```bash
sudo apt update
```
Install MAVROS and dependencies
```bash
sudo apt install ros-humble-mavros ros-humble-mavros-extras ros-humble-mavros-msgs
```
Install GeographicLib datasets (required for MAVROS). They are needed for GPS altitude conversion (WGS84<->geoid).
```bash
wget https://raw.githubusercontent.com/mavlink/mavros/master/mavros/scripts/install_geographiclib_datasets.sh
chmod +x install_geographiclib_datasets.sh
./install_geographiclib_datasets.sh
```
This is the official MAVROS helper script. It internally Installs geographiclib tools if missing, Downloads multiple datasets needed by MAVROS: geoids(egm96), gravity models, magnetic models; and places them in /usr/share/GeogrpahicLib.
There is a simpler manual install using :
```bash
sudo apt install -y geographiclib-tools
sudo geographiclib-get-geoids egm96-5
```
This is a minimal and faster installation, installs only strictly needed dataset.
verify GeographicLib tools installation:
```bash
ls /usr/share/GeographicLiib/geoids
```
You should see: egm96-5.pgm


Post installation test
Source ROS2 Humble
```bash
source /opt/ros/humble/setup.bash
```
Find your Pixhawk serial port first
```bash
dmesg | grep tty  # or ls /dev/ttyUSB*
```
Run MAVROS node for serial connection (adjust port/baudrate)
```bash
ros2 run mavros mavros_node \
  --ros-args \
  --param fcu_url:=/dev/ttyUSB0:57600 \
  --param gcs_url:= \
  --param target_system:=1 \
  --param config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml
```

Check available config files in MAVROS package
```bash
ros2 pkg prefix mavros
find /opt/ros/humble/share/mavros -name "*.yaml" | grep -E "(apm|px4)"
```

new working launch snippet on jetson orin nano
```bash
ros2 run mavros mavros_node --ros-args\
-p fcu_url:=/dev/ttyACM0:57600\
-p target_system:=1\
-p config_file:=/opt/ros/humble/share/mavros/launch/apm_config.yaml
```
In ROS 2 parameter overrides must always have a value, an empty value is invalid syntax. If you explicitly want to define gcs_url set it to "".
The launch log should contain:
MAVROS connected: CON: Got HEARTBEAT, connected. FCU: ArduPilot.
Time sync active: [mavros.time]: TM: Timesync mode: MAVLINK.
Pixhawk detected correctly: FCU: ArduCopter V4.6.0, FCU: CubeOrangePlus.
IMU running at high rate internally: IMU0: fast sampling 2.0kHz, IMU1: fast sampling enabled 9.0kHz.

After starting verify connection
```bash
ros2 topic list | grep mavros
ros2 service list | grep mavros
```
Check frequency and timestamps (monotonic and stable) of imu topics
```bash
ros2 topic hz /mavros/imu/data
```
Do a 60-sec stationary test by echoing imu/data, make sure it is stable
Time-synchronization, before using the topic data:
```bash
ros2 param get /mavros time_sync
```
Make sure the time sync is enabled
if not:
```bash
ros2 param set /mavros time_sync true
```
then verify
```bash
ros2 topic echo /mavros/imu/data/header/stamp
```
Pixhawk and system time must be aligned within few milliseconds

Setting MAVLINK stream rates: Run the following commands one by one
```bash
ros2 service call /mavros/param/set mavros_msgs/srv/ParamSet \
"{param_id: 'SR0_RAW_SENS', value: {integer: 50}}"
```
```bash
ros2 service call /mavros/param/set mavros_msgs/srv/ParamSet \
"{param_id: 'SR0_EXTRA1', value: {integer: 50}}"
```
```bash
ros2 service call /mavros/param/set mavros_msgs/srv/ParamSet \
"{param_id: 'SR0_EXTRA2', value: {integer: 50}}"
```
```bash
ros2 service call /mavros/param/set mavros_msgs/srv/ParamSet \
"{param_id: 'SR0_POSITION', value: {integer: 50}}"
```
SR0_* = USB/Telem stream
RAW_SENS = IMU raw data
EXTRA1 = attitude
EXTRA2 = IMP processed
These values are Hz

If stream still does not start, verify if the topics exist. If topics exist but no data flow, force request.
```bash
ros2 service call /mavros/set_stream_rate mavros_msgs/srv/StreamRate \
"{stream_id: 0, message_rate: 200, on_off: true}"
```
This tells ardupilot: start streaming at 200 Hz.
For only enabling IMU set stream_id: 6
stream_id 10 = EXTRA1 → attitude
stream_id 0 (ALL)
stream_id 1 (STATUS)
stream_id 2 (POSITION)
stream_id 3 (EXTRA3)

# How to control stream rates in ROS2 MAVROS- param/value is not a topic supported by ROS2, it exists in ROS1
Confirm the service exists
```bash
ros2 service list  | grep stream
```
You should see: /mavros/set_stream_rate.
Then run the force request, 200 Hz command

If you want the FCU to always stream IMU, even after reboot:
Check parameters:
```bash
ros2 service call /mavros/param/get mavros_msgs/srv/ParamGet \
"{param_id: 'SR0_RAW_SENS'}"
```
Then set them
```bash
ros2 service call /mavros/param/set mavros_msgs/srv/ParamSet \
"{param_id: 'SR0_RAW_SENS', value: {integer: 50}}"
```
