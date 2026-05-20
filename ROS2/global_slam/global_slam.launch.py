"""
global_slam.launch.py

Launches the GlobalSlamNode.

Usage:
    ros2 launch global_slam global_slam.launch.py
    ros2 launch global_slam global_slam.launch.py \
        keyframe_dist_m:=0.5 icp_fitness_threshold:=0.30
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():

    # ── Parameter file ─────────────────────────────────────────────────────────
    params_file = PathJoinSubstitution(
        [FindPackageShare("global_slam"), "config", "params.yaml"]
    )

    # ── Optional CLI overrides ─────────────────────────────────────────────────
    declare_kf_dist = DeclareLaunchArgument(
        "keyframe_dist_m", default_value="0.30",
        description="Keyframe distance threshold (m)")

    declare_kf_angle = DeclareLaunchArgument(
        "keyframe_angle_deg", default_value="10.0",
        description="Keyframe angular threshold (deg)")

    declare_icp = DeclareLaunchArgument(
        "icp_fitness_threshold", default_value="0.25",
        description="ICP fitness score acceptance threshold")

    # ── Node ───────────────────────────────────────────────────────────────────
    global_slam_node = Node(
        package    = "global_slam",
        executable = "global_slam_node",
        name       = "global_slam_node",
        output     = "screen",
        emulate_tty = True,

        parameters = [
            params_file,
            {
                # CLI overrides win over the yaml file
                "keyframe_dist_m":       LaunchConfiguration("keyframe_dist_m"),
                "keyframe_angle_deg":    LaunchConfiguration("keyframe_angle_deg"),
                "icp_fitness_threshold": LaunchConfiguration("icp_fitness_threshold"),
            }
        ],

        remappings = [
            # ── Inputs ──────────────────────────────────────────────────────────
            ("/kiss_icp/local_map",  "/kiss_icp/local_map"),
            ("/kiss_icp/odometry",   "/kiss_icp/odometry"),
            ("/cs20/pointcloud",     "/cs20/pointcloud"),
            # ── Outputs ─────────────────────────────────────────────────────────
            ("/gtsam/odometry",      "/gtsam/odometry"),
            ("/gtsam/map_cloud",     "/gtsam/map_cloud"),
            ("/gtsam/path",          "/gtsam/path"),
        ],
    )

    return LaunchDescription([
        declare_kf_dist,
        declare_kf_angle,
        declare_icp,
        global_slam_node,
    ])
