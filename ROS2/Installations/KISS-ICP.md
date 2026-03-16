KISS-ICP is not in the apt repository, it needs to be built from source on Jetson Orin Nano (Jetpack 6.2.1).
```bash
sudo apt install ros-humble-kiss-icp
```
This wont work- Unable to locate package ros-humble-kiss-icp.

Install KISS-ICP from source (for kiss-icp as a Python library):
```bash
#Install dependencies first
sudo apt install python3-pip python3-colcon-common-extensions -y
sudo apt install ros-humble-tf2-eigen ros-humble-tf2-ros -y
pip3 install kiss-icp 
```
On newer pip versions a safety flag is used with: "pip3 install kiss-icp --break-system-packages".
Possibility of Python packaging version incompatibility. kiss-icp 1.2.3 requires a newer version of the packaging library than what's installed.
Python 3.10.12 on Ubuntu 22.04 shipd with packaging 21.3 and required is >=22.0.
Solution: Upgrade packaging and pip:
```bash
pip install --upgrade pip packaging
```
If upgrading packaging fails due to system-managed packages, try forcing it:
```bash
pip install --upgrade --ignore-installed packaging
```
If it still fails, the system packaging(installed via apt) maybe taking precedence. Force pip to use its own:
```bash
pip install --upgrade pip setuptools wheel packaging --break-system-packages
```
Another possible problem here: pip upgraded packaging in the user directory, but the build subprocess is still picking up the system 21.3 version from /usr/lib/python3/dist-packages.
i.e. The isolated build environment pip creates ignores your user-installed packages. 
Fix: use a virtual environment. This gives pip a clean, isolated environment where it fully controls the packaging version.
```bash
sudo apt install python3.10-venv
python3 -m venv ~/kiss_icp_env
source ~/kiss_icp_env/bin/activate
pip install --upgrade pip packaging
pip install kiss-icp
```
If during install, the pybind subdirectory is missing it may be a network/firewall issue on jetson blocking GitHub during the cmake fetch step.
In that case, you can build from source directly, skip the pip install kiss-icp step now and run pip install after cloning the git repo.
```bash
cd kiss-icp
pip install .
```

For KISS-ICP as ROS2 package(which is what I'll be using):
Create workspace if not already
```bash
cd ~/ros2_ws/src
git clone https://github.com/PRBonn/kiss-icp.git
```
Build
```bash
cd ~/ros2_ws
colcon build --packages-select kiss_icp
```
Now Ubuntu 22.04 ships with CMake 3.22.1 but kiss-icp's dependency(Sophus) needs 3.24+. So, you need to install a newer version of CMake manually.
Install CMake 3.28 via Kitware's official repo:
```bash
# Remove old cmake
sudo apt remove cmake -y

# Install kitware repo key
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null

# Add the repo (Ubuntu 22.04 = jammy)
echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null

# Update and install
sudo apt update
sudo apt install cmake -y

# Verify
cmake --version
```
This can remove the existing ROS packages, check the ROS installation and repair/reinstall it if required.
My system was missing ament_cmake, geometry_msgs, RMW(ROS middleware), ROS2 binary. Fix:
```bash
sudo apt install ros-humble-ament-cmake ros-humble-ament-cmake-core
sudo apt install ros-humble-geometry-msgs ros-humble-nav-msgs ros-humble-rclcpp ros-humble-rclcpp-components ros-humble-rcutils ros-humble-sensor-msgs ros-humble-std-msgs ros-humble-tf2-ros ros-humble-std-srvs
sudo apt install ros-humble-rmw-cyclonedds-cpp
echo "export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp" >> ~/.bashrc
sudo apt install ros-humble-ros2cli ros-humble-desktop
```
Now retry colcon build.
If colcon can't find the package.xml for kiss-icp it will complete the colcon build with zero packages. 
Check what colcon actually sees:
```bash
colcon list
```
If this gives no output, as in my case, colcon ROS2 package identification extension was missing. It needs colcon-ros:
```bash
sudo apt install python3-colcon-ros python3-colcon-common-extensions
```
Source Workspace
```bash
echo "source ~/ros2_ws/install/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

Verify Build Succeeded
```bash
ros2 pkg list | grep kiss
```
Should return: kiss_icp.
