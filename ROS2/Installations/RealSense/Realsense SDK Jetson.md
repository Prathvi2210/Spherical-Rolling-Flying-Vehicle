The documentation for installation of Intel Realsense SDK which is the first step in integrating the intel depth sensors with ros, is confusing.
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
git checkout v2.56.4
#Apply kernel patches
sudo ./scripts/setup_udev_rules.sh
sudo ./scripts/patch-realsense-ubuntu-L4T.sh
```

Till this point the commands were general and working on Ubuntu kernels. 
But that stops here, Jetson boards use custom kernels 'tegra' and patching kernels like we do on normal ubuntu computers can break them.
Here we skip that step and move to build
```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=true -DCMAKE_BUILD_TYPE=release -DFORCE_RSUSB_BACKEND=true -DBUILD_GRAPHICAL_EXAMPLES=true -DBUILD_WITH_CUDA=true -DFORCE_LIBUVC=true -DBUILD_PYTHON_BINDINGS=true 

make -j$(nproc)
sudo make install
sudo Idconfig
```

Verify:
```bash
realsense-viewer
```
In the provided documentation, -DBUILD_GRAPHICAL_EXAMPLES is not included also -DFORCE_RSUSB_BACKEND is set to false 
But for visualization on the realsense-viewer it is needed because:
It forces pure USB userspace backend
It avoids kernel module dependencies

Now we can proceeed to install the ros wrapper via the git repo and integrate camera output in ROS topics

XXX Troubleshooting XXX
To verify camera connections, version comaptibility problems, drivers, USB persmissions etc:
1) To confirm the camera is physically visible to the OS:
```bash
lsusb | grep 8086
```
expected>>> Intel Corp. Intel(R) RealSense(TM)
```bash
lsusb -t
```
expected under USB3 (5000M)
XXX DRIVERS ISSUE START XXX
All driver should have values, shouldn't be empty
Driver=uvcvideo   (for video)
Driver=usbhid     (for HID)
If there is noting in front of 'Driver=' that means the kernel did NOT bind the RealSense interfaces to any driver
Cause: Sometimes jetson will enumerate an USB device without first binding it
Without this the next command wont recognize the camera
First pinpoint why kernel not bound to drivers:
Check is uvcvideo is loaded
```bash
lsmod | grep uvcvideo
```
If no output here, we need to manually load it
```bash
sudo modprobe uvcvideo
```
unplug the camera and replug it and check again
```bash
lsusb -t
```
Expected output
```bash
Driver=uvcvideo
```
if lsmod | grep uvcvideo showed output directly come here:
We need to forcefully unbind and rebind the USB device
Identify the bus+device path. It will look like: Bus 002 Device 009
Now unbind it:
```bash
echo '2-1.3' | sudo tee /sys/bus/usb/drivers/usb/unbind
```
Now rebind it
```bash
echo '2-1.3' | sudo tee /sys/bus/usb/drivers/usb/bind
```
Now check with
```bash
lsusb -t
```
If still drivers are not visible proceed to hard reset: shutdown, unplug and wait for 1-2 mins

The drivers should look like
```bash
Class=Video, Driver=uvcvideo, 5000M
Class=Human Interface Device, Driver=usbhid, 5000M
```
Only then proceed
XXX DRIVERS ISSUE END XXX


```bash
v4l2-ctl --list-devices
```
expected>>> Intel(R) RealSense(TM) Depth Camera
  /dev/video*

If any of the above three fail: it is a hardware/cable/port problem. Linux can't see or recognize the camera

2) Apply ROS librealsense udev rules correctly:
If ROS-binary librealsense is installed, DO NOT use librealsense GitHub udev script
Verify ROS rules exist
```bash
ls /etc/udev/rules.d | grep realsense
```
expected>>> 99-realsense-libusb.rules
If this is missing, reinstall rules via ROS package
```bash
sudo apt reinstall ros-humble-librealsense2
```
Then reload the udev:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

3) Testing SDK before moving to ROS
```bash
rs-enumerate-devices
```
The camera must be listed here with its properties

4) If realsense-viewer fails, this is failure at the SDK stage
Verify permissions
```bash
ls -l /dev/hidraw*
```
expected>>> root plugdev
If not set:
```bash
sudo usermod -aG plugdev,video $USER
```
Logout/ Login
