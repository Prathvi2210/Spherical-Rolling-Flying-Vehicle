#!/usr/bin/env python3
"""
waypoint_sequencer.py  --  SRFV Phase 1, "Drone following A*"
POSITION-TARGET version (single target per waypoint, wait for arrival).

Why this replaces the velocity-streaming version:
  ArduCopter GUIDED accepts a SINGLE position target and flies to it with its own
  internal position controller (clean straight line, then loiter). Position targets
  do NOT time out, so you send ONE per waypoint and wait until you've arrived --
  no hot streaming, no hunting, no 3s GUID_TIMEOUT to fight. This is the pattern
  reference mavros waypoint followers use.

Mechanism (all per ArduPilot docs):
  * /mavros/setpoint_raw/local, mavros_msgs/PositionTarget
  * coordinate_frame = FRAME_LOCAL_NED (1); MAVROS converts the ENU values we
    publish into the NED frame ArduPilot expects -- so we publish ENU here.
  * type_mask = 3576 (position only: ignore vel, accel, yaw, yaw_rate)
  * arrival = Euclidean distance from /mavros/local_position/pose < accept_radius

Frame fix (unchanged, still needed): the planner path is in SLAM 'map' frame, which
is rotated ~90deg from ArduPilot's local frame. We measure the offset live from
/lio_sam/mapping/odometry vs /mavros/local_position/pose, freeze it on the first
path, and transform every waypoint map->ArduPilot ENU before publishing.

Hard-won facts honored: Fact 2 (BEST_EFFORT subs), Fact 3 (setpoint_raw, no TF),
Fact 4 (ENU on ROS side), Fact 5 (arrival check uses mavros pose only; SLAM only
seeds the one-time calibration), Fact 7 (SLAM z unused; altitude held in ArduPilot).

Assumes the drone is already armed + airborne in GUIDED.
"""

import math

import rclpy
from rclpy.node import Node
from rclpy.qos import (
    QoSProfile, ReliabilityPolicy, HistoryPolicy, DurabilityPolicy,
    qos_profile_sensor_data,
)

from nav_msgs.msg import Path, Odometry
from geometry_msgs.msg import PoseStamped
from mavros_msgs.msg import PositionTarget, State


# type_mask = 3576: position only (ignore vel, accel, yaw, yaw_rate).
POSITION_TYPE_MASK = (
    PositionTarget.IGNORE_VX | PositionTarget.IGNORE_VY | PositionTarget.IGNORE_VZ
    | PositionTarget.IGNORE_AFX | PositionTarget.IGNORE_AFY | PositionTarget.IGNORE_AFZ
    | PositionTarget.IGNORE_YAW | PositionTarget.IGNORE_YAW_RATE
)  # = 3576


def yaw_from_quat(x, y, z, w):
    return math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))


def rotate_z(x, y, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (c * x - s * y, s * x + c * y)


def wrap_pi(a):
    return math.atan2(math.sin(a), math.cos(a))


class WaypointSequencer(Node):
    def __init__(self):
        super().__init__("waypoint_sequencer")

        self.declare_parameter("path_topic", "/srfv/global/path")
        self.declare_parameter("pose_topic", "/mavros/local_position/pose")
        self.declare_parameter("slam_odom_topic", "/lio_sam/mapping/odometry")
        self.declare_parameter("setpoint_topic", "/mavros/setpoint_raw/local")
        self.declare_parameter("state_topic", "/mavros/state")
        self.declare_parameter("publish_rate_hz", 10.0)     # re-publish current target
        self.declare_parameter("accept_radius", 0.6)        # 2D arrival, mavros (Fact 5)
        self.declare_parameter("require_guided", True)
        self.declare_parameter("target_altitude", -1.0)     # <0 => hold current mavros z
        self.declare_parameter("yaw_offset_deg", float("nan"))
        self.declare_parameter("ignore_replans", True)      # don't restart on every replan
        self.declare_parameter("hold_ticks", 20)            # how many ticks to latch final hold
        self.declare_parameter("new_goal_tol", 0.5)         # goal must move > this (m) to count as NEW

        g = self.get_parameter
        self.accept_radius = g("accept_radius").value
        self.require_guided = g("require_guided").value
        self.target_alt_param = g("target_altitude").value
        self.yaw_override = g("yaw_offset_deg").value
        self.ignore_replans = g("ignore_replans").value
        self.hold_ticks = int(g("hold_ticks").value)
        self.new_goal_tol = g("new_goal_tol").value
        rate = g("publish_rate_hz").value

        # live state
        self.have_pose = False
        self.cur = (0.0, 0.0, 0.0)
        self.cur_yaw = 0.0
        self.have_slam = False
        self.slam = (0.0, 0.0, 0.0)
        self.slam_yaw = 0.0
        self.armed = False
        self.mode = ""
        self.have_state = False

        # calibration (frozen at first path)
        self.calibrated = False
        self.yaw_offset = 0.0
        self.t = (0.0, 0.0)
        self.yaw_offset_ema = None

        # mission
        self.waypoints = []        # ArduPilot ENU (x, y, z)
        self.idx = 0
        self.finished = False
        self.hold_left = 0
        self.target_alt = None
        self.have_active_path = False
        self.cur_goal = None       # ArduPilot ENU (x, y) of the goal we're following

        self._log_throttle = self.get_clock().now()
        self._warn_throttle = self.get_clock().now()

        sensor_qos = qos_profile_sensor_data
        state_qos = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                               history=HistoryPolicy.KEEP_LAST, depth=5)
        path_qos = QoSProfile(reliability=ReliabilityPolicy.RELIABLE,
                              durability=DurabilityPolicy.VOLATILE,
                              history=HistoryPolicy.KEEP_LAST, depth=10)

        self.pub = self.create_publisher(PositionTarget, g("setpoint_topic").value, 10)
        self.create_subscription(Path, g("path_topic").value, self._on_path, path_qos)
        self.create_subscription(PoseStamped, g("pose_topic").value, self._on_pose, sensor_qos)
        self.create_subscription(Odometry, g("slam_odom_topic").value, self._on_slam, sensor_qos)
        self.create_subscription(State, g("state_topic").value, self._on_state, state_qos)

        self.timer = self.create_timer(1.0 / rate, self._control_loop)

        self.get_logger().info(
            "waypoint_sequencer (POSITION-TARGET, frame-aware) up. "
            "Sends one position target per waypoint, waits for arrival. "
            "Calibrating SLAM->ArduPilot offset; frozen on first path.")

    # callbacks
    def _on_pose(self, msg: PoseStamped):
        p, o = msg.pose.position, msg.pose.orientation
        self.cur = (p.x, p.y, p.z)
        self.cur_yaw = yaw_from_quat(o.x, o.y, o.z, o.w)
        self.have_pose = True
        self._update_calibration_estimate()

    def _on_slam(self, msg: Odometry):
        p, o = msg.pose.pose.position, msg.pose.pose.orientation
        self.slam = (p.x, p.y, p.z)
        self.slam_yaw = yaw_from_quat(o.x, o.y, o.z, o.w)
        self.have_slam = True
        self._update_calibration_estimate()

    def _on_state(self, msg: State):
        self.armed = msg.armed
        self.mode = msg.mode
        self.have_state = True

    def _update_calibration_estimate(self):
        if self.calibrated or not (self.have_pose and self.have_slam):
            return
        off = wrap_pi(self.cur_yaw - self.slam_yaw)
        if self.yaw_offset_ema is None:
            self.yaw_offset_ema = off
        else:
            d = wrap_pi(off - self.yaw_offset_ema)
            self.yaw_offset_ema = wrap_pi(self.yaw_offset_ema + 0.1 * d)

    def _freeze_calibration(self):
        if not math.isnan(self.yaw_override):
            self.yaw_offset = math.radians(self.yaw_override)
        elif self.yaw_offset_ema is not None:
            self.yaw_offset = self.yaw_offset_ema
        else:
            self.get_logger().error(
                "No SLAM/mavros poses yet -- cannot calibrate. "
                "Is /lio_sam/mapping/odometry publishing?")
            return False
        rx, ry = rotate_z(self.slam[0], self.slam[1], self.yaw_offset)
        self.t = (self.cur[0] - rx, self.cur[1] - ry)
        self.calibrated = True
        self.get_logger().info(
            f"FRAME CALIBRATED: yaw_offset = {math.degrees(self.yaw_offset):+.1f} deg, "
            f"t = ({self.t[0]:+.2f}, {self.t[1]:+.2f}) m  (map -> ArduPilot ENU)")
        return True

    def _map_to_ardu(self, mx, my):
        rx, ry = rotate_z(mx, my, self.yaw_offset)
        return (rx + self.t[0], ry + self.t[1])

    def _on_path(self, msg: Path):
        if not msg.poses:
            return
        if not self.have_pose:
            self.get_logger().warn("No mavros pose yet; cannot follow path.")
            return
        if not self.calibrated and not self._freeze_calibration():
            return

        # Transform the incoming goal (final waypoint) into ArduPilot ENU so we can
        # compare it against the goal we're currently following.
        gx_map, gy_map = msg.poses[-1].pose.position.x, msg.poses[-1].pose.position.y
        new_goal = self._map_to_ardu(gx_map, gy_map)

        # Decide whether this path is a NEW goal or just a replan of the current one.
        if self.ignore_replans and self.have_active_path and self.cur_goal is not None:
            moved = math.hypot(new_goal[0] - self.cur_goal[0],
                               new_goal[1] - self.cur_goal[1])
            if moved <= self.new_goal_tol:
                # Same goal, replanned -- ignore (prevents post-arrival loop).
                return
            # else: goal has moved -> fall through and accept as a new mission.
            self.get_logger().info(
                f"New GOAL detected (moved {moved:.2f} m). Re-engaging.")

        if self.target_alt is None:
            self.target_alt = (self.cur[2] if self.target_alt_param < 0.0
                               else self.target_alt_param)

        wps = [self._map_to_ardu(ps.pose.position.x, ps.pose.position.y)
               for ps in msg.poses]
        self.waypoints = [(ax, ay, self.target_alt) for (ax, ay) in wps]
        self.idx = 0
        self.finished = False
        self.hold_left = 0
        self.have_active_path = True
        self.cur_goal = new_goal

        self.get_logger().info(
            f"New path: {len(self.waypoints)} waypoints (ArduPilot ENU, "
            f"alt {self.target_alt:.2f} m). Following with position targets.")
        self.get_logger().info(
            f"  goal ENU = ({self.waypoints[-1][0]:+.2f}, {self.waypoints[-1][1]:+.2f}) "
            f"| drone now = ({self.cur[0]:+.2f}, {self.cur[1]:+.2f})")

    # control loop: publish the CURRENT waypoint as a position target; advance on arrival
    def _control_loop(self):
        if not self.have_pose or not self.waypoints:
            return

        # Finished: re-send the final hold target a bounded number of times so
        # ArduCopter latches it, then stop publishing entirely (release the stream).
        if self.finished:
            if self.hold_left > 0:
                fx, fy, fz = self.waypoints[-1]
                self._send_position(fx, fy, fz)
                self.hold_left -= 1
                if self.hold_left == 0:
                    self.get_logger().info(
                        "Hold target latched -- stream released. Mission complete.")
            return

        if self.require_guided:
            if not self.have_state:
                self._warn("No /mavros/state yet -- not commanding.")
                return
            if self.mode != "GUIDED" or not self.armed:
                self._warn(f"Not commanding: mode={self.mode!r} armed={self.armed} "
                           "(need GUIDED + armed).")
                return

        wx, wy, wz = self.waypoints[self.idx]
        cx, cy, cz = self.cur
        d2 = math.hypot(wx - cx, wy - cy)      # Fact 5: 2D arrival via mavros

        if d2 < self.accept_radius:
            if self.idx >= len(self.waypoints) - 1:
                self.finished = True
                self.hold_left = self.hold_ticks
                self.get_logger().info(
                    "Final waypoint reached. Sending hold target, then releasing stream.")
                self._send_position(wx, wy, wz)
                return
            self.idx += 1
            self.get_logger().info(
                f"Reached waypoint {self.idx}/{len(self.waypoints) - 1}.")
            wx, wy, wz = self.waypoints[self.idx]

        # Send the current waypoint as a single position target (re-sent at rate to
        # keep a fresh setpoint + heartbeat; position targets don't time out).
        self._send_position(wx, wy, wz)
        self._log(f"[wp {self.idx}/{len(self.waypoints) - 1}] "
                  f"d2={d2:.2f}m -> tgt ENU=({wx:+.2f},{wy:+.2f},{wz:+.2f})")

    def _send_position(self, e, n, u):
        """Publish an ENU position target; MAVROS converts ENU->NED for ArduPilot."""
        msg = PositionTarget()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.coordinate_frame = PositionTarget.FRAME_LOCAL_NED
        msg.type_mask = POSITION_TYPE_MASK
        msg.position.x = float(e)   # ENU east  (MAVROS rotates into NED)
        msg.position.y = float(n)   # ENU north
        msg.position.z = float(u)   # ENU up
        self.pub.publish(msg)

    def _log(self, text, period_s=1.0):
        now = self.get_clock().now()
        if (now - self._log_throttle).nanoseconds * 1e-9 >= period_s:
            self.get_logger().info(text)
            self._log_throttle = now

    def _warn(self, text, period_s=3.0):
        now = self.get_clock().now()
        if (now - self._warn_throttle).nanoseconds * 1e-9 >= period_s:
            self.get_logger().warn(text)
            self._warn_throttle = now


def main(args=None):
    rclpy.init(args=args)
    node = WaypointSequencer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
