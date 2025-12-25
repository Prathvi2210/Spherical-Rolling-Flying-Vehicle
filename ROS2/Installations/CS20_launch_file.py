#Launching RTABMap (depth only mode) with Synexens CS20 and static transform links of robot base and false odom
#Remember to create a ros package too as ROS2 cannot launch loose files: 
#ros2 pkg create rtab_custom_launch --build-type ament_python
#Also the setup.py file in the created pkg should be checked to include launch files, imp check

#after creating package and launch file need to build the workspace again with colcon build

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():

    return LaunchDescription([

        # --------------------------------------------------
        # 1. Static TF: odom -> base_link
        # (Fake odometry frame for now)
        # --------------------------------------------------
        Node(
            package='tf2_ros',
            executable='static_transform_publisher',
            name='odom_to_base',
            arguments=['0', '0', '0', '0', '0', '0', 'odom', 'base_link']
        ),

        # --------------------------------------------------
        # 2. Static TF: base_link -> depth_camera_link
        # (Camera mounting frame)
        # --------------------------------------------------
        Node(
            package='tf2_ros',
            executable='static_transform_publisher',
            name='base_to_camera',
            arguments=['0', '0', '0', '0', '0', '0',
                       'base_link', 'depth_camera_link']
        ),

        # --------------------------------------------------
        # 3. Fake Odometry Publisher
        # (Required by RTAB-Map)
        # --------------------------------------------------
        Node(
            package='nav_msgs',
            executable='fake_odometry_publisher',
            name='fake_odom',
            output='screen'
        ),

        # --------------------------------------------------
        # 4. RTAB-Map (Depth-only mode)
        # --------------------------------------------------
        Node(
            package='rtabmap_slam',
            executable='rtabmap',
            name='rtabmap',
            output='screen',
            parameters=[{
                'frame_id': 'base_link',
                'subscribe_depth': True,
                'subscribe_rgb': False,
                'subscribe_rgbd': False,
                'approx_sync': True,
                'topic_queue_size': 30,
                'sync_queue_size': 30,
            }],
            remappings=[
                ('depth/image', '/camera1_HV0121115C0539/depth_raw'),
                ('depth/camera_info', '/camera1_HV0121115C0539/depth_info'),
                ('odom', '/odom'),
            ]
        ),
    ])
