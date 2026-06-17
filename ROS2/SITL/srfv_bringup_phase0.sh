#!/usr/bin/env bash
#
# srfv_bringup.sh - bring up the full SRFV stack (8 processes) in a tmux session.
#
#   ./srfv_bringup.sh
#   tmux attach -t srfv          # then Ctrl-b <0..7> to switch windows
#   tmux kill-session -t srfv    # tears down ALL 8 at once
#
# Windows: 0 gz | 1 bridge | 2 timefield | 3 liosam | 4 rviz | 5 sitl | 6 mavros | 7 teleop
# The teleop window (7) has its command QUEUED but NOT run - switch to it and press
# Enter when you're watching, since it auto-arms and takes off.
#
# Swap the world by editing WORLD below (e.g. iris_indoor.sdf for the corridor).

set -e
command -v tmux >/dev/null || { echo "tmux not installed:  sudo apt install tmux"; exit 1; }

WS="$HOME/srfv_ws"
WORLD="$HOME/ardupilot_gazebo/worlds/iris_house_trim.sdf"
SRC="source /opt/ros/humble/setup.bash && source $WS/install/setup.bash"
S=srfv

OFFLOAD="__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia"
BRIDGE="ros2 run ros_gz_bridge parameter_bridge \
/scan/points@sensor_msgs/msg/PointCloud2[gz.msgs.PointCloudPacked \
/imu@sensor_msgs/msg/Imu[gz.msgs.IMU \
/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock \
--ros-args -r /scan/points:=/points -r /imu:=/imu/data"

tmux kill-session -t $S 2>/dev/null || true

# 0: Gazebo (headless, T600) -------------------------------------------------
tmux new-session -d -s $S -n gz
tmux send-keys -t $S:gz "$SRC && $OFFLOAD gz sim -v4 -s -r $WORLD" C-m
sleep 6                      # let gz load the world + publish /clock

# 1: ros_gz bridge -----------------------------------------------------------
tmux new-window -t $S -n bridge
tmux send-keys -t $S:bridge "$SRC && $BRIDGE" C-m
sleep 2

# 2: lidar_time_field --------------------------------------------------------
tmux new-window -t $S -n timefield
tmux send-keys -t $S:timefield "$SRC && ros2 run srfv_flight lidar_time_field" C-m
sleep 1

# 3: LIO-SAM -----------------------------------------------------------------
tmux new-window -t $S -n liosam
tmux send-keys -t $S:liosam "$SRC && ros2 launch $WS/srfv_slam.launch.py" C-m
sleep 2

# 4: RViz --------------------------------------------------------------------
tmux new-window -t $S -n rviz
tmux send-keys -t $S:rviz "$SRC && ros2 run rviz2 rviz2 -d $WS/srfv.rviz --ros-args -p use_sim_time:=true" C-m
sleep 2

# 5: ArduCopter SITL (MAVProxy console pops up separately) -------------------
tmux new-window -t $S -n sitl
tmux send-keys -t $S:sitl "cd ~/ardupilot/ArduCopter && sim_vehicle.py -v ArduCopter -f gazebo-iris --model JSON --console" C-m
sleep 12                     # boot + connect to gz + param download

# 6: mavros ------------------------------------------------------------------
tmux new-window -t $S -n mavros
tmux send-keys -t $S:mavros "$SRC && ros2 launch mavros apm.launch fcu_url:=tcp://127.0.0.1:5762" C-m
sleep 6

# 7: teleop (queued, NOT run - press Enter here when ready) ------------------
tmux new-window -t $S -n teleop
tmux send-keys -t $S:teleop "$SRC" C-m
tmux send-keys -t $S:teleop "ros2 run srfv_flight keyboard_teleop --ros-args -p speed:=0.5 -p takeoff_alt:=1.2"

tmux select-window -t $S:teleop
echo "-------------------------------------------------------------"
echo "SRFV stack up in tmux session '$S'."
echo "  Attach:        tmux attach -t $S"
echo "  Switch window: Ctrl-b then 0-7   (7 = teleop)"
echo "  Fly:           go to window 7, press Enter to launch teleop, then WASD/RF/JL"
echo "  Kill all 8:    tmux kill-session -t $S"
echo "-------------------------------------------------------------"
