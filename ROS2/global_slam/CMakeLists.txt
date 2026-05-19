cmake_minimum_required(VERSION 3.8)
project(global_slam)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
# -O3 on all platforms; -march=native only on the Jetson (aarch64)
add_compile_options(-O3 -Wall -Wextra)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  message(STATUS "[global_slam] Jetson/ARM detected — enabling -march=native")
  add_compile_options(-march=native -mtune=native)
endif()

# ── ROS 2 dependencies ────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp       REQUIRED)
find_package(sensor_msgs  REQUIRED)
find_package(nav_msgs     REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(pcl_ros      REQUIRED)

# ── System dependencies ───────────────────────────────────────────────────────
find_package(Eigen3 REQUIRED)
find_package(PCL    REQUIRED COMPONENTS common io filters registration)

# GTSAM — prefer cmake config, fall back to pkg-config
find_package(GTSAM QUIET)
if(NOT GTSAM_FOUND)
  message(WARNING
    "[global_slam] GTSAM cmake config not found. "
    "Trying pkg-config / manual path.\n"
    "Run scripts/install_deps.sh first, or set GTSAM_DIR.")
  find_library(GTSAM_LIB gtsam REQUIRED)
  find_path(GTSAM_INCLUDE_DIR gtsam/geometry/Pose3.h
            HINTS /usr/local/include /usr/include)
  set(GTSAM_LIBRARIES ${GTSAM_LIB})
endif()

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(global_slam_node
  src/global_slam_node.cpp
)

target_include_directories(global_slam_node
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${EIGEN3_INCLUDE_DIR}
    ${PCL_INCLUDE_DIRS}
    ${GTSAM_INCLUDE_DIR}
)

target_link_libraries(global_slam_node
  ${PCL_LIBRARIES}
  ${GTSAM_LIBRARIES}
  Eigen3::Eigen
)

ament_target_dependencies(global_slam_node
  rclcpp
  sensor_msgs
  nav_msgs
  geometry_msgs
  pcl_conversions
  pcl_ros
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS global_slam_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_package()
