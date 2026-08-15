# USBIP-WindowsClient

[![Status](https://img.shields.io/badge/Status-Beta-orange.svg?style=flat-square)](https://github.com/hellfurian1228/USBIP-WindowsClient)
[![Donate](https://img.shields.io/badge/Donate-PayPal-blue.svg?style=flat-square&logo=paypal)](https://www.paypal.com/donate/?hosted_button_id=P3P43EXYJWRLC)

A modern, Qt-based graphical user interface for the USB/IP protocol on Windows. This client allows you to easily connect to remote USB/IP servers, mount shared USB devices, and manage connections with a clean and intuitive interface.

## ✨ Features
Intuitive GUI: Easily scan hosts, list available remote USB devices, and attach/detach them with a single click.

Auto-Connect: Automatically reconnect to desired devices on startup.

System Tray Integration: Minimize the application to the system tray to keep it running in the background.

Audio Relay Subsystem: Stream and receive audio over UDP to relay audio devices between systems.

System Logger: Built-in real-time logger for monitoring connection status and troubleshooting.

Multi-Architecture Support: Fully compatible with both x64 and ARM64 Windows devices.

## 📦 Requirements
OS: Windows 10 (version 1903 or later) or Windows 11 (x64 / ARM64)

Drivers: USB/IP VHCI and UDE drivers installed (Test Signing mode enabled if using unsigned drivers)

Framework: Qt 6.11.1 or later

## 🛠️ Build Instructions
### 1. IDE Setup
*   Open the project folder in Visual Studio or VS Code.

### 2. Configuration
*   Configure the project using CMake.

### 3. Compilation
*   Build the ALL_BUILD target in Release configuration.

### 4. Packaging
*   Package the installer using CPack (NSIS generator).

## ☕ Support the Project
If you find this tool useful and want to support continued development, feel free to buy me a coffee!

[**Donate via PayPal**](https://www.paypal.com/donate/?hosted_button_id=P3P43EXYJWRLC)

## 🛠️ Tech Stack
*   **C++ (JNI):** High-performance native server daemon for protocol handling.
*   **CMake:** Unified build system for native components.

*Note: This is an early beta. Use it, break it, and report issues to help improve stability.*
