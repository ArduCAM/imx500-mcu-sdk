# Mission: Bring Up IMX500 Metadata On RPi5(experimental)

## Get image

Download the Installation Script
```
sudo update
sudo apt install wget -y
wget -O install_pivariety_pkgs.sh https://github.com/ArduCAM/Arducam-Pivariety-V4L2-Driver/releases/download/install_script/install_pivariety_pkgs.sh
chmod +x install_pivariety_pkgs.sh
```
Install Arducam libcamera Software
```
./install_pivariety_pkgs.sh -p libcamera_dev
./install_pivariety_pkgs.sh -p libcamera_apps
```

Open the configuration file:
```
sudo nano /boot/firmware/config.txt
```

Disable camera auto-detection:
```
camera_auto_detect=0
```
Add arducam-pivariety overlay under the [all] section:
```
dtoverlay=arducam-pivariety
```
Save and reboot:
```
sudo reboot
```
Get frame
```
rpicam-still -t 0 --tuning-file /usr/share/libcamera/ipa/rpi/pisp/imx500.json
```

![](pics/image_preview.png)