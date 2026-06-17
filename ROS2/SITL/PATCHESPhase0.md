# SRFV Phase 0 / 0.5 — Reproducibility Notes

End-to-end record for standing up the SRFV simulation foundation: ArduCopter SITL
+ Gazebo Harmonic (via `ardupilot_gazebo` + mavros) + simulated VLP-16 + LIO-SAM,
flown both autonomously (`guided_waypoint`) and by keyboard (`keyboard_teleop`) in
a furnished indoor world. Everything below is what deviates from a stock install.

---

## 1. Stack / versions

| Component | Version |
|---|---|
| OS | Ubuntu 22.04 |
| ROS 2 | Humble |
| Gazebo | Harmonic (gz-sim 8) |
| gz <-> ROS | `ros-humble-ros-gzharmonic` (`ros_gz_bridge`) |
| SITL <-> gz | `ardupilot_gazebo` plugin (NOT ardupilot_gz/DDS — keeps mavros as the bridge) |
| Autopilot | ArduCopter SITL **4.7** (SI-unit params — see §5) |
| SLAM | LIO-SAM (`TixiaoShan/LIO-SAM`, **ros2** branch) |
| GTSAM | 4.1 (`ppa:borglab/gtsam-release-4.1`) |
| Workspace | `~/srfv_ws` (overlay; `source ~/srfv_ws/install/setup.bash`) |

Hardware target: laptop "Prime" — i7 12th gen, NVIDIA T600 4 GB, 16 GB RAM.

## 2. Environment (`~/.bashrc`)

```bash
source /opt/ros/humble/setup.bash
export GZ_VERSION=harmonic
export GZ_SIM_SYSTEM_PLUGIN_PATH=$HOME/ardupilot_gazebo/build
export GZ_SIM_RESOURCE_PATH=$HOME/ardupilot_gazebo/models:$HOME/ardupilot_gazebo/worlds
# house models (added in §6):
export GZ_SIM_RESOURCE_PATH=$HOME/aws-robomaker-small-house-world/models:$GZ_SIM_RESOURCE_PATH
```

Optional 8 GB swap as OOM insurance for heavy builds/runs (not in fstab, won't
survive reboot):
```bash
sudo fallocate -l 8G /swapfile2 && sudo chmod 600 /swapfile2 && sudo mkswap /swapfile2 && sudo swapon /swapfile2
```

## 3. LIO-SAM source patches (3)

Cloned to `~/srfv_ws/src/LIO-SAM`. Rebuild after editing:
```bash
MAKEFLAGS="-j1" colcon build --packages-select lio_sam \
  --cmake-args -DCMAKE_BUILD_TYPE=Release --executor sequential
```

Two TF broadcasts copied only rotation/translation via `tf2::convert(...)` and
never set `header.frame_id`, producing `TF_NO_FRAME_ID` spam and an empty parent.
A third was a duplicate `odom -> lidar_link` publisher causing `TF_OLD_DATA`.

- **`imuPreintegration.cpp`** — before `ts.child_frame_id = baselinkFrame;` insert:
  ```cpp
  ts.header.frame_id = odometryFrame;
  ```
- **`mapOptmization.cpp`** (~line 1660; note the file is misspelled, no "i") —
  before `trans_odom_to_lidar.child_frame_id = "lidar_link";` insert:
  ```cpp
  trans_odom_to_lidar.header.frame_id = odometryFrame;
  ```
- **`imuPreintegration.cpp`** — comment out the duplicate broadcast so only
  mapOptimization publishes the authoritative `odom -> lidar_link`:
  ```cpp
  // tfBroadcaster->sendTransform(ts);
  ```

## 4. LIO-SAM `config/params.yaml` (key settings)

Edit `~/srfv_ws/src/LIO-SAM/config/params.yaml`, then **copy to the install share
and relaunch — no rebuild** (params load at runtime):
```bash
cp ~/srfv_ws/src/LIO-SAM/config/params.yaml \
   ~/srfv_ws/install/lio_sam/share/lio_sam/config/params.yaml
```

| Key | Value | Note |
|---|---|---|
| `use_sim_time` | `true` | |
| `pointCloudTopic` | `/lio_points` | output of `lidar_time_field` |
| `imuTopic` | `/imu/data` | |
| `sensor` | `velodyne` | VLP-16 ring structure |
| `N_SCAN` | `16` | |
| `Horizon_SCAN` | `900` | |
| extrinsics (Trans/Rot) | identity | IMU co-located with LiDAR on `lidar_link` |
| `lidarFrame` | `lidar_link` | |
| `baselinkFrame` | `lidar_link` | **must equal lidarFrame** — `base_link` caused divergence |
| `odometryFrame` | `odom` | |
| `mapFrame` | `map` | |
| `globalMapVisualizationPoseDensity` | `1.0` | denser global map in RViz |
| `globalMapVisualizationLeafSize` | `0.2` | |

Do **not** use LIO-SAM's bundled `run.launch.py` — its reference URDF injects
phantom frames and a bad base->lidar extrinsic. Use `srfv_slam.launch.py`
(standalone: the 4 LIO-SAM nodes + a static `map->odom` TF, `use_sim_time`,
no robot_state_publisher / URDF / rviz).

RViz QoS (different per topic): map cloud `/lio_sam/mapping/map_global` =
**Reliable**; odometry `/lio_sam/mapping/odometry` = **Best Effort**. Run RViz with
`use_sim_time:=true`.

## 5. ArduCopter 4.7 SI-unit parameter renames

4.7 renamed/rescaled speed params to SI (m, m/s). The `WPNAV_` group moved to the
`WP_` prefix; values are now m/s, not cm/s. Set these in the MAVProxy console
**after** `param fetch` completes (typing before the download finishes gives a
false "Unable to find"):

| Old (cm-based) | New 4.7 (SI) | Value set |
|---|---|---|
| `WPNAV_SPEED` | `WP_SPD` | `1.5` (m/s) |
| `WPNAV_SPEED_UP` | `WP_SPD_UP` | `0.75` |
| `WPNAV_SPEED_DN` | `WP_SPD_DN` | `0.75` |
| `WPNAV_ACCEL` | `WP_ACC` | `1.0` (m/s^2) |
| `RTL_ALT` | `RTL_ALT_M` | `2.0` (m — keep RTL inside the feature zone) |
| `RTL_ALT_FINAL` | `RTL_ALT_FINAL_M` | `1.5` (hover) or `0` (land) |
| `RTL_SPEED` | `RTL_SPEED_MS` | `0` -> inherits `WP_SPD` |

GUIDED position targets run through the WP-nav controller, so `WP_SPD` caps
`guided_waypoint`. `guided_waypoint`'s own `target_x/y` and `takeoff_alt` are
MAVLink setpoints in **meters** and are unaffected by these renames.

## 6. AWS small-house environment (Gazebo Harmonic port)

The house gives a feature-rich, attractive indoor map. It is Gazebo-Classic-era,
so it needs a port. Helper scripts: `fix_house_models.py`, `build_iris_house.py`,
`trim_house.py`.

```bash
# 1. assets
git clone --depth 1 -b ros2 \
  https://github.com/aws-robotics/aws-robomaker-small-house-world.git ~/aws-robomaker-small-house-world
# 2. register models (also add the export to ~/.bashrc — see §2)
export GZ_SIM_RESOURCE_PATH=$HOME/aws-robomaker-small-house-world/models:$GZ_SIM_RESOURCE_PATH
# 3. Harmonic makes invalid inertia FATAL (Error 19); fix every model + set static
python3 fix_house_models.py ~/aws-robomaker-small-house-world/models
# 4. merge house geometry into the working drone world (drops runway + srfv_ corridor)
python3 build_iris_house.py \
  ~/ardupilot_gazebo/worlds/iris_indoor.sdf \
  ~/aws-robomaker-small-house-world/worlds/small_house.world \
  ~/ardupilot_gazebo/worlds/iris_house.sdf
# 5. trim to ~2 rooms around the drone (keeps floor + exterior shell)
python3 trim_house.py \
  ~/ardupilot_gazebo/worlds/iris_house.sdf \
  ~/ardupilot_gazebo/worlds/iris_house_trim.sdf 4 4
```

Notes:
- `Could not resolve file [.../photos/Portrait*.jpg]` warnings are **cosmetic**
  (picture-frame textures only). LiDAR reads geometry, not textures — ignore.
- Render Gazebo on the T600, not the Intel iGPU, via PRIME offload:
  `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia gz sim ...`
- The stack is world-decoupled: swap environments with the launch `world:=` arg
  (corridor `iris_indoor.sdf` or tunnel tiles for heavy/fast SLAM runs).

## 7. `srfv_flight` nodes

- **`guided_waypoint`** — GUIDED -> arm -> takeoff -> fly to an ENU waypoint
  (`/mavros/setpoint_position/local`); params `target_x/target_y/takeoff_alt/arrival_radius`.
- **`lidar_time_field`** — subscribes `/points`, adds per-point `time` field
  (float32 = column/width x 0.1 s; `ring` already present), republishes `/lio_points`.
  (The 0.1 s assumes a 10 Hz lidar; change if the sensor rate changes.)
- **`keyboard_teleop`** — manual GUIDED **body-frame** velocity teleop on
  `/mavros/setpoint_raw/local` (FRAME_BODY_NED). W/S fwd/back, A/D left/right,
  R/F up/down, J/L yaw, SPACE hover, G land+quit; auto-hover on key release.
  Params: `speed`, `climb`, `yaw_rate`, `takeoff_alt`, `takeoff_wait`, `cmd_timeout`.

## 8. Bringup

```bash
source ~/srfv_ws/install/setup.bash
# 1) sim + perception + SLAM + viz (one command):
ros2 launch ~/srfv_ws/srfv_sim.launch.py
# 2) SITL (own terminal; no -w, keeps frame config):
cd ~/ardupilot/ArduCopter && sim_vehicle.py -v ArduCopter -f gazebo-iris --model JSON --console
# 3) mavros (MAVProxy holds 5760, so use 5762):
ros2 launch mavros apm.launch fcu_url:="tcp://127.0.0.1:5762"
# 4) fly — manual or autonomous:
ros2 run srfv_flight keyboard_teleop --ros-args -p speed:=0.5 -p takeoff_alt:=1.2
ros2 run srfv_flight guided_waypoint --ros-args -p target_x:=1.0 -p target_y:=0.0 -p takeoff_alt:=1.2
```
gz topics if the bridge sees no data: `gz topic -l | grep -E 'points|imu'`.
RTF check: `gz topic -e -t /world/iris_house/stats | grep real_time_factor`.

## 9. Performance / known limitations

- **RTF:** Gazebo free-running (no SITL) renders the lidar against the detailed
  house at ~0.45–0.5x. With **SITL lockstep** the sim is paced by the autopilot and
  holds ~1.0 (real-time). Headless `-s` + T600 offload + trimming to ~2 rooms keeps
  it comfortable on this hardware. Trimming furniture barely moves RTF — the cost is
  the per-scan lidar render of the (kept) floor/shell, not furniture count.
- **Memory:** full stack ~4 GB used of 16 GB; 8 GB temp swap kept as insurance.
- **SLAM quality:** featureless/degenerate spaces (a bare corridor) cause LIO-SAM
  drift. The feature-rich house mitigates this and yields a good map. If the lidar
  render ever needs to be cheaper, the levers are (in order of preference for map
  quality): lighter world (corridor/tiles) > fewer lidar samples > lower lidar rate
  (last resort — also requires updating `lidar_time_field`'s 0.1 s sweep constant).
