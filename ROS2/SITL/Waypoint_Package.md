This is guided_waypoint. It's a 10 Hz state machine: wait for FCU connect → request the position stream → set GUIDED → arm → takeoff → fly to an ENU waypoint → hold on arrival.
It self-requests LOCAL_POSITION_NED, so it doesn't depend on you having run the manual set_message_interval call (handy after a mavros restart).
Scaffold the package (this also wires the entry point and package.xml deps for you):
```bash
cd ~/srfv_ws/src
ros2 pkg create --build-type ament_python \
  --dependencies rclpy geometry_msgs mavros_msgs \
  --node-name guided_waypoint srfv_flight
```
Overwrite the generated stub with the actual node:
```bash
cat > ~/srfv_ws/src/srfv_flight/srfv_flight/guided_waypoint.py << 'EOF'
#!/usr/bin/env python3
import math
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from geometry_msgs.msg import PoseStamped
from mavros_msgs.msg import State
from mavros_msgs.srv import SetMode, CommandBool, CommandTOL, MessageInterval


class GuidedWaypoint(Node):
    def __init__(self):
        super().__init__('guided_waypoint')

        # ---- parameters (override with --ros-args -p name:=value) ----
        self.declare_parameter('target_x', 5.0)        # metres East (ENU)
        self.declare_parameter('target_y', 0.0)        # metres North
        self.declare_parameter('takeoff_alt', 5.0)     # metres Up
        self.declare_parameter('arrival_radius', 0.5)  # metres
        self.tx = self.get_parameter('target_x').value
        self.ty = self.get_parameter('target_y').value
        self.alt = self.get_parameter('takeoff_alt').value
        self.radius = self.get_parameter('arrival_radius').value

        # best-effort subs are compatible with any publisher (avoids QoS mismatch)
        sub_qos = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                             history=HistoryPolicy.KEEP_LAST, depth=10)

        self.state = State()
        self.pose = PoseStamped()
        self.have_pose = False
        self.create_subscription(State, '/mavros/state', self.state_cb, sub_qos)
        self.create_subscription(PoseStamped, '/mavros/local_position/pose',
                                 self.pose_cb, sub_qos)

        # reliable publisher is compatible with any subscriber
        self.sp_pub = self.create_publisher(
            PoseStamped, '/mavros/setpoint_position/local', 10)

        self.set_mode_cli = self.create_client(SetMode, '/mavros/set_mode')
        self.arm_cli = self.create_client(CommandBool, '/mavros/cmd/arming')
        self.takeoff_cli = self.create_client(CommandTOL, '/mavros/cmd/takeoff')
        self.interval_cli = self.create_client(
            MessageInterval, '/mavros/set_message_interval')

        self.phase = 'WAIT_CONNECT'
        self.req_pending = False
        self.takeoff_sent = False
        self.interval_requested = False
        self.create_timer(0.1, self.tick)  # 10 Hz
        self.get_logger().info('guided_waypoint started; waiting for FCU')

    def state_cb(self, msg):
        self.state = msg

    def pose_cb(self, msg):
        self.pose = msg
        self.have_pose = True

    def _req_done(self, _):
        self.req_pending = False

    def request_local_pos_stream(self):
        if not self.interval_cli.service_is_ready():
            return
        req = MessageInterval.Request()
        req.message_id = 32        # LOCAL_POSITION_NED
        req.message_rate = 10.0    # Hz
        self.interval_cli.call_async(req)
        self.interval_requested = True
        self.get_logger().info('requested LOCAL_POSITION_NED @ 10 Hz')

    def call_set_mode(self, mode):
        if self.req_pending or not self.set_mode_cli.service_is_ready():
            return
        req = SetMode.Request()
        req.base_mode = 0
        req.custom_mode = mode
        self.req_pending = True
        self.set_mode_cli.call_async(req).add_done_callback(self._req_done)

    def call_arm(self, value):
        if self.req_pending or not self.arm_cli.service_is_ready():
            return
        req = CommandBool.Request()
        req.value = value
        self.req_pending = True
        self.arm_cli.call_async(req).add_done_callback(self._req_done)

    def call_takeoff(self, alt):
        if not self.takeoff_cli.service_is_ready():
            return
        req = CommandTOL.Request()
        req.min_pitch = 0.0
        req.yaw = 0.0
        req.latitude = 0.0
        req.longitude = 0.0
        req.altitude = float(alt)
        self.takeoff_cli.call_async(req)
        self.takeoff_sent = True
        self.get_logger().info(f'takeoff sent (alt={alt} m)')

    def publish_setpoint(self, x, y, z):
        sp = PoseStamped()
        sp.header.stamp = self.get_clock().now().to_msg()
        sp.header.frame_id = 'map'
        sp.pose.position.x = float(x)
        sp.pose.position.y = float(y)
        sp.pose.position.z = float(z)
        sp.pose.orientation.w = 1.0
        self.sp_pub.publish(sp)

    def tick(self):
        if self.phase == 'WAIT_CONNECT':
            if self.state.connected and not self.interval_requested:
                self.request_local_pos_stream()
            if self.interval_requested:
                self.get_logger().info('connected -> setting GUIDED')
                self.phase = 'SET_GUIDED'

        elif self.phase == 'SET_GUIDED':
            if self.state.mode == 'GUIDED':
                self.get_logger().info('mode=GUIDED -> arming')
                self.phase = 'ARM'
            else:
                self.call_set_mode('GUIDED')

        elif self.phase == 'ARM':
            if self.state.armed:
                self.get_logger().info('armed -> takeoff')
                self.phase = 'TAKEOFF'
            else:
                self.call_arm(True)

        elif self.phase == 'TAKEOFF':
            if not self.takeoff_sent:
                self.call_takeoff(self.alt)
            elif self.have_pose and self.pose.pose.position.z >= self.alt - 0.3:
                self.get_logger().info(
                    f'reached {self.pose.pose.position.z:.2f} m -> waypoint '
                    f'({self.tx:.1f}, {self.ty:.1f}, {self.alt:.1f})')
                self.phase = 'GO_WAYPOINT'

        elif self.phase == 'GO_WAYPOINT':
            self.publish_setpoint(self.tx, self.ty, self.alt)
            if self.have_pose:
                p = self.pose.pose.position
                if math.dist((p.x, p.y, p.z),
                             (self.tx, self.ty, self.alt)) < self.radius:
                    self.get_logger().info('WAYPOINT REACHED -- holding')
                    self.phase = 'HOLD'

        elif self.phase == 'HOLD':
            self.publish_setpoint(self.tx, self.ty, self.alt)


def main():
    rclpy.init()
    node = GuidedWaypoint()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
EOF
```
Build and run while gazebo, SITL and mavros are running
```bash
cd ~/srfv_ws
colcon build --packages-select srfv_flight
source install/setup.bash
ros2 run srfv_flight guided_waypoint
```
