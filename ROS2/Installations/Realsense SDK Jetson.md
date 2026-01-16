The documentation for installation of Intel Realsense SDK which is the first step in integrating the intel deoth sensors with ros, is confusing.
Here is a concise overview and solution

There are two steps to be followed for connecting an intel depth camera with ROS:
  1) Install the SDK
  2) Install the ROS wrapper
The commands for installing the SDK are jumbled up pretty good, expecially for jetson boards, it is not beginner friendly at all.
Even after following all the pages and the steps it will still install without the realsense-viewer tool.

I have used a Jetson Orin Nano Dev Kit 8gb ram and followed these following commands for a source build:

STEP1: Install the dependencies for ubuntu environment
```bash
sudo apt-get update && sudo apt-get upgrade

#Install the core packages required to build librealsense binaries and the affected kernel modules:
sudo apt-get install libssl-dev libusb-1.0-0-dev libudev-dev pkg-config libgtk-3-dev

# Install build tools
sudo apt-get install git wget cmake build-essential

#Prepare Linux Backend and the Dev. Environment
#Unplug any connected RealSense camera and run:
sudo apt-get install libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev at
```
STEP2: Install librealsense2
```bash
#Clone the librealsense2 repo
git clone https://github.com/realsenseai/librealsense.git

#Run Realsense permissions script from librealsense2 root directory:
cd librealsense
./scripts/setup_udev_rules.sh
```

Till this point the commands were general and working 
