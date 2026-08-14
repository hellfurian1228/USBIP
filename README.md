Buy Me A Coffee?
https://www.paypal.com/donate/?business=WVZF92EENUJ7Y&no_recurring=0&currency_code=USD

THIS IS VERY EARLY IN DEVELOPMENT. THERE WILL BE BUGS.

<div align="center">
  
# 🌐 OmniStream

**End-to-end network streaming and hardware redirection solution.**

[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)](#-omnistream-client-windows-setup)
[![Platform: Android](https://img.shields.io/badge/Platform-Android%208.0+-3DDC84?logo=android&logoColor=white)](#-omnistream-host-android-setup)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-D22128.svg)](#-license)

OmniStream pairs a high-performance **Windows Client** with a low-latency **Android Host** to seamlessly stream virtual displays and redirect local USB devices over your local network.

</div>

---

## 📑 Table of Contents
- [Ecosystem Overview](#-ecosystem-overview)
- [Features](#-features)
- [OmniStream Client (Windows Setup)](#-omnistream-client-windows-setup)
- [OmniStream Host (Android Setup)](#-omnistream-host-android-setup)
- [Quick Start Guide](#-quick-start-guide)
- [License](#-license)

---

## 📐 Ecosystem Overview

| Component | Platform | Primary Function |
| :--- | :--- | :--- |
| **OmniStream Client** | Windows 10/11 (64-bit) | Manages virtual display streaming (DuoStream) and receives redirected USB devices. |
| **OmniStream Host** | Android (Mobile & TV) | Shares attached USB hardware over the network using native USB/IP protocol stack. |

---

## ✨ Features

### 💻 OmniStream Client (Windows)
* **Virtual Displays (DuoStream):** Manage and stream virtual displays using the Duo Indirect Display Driver (IDD) and Sunshine host.
* **USB Redirection (OmniStream USB):** Redirect local USB devices to remote hosts over the network using a WHLK-certified UDE and filter driver stack.
* **Integrated Logging:** Real-time log viewer capturing application events, OmniStream USB core logs, and driver status.
* **Self-Elevation:** Automatically requests administrator privileges when required to install or configure drivers.

### 📱 OmniStream Host (Android v1.0.4)
* **Dark Theme UI:** Matches the Windows client palette (`#00D2D3` Teal accent, `#0F1115` Deep Dark background).
* **Cross-Platform TV & Mobile Support:** Optimized responsive layout with focus handling (`btn_selector.xml`) for Android TV D-pad navigation and mobile touch.
* **Proactive USB Export:** Automatically detects, prompts, and exports USB hardware upon plug-in with stable Bus ID caching.
* **SuperSpeed USB 3.0 Support:** Accurately reports USB hardware speeds (Low, Full, High, SuperSpeed) using Android API 31+ reflection.
* **Power & Connection Stability:** Native TCP keepalive to purge zombie sockets, `FLAG_KEEP_SCREEN_ON`, and a background `PARTIAL_WAKE_LOCK` with a 10-minute safety timeout.
* **Hardware Robustness:** Specialized driver eviction logic for wheels switching modes (e.g., Logitech G29).

---

## 💻 OmniStream Client (Windows Setup)

### Requirements
* **Operating System:** Windows 10 or Windows 11 (64-bit)
* **Framework:** Qt 6.x (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6Network`)
* **Build Tools:** CMake 3.16+ and MSVC 2022 (or compatible compiler)
* **Bundled Drivers:**
  * OmniStream USB UDE and Filter drivers
  * Duo Indirect Display Driver

### Project Structure
```text
├── CMakeLists.txt         # Build and CPack packaging configuration
├── main.cpp               # Application entry point and self-elevation logic
├── mainwindow.cpp         # Main UI, driver verification, Duo/USB integration
├── logwindow.cpp          # Real-time logging window implementation
├── Drivers/               # Bundled OmniStream USB driver binaries
└── ProjectFiles/Duo/      # Bundled Duo display driver and Sunshine host binaries
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
