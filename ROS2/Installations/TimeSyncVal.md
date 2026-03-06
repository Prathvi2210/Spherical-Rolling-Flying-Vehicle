After installing ROS distro and before moving to hardware level installations we need to verify that the system is capable of time synchronization.
More specifically we need to validate use_sim_time
1) Run a node:
   In terminal 1
   ```bash
   ros2 run demo_nodes_cpp talker
   ```
2) List Nodes:
   In terminal 2
   ```bash
   ros2 node list
   ```
   expected: /talker
3) Query use_sim_time on THAT node
   ```bash
   ros2 param get /talker use_sim_time
   ```
   Expected: Boolean Value is False

Importance: ROS2 supports 2 time sources(clocks)
System time where /clock is not used
and Sim time where /clock is used
The False output means nodes use system wall clock which is good for harware sensors and SLAM

/clock topic should not exist or else it will interfere and may break SLAM

Extra Validation: Check ROS time vs System time
```bash
ros2 topic echo /rosout | head
```
Then compare with
```bash
date +%s
```
Timestamp should match system epoch time, not zero or frozen

Avoid setting ROS_USE_SIM_TIME=true in .bashrc globally

TO validate Sensor message timestamps:
```bash
ros2 topic echo <topic_name> --once
```
Check
```Plain text
header:
  stamp:
    sec: 1705xxxxxx
    nanosec: xxxxxxxx
```
These values must be non-zero, close to system time and increasing over time
Check monotonisity:
```bash
ros2 topic echo <topic_name> --field header.stamp
```

Check ROS time vs sensor time drift- difference
Check IMU vs sensor alignment
