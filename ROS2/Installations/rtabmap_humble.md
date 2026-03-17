```bash
sudo apt install ros-humble-rtabmap-ros -y
```
This installs the full package including all the dependencies and will take some time.
For source build:
```bash
mkdir -p ~/rtabmap_ws/src
cd ~/rtabmap_ws/src
git clone https://github.com/introlab/rtabmap.git
git clone --branch ros2 https://github.com/introlab/rtabmap_ros.git
cd ..
rosdep update && rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```
