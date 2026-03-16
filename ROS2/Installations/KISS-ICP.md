KISS-ICP is not in the apt repository, it needs to be built from source on Jetson Orin Nano (Jetpack 6.2.1).
```bash
sudo apt install ros-humble-kiss-icp
```
This wont work- Unable to locate package ros-humble-kiss-icp.
Install KISS-ICP from source:
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
Create workspace if not already
```bash
cd ~/ros2_ws/src
git clone https://github.com/PRBonn/kiss-icp.git
```
Build
```bash
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
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
