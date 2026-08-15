# USB/IP Windows Client

A modern, Qt-based graphical user interface for the USB/IP protocol on Windows. This client allows you to easily connect to remote USB/IP servers, mount shared USB devices, and manage connections with a clean and intuitive interface.

## Features

- **Intuitive GUI**: Easily scan hosts, list available remote USB devices, and attach/detach them with a single click.
- **Auto-Connect**: Automatically reconnect to desired devices on startup.
- **System Tray Integration**: Minimize the application to the system tray to keep it running in the background.
- **Audio Relay Subsystem**: Stream and receive audio over UDP to relay audio devices between systems.
- **System Logger**: Built-in real-time logger for monitoring connection status and troubleshooting.
- **Multi-Architecture Support**: Fully compatible with both x64 and ARM64 Windows devices.

## Requirements

- **OS**: Windows 10 (version 1903 or later) or Windows 11 (x64 / ARM64)
- **Drivers**: USB/IP VHCI and UDE drivers installed (Test Signing mode enabled if using unsigned drivers)
- **Framework**: Qt 6.11.1 or later

## Build Instructions

The project is built using CMake and Visual Studio.

1. Open the project folder in Visual Studio or VS Code.
2. Configure the project using CMake.
3. Build the `ALL_BUILD` target in Release configuration.
4. Package the installer using CPack (NSIS generator).

## License

This project is licensed under the 2-Clause BSD License.
