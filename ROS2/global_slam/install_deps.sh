#!/usr/bin/env bash
# =============================================================================
#  install_deps.sh
#  One-shot dependency installer for global_slam on Jetson Orin Nano Super
#  (Ubuntu 22.04 Jammy + ROS 2 Humble)
#
#  Run ONCE before your first colcon build:
#      chmod +x scripts/install_deps.sh
#      ./scripts/install_deps.sh
# =============================================================================
set -euo pipefail

BOLD='\033[1m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
section() { echo -e "\n${BOLD}══ $* ══${NC}"; }

# ── Sanity checks ──────────────────────────────────────────────────────────────
section "Environment check"
if [[ "$(uname -m)" != "aarch64" ]]; then
    warn "Not running on aarch64. This script targets Jetson Orin Nano (ARM)."
fi

ROS_DISTRO=${ROS_DISTRO:-humble}
info "ROS_DISTRO = ${ROS_DISTRO}"

# ── System packages ────────────────────────────────────────────────────────────
section "System packages"
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    libeigen3-dev \
    libpcl-dev \
    libboost-all-dev \
    cmake \
    build-essential \
    git

# ── ROS 2 PCL packages ────────────────────────────────────────────────────────
section "ROS 2 PCL packages"
sudo apt-get install -y --no-install-recommends \
    ros-${ROS_DISTRO}-pcl-conversions \
    ros-${ROS_DISTRO}-pcl-ros

# ── GTSAM ─────────────────────────────────────────────────────────────────────
section "GTSAM"
if dpkg -l libgtsam-dev 2>/dev/null | grep -q "^ii"; then
    info "libgtsam-dev already installed ($(dpkg -s libgtsam-dev | grep Version))."
else
    info "Attempting apt install of libgtsam-dev..."
    if sudo apt-get install -y libgtsam-dev 2>/dev/null; then
        info "GTSAM installed via apt."
    else
        warn "apt install failed — building GTSAM 4.2 from source (takes ~10 min on Jetson)."
        build_gtsam_from_source
    fi
fi

build_gtsam_from_source() {
    local GTSAM_VERSION="4.2.0"
    local GTSAM_DIR="/tmp/gtsam_build"
    mkdir -p "${GTSAM_DIR}" && cd "${GTSAM_DIR}"

    if [[ ! -d "gtsam" ]]; then
        git clone --depth 1 --branch "${GTSAM_VERSION}" \
            https://github.com/borglab/gtsam.git
    fi

    cd gtsam
    mkdir -p build && cd build
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DGTSAM_BUILD_TESTS=OFF \
        -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF \
        -DGTSAM_USE_SYSTEM_EIGEN=ON \
        -DGTSAM_BUILD_WITH_MARCH_NATIVE=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local

    make -j"$(nproc)"
    sudo make install
    sudo ldconfig
    info "GTSAM ${GTSAM_VERSION} built and installed to /usr/local."
    cd /
}

# ── Verify ─────────────────────────────────────────────────────────────────────
section "Verification"
echo "  Eigen  : $(pkg-config --modversion eigen3 2>/dev/null || echo 'not via pkg-config — check /usr/include/eigen3')"
echo "  PCL    : $(dpkg -s libpcl-dev 2>/dev/null | grep Version || echo 'MISSING')"
echo "  GTSAM  : $(dpkg -s libgtsam-dev 2>/dev/null | grep Version \
                   || (ls /usr/local/lib/libgtsam* 2>/dev/null | head -1) \
                   || echo 'MISSING')"
echo "  ROS2   : ${ROS_DISTRO}"

# ── Build instructions ─────────────────────────────────────────────────────────
section "Next steps"
cat <<'EOF'
  1.  Source ROS 2:
          source /opt/ros/humble/setup.bash

  2.  Build the package:
          cd ~/ros2_ws
          colcon build --packages-select global_slam \
              --cmake-args -DCMAKE_BUILD_TYPE=Release \
              --parallel-workers 4

  3.  Source the workspace:
          source install/setup.bash

  4.  Launch (Kiss-ICP must already be running):
          ros2 launch global_slam global_slam.launch.py

  5.  Visualize in RViz:
          Topics to add:
            /gtsam/map_cloud   → PointCloud2  (frame: odom)
            /gtsam/path        → Path         (frame: odom)
            /gtsam/odometry    → Odometry
EOF
