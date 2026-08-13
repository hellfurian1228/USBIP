Buy Me A Coffee?
https://www.paypal.com/donate/?business=WVZF92EENUJ7Y&no_recurring=0&currency_code=USD

THIS IS VERY EARLY IN DEVELOPMENT. THERE WILL BE BUGS.

# USB/IP Cross-Platform Client & Driver Suite (`usbip-win2`)

![Status](https://img.shields.io/badge/Status-Beta-orange)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Android-blue)

A lightweight client application and kernel driver suite designed to share and connect USB peripherals (such as gaming steering wheels, pedals, and controllers) from an Android host device to a Windows client computer over a local network using the USB/IP protocol.

Setup:

Download USBIPClient.exe
Download USBIPHost.apk
Download USBIP.zip

Extract USBIP.zip to a folder.
Run the install_drivers.bat 

Open USBIP.apk on Android Host and install.

Open USBIP App on Android Device
Connect USB Device(s)
Refresh if needed (top right of app)
Click "Connect"

Open USBIP App on Windows Client
Enter IP (displayed on Android app)
Click "Scan" (second tab at top)
Click "Connect"

Device should be connected.

If this does not work, try using the original USBIP_Win2 App on the Windows Client. This has been reported to work for some users.
```text
Tested on:
Samsung Galaxy S20+
Samsung Galaxy S22+ Ultra
Samsung Tab A8
Essential PH-1
Samsung Galaxy Z Fold 7
Google Pixel 10 Fold Pro
---
```
## 📂 Project Structure

```
USBIP-v1.x.x.zip/
│
├── Drivers/
│   ├── usbip2_ude.inf      # Virtual Host Controller driver setup
│   ├── usbip2_ude.sys      # Virtual Host Controller driver binary
│   ├── usbip2_ude.cat      # Security catalog file for UDE driver
│   ├── usbip2_filter.inf   # USB device filter driver setup
│   ├── usbip2_filter.sys   # USB device filter driver binary
│   └── usbip2_filter.cat   # Security catalog file for filter driver
│
├── install_drivers.bat     # Automated administrator-elevated driver installer
├── USBIP-Client-v0.3Beta.exe # Windows client application executable
└── USBIP-Host-v0.3Beta.apk   # Android host companion application
```
## 🚀 Quick Start Guide
```
Step 1: Install Required Windows Drivers
Because this project utilizes core kernel-mode components (usbip2_ude and usbip2_filter), they must be registered with Windows before running the client application.

Keep all driver files inside the Drivers/ folder right next to your installer script.

Right-click on install_drivers.bat and select Run as administrator.

Accept the User Account Control (UAC) prompt when it appears. The script will automatically install both required driver packages and stay open so you can review the status.

Press any key to close the window once finished.

Step 2: Set Up the Android Host
To share your physical USB hardware (e.g., a racing wheel setup) from an Android device:

Transfer USBIP-Host-v0.3Beta.apk to your Android device and install it (ensure installation from unknown sources is enabled if prompted).

Connect your target USB device directly to your Android device using a compatible USB OTG (On-The-Go) cable.

Launch the Android USB/IP Host application and grant it permission to access the connected USB hardware.

Step 3: Connect and Play
Ensure both your Windows PC and your Android device are connected to the same local network (Wi-Fi or Ethernet).

Launch USBIP-Client-v0.3Beta.exe on Windows.

Input the local IP address of your Android device as displayed inside the Android host app.

Select your device from the list and click Attach to mount the remote USB peripheral directly into your Windows session.
```

## 🛠️ System Requirements
```
Windows Client: Windows 10 or Windows 11 (64-bit) with administrator access for initial driver installation.

Android Host: Android device featuring USB OTG host capabilities.
```
I have had some users getting upset with how this is being made. So I will be transparent here.
This is being done with Android Studio and VSCode. These programs have AI "Agents" that can write and modify code. It does virtually all of the heavy lifting. Something that would take a software engineer weeks or months to code, can be done in days.
It is almost never "clean". It requires a lot of review, debugging, testing, etc. AI can only do so much. That's where I come in. I make sure it functions properly. If there are errors, I fix them. If I need to test certain devices, I will hook them up and make sure it works. If someone here says "I have error x1234 when trying to connect with x device", AI isn't going to magically fix that right away.

Some people hate AI. I don't fully understand why. It is a tool. It obviously contributed to something multiple people are finding useful (including myself).

So, in the kindest way possible. If you don't like how this is built, don't use it. Make your own from scratch and share it for free.

Thank you.
```

## Acknowledgements and Credits

This application is built upon the foundational open-source work of the USB/IP ecosystem. I would like to specifically credit the original creators, researchers, and maintainers who made this protocol possible:

* **Takahiro Hirofuchi & the NAIST Research Team:** The original architects of the USB/IP protocol. Takahiro Hirofuchi, along with Eiji Kawai, Kazutoshi Fujikawa, and Hideki Sunahara at the Nara Institute of Science and Technology (Japan), designed and developed the first implementation of USB/IP, presenting their award-winning research at the 2005 USENIX technical conference.
* **The Linux Kernel Community:** The core USB/IP drivers were integrated into the mainline Linux kernel (as of Linux 3.17) and continue to be maintained and improved by the global open-source kernel community.
* **cezanne (GitHub):** The creator of the `usbip-win` open-source project, which successfully ported the Virtual Host Controller Interface (VHCI) driver to Windows. Their work is what makes it possible for modern Windows machines to act as USB/IP clients.
