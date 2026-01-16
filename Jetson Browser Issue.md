The NVIDIA Jetson Orin Nano Super Dev Pack faces a snap issue for browsers
I had a dev kit with UEFI version <36.0
Basically the 36 means it can run Jetpack 6.x versions which have the MAXN SUPER modes giving 67 TOPS
For the older versions like mine, where jetpack 6.x versions can't directly run, first start with Jetpack 5.1.3
This version is very specific, 5.1.2 won't be acceptable for next steps

After booting with 5.1.3 complete the initial setup
Ensure the firmware update is scheduled, Wait for about 5 mins. Verify with
```bash
sudo systemctl status nv-l4t-bootloader-config
```
Then reboot and you can see the update
Once done, you will boot into JetPack 5.1.3 (again), with underlying firmware updated to 5.0-35550185.
Now that your UEFI firmware is updated to 35.5.0 (= JetPack 5.1.3), it is capable of updating the entire QSPI content to make it ready for JetPack 6.x.
verify
```bash
sudo nvbootctrl dump-slots-info
```
You should see Current version: 35.5.0
Install QSPI updater
```bash
sudo apt-get install nvidia-l4t-jetson-orin-nano-qspi-updater
```
Now reboot
the update will be observable but it wont boot again because the UEFI is now upgraded for jetpack 6.x and wont start with 5.1.3
Reflash with 6.2.1 (latest for my time)

Now the Hardware and OS are set but here you cant use the browsers
First check if the network is fine with ping
You can launch browsers from terminal to get a troubleshooting message
The one I got was
```bash
cannot create user data directory:
failed to verify SELinux context of /home/prime/snap:
cannot locate "matchpathcon" executable
```

This means Firefox is trying to interact with Snap infrastructure on a system that does NOT have SELinux tools installed (Ubuntu doesn’t use SELinux by default).
This happens when
Snap was partially installed / partially removed
Firefox binary is APT-based, but
The home directory still contains Snap remnants
Firefox tries to probe Snap confinement → fails → refuses to start correctly

Specifically on my system:
JetPack 6.2.1 image initially had Snap plumbing
Firefox was switched to APT-based
Snap core or SELinux helper tools are not present
/home/device_name/snap/ still exists
Firefox tries to validate sandbox paths → crashes early

Fix:
1. remove snap from home directory
```bash
rm -rf /home/prime/snap
```
2. Ensure snapd is fully removed (Jetson does not need it)
```bash
sudo apt purge snapd -y
sudo rm -rf /snap /var/snap /var/lib/snapd /var/cache/snapd
```
3. Reinstall browser (APT version only)- I will use firefox. Replace with chromium-browser if needed
```bash
sudo apt update
sudo apt install --reinstall firefox -y
```
4. Reset firefox profile
to ensure no stale sandbox metadata remains
```bash
rm -rf ~/.mozilla
```
Now launch the browser (from terminal) for checking, if everything works good, it can be launched normally also
   
