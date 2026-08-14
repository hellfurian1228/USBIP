# OmniStream Client

OmniStream Client is a high-performance Windows client application designed for streaming virtual displays and redirecting USB devices over the network. It integrates virtual display technology (via Duo) and USB-over-IP redirection (via USB/IP) into a unified Qt-based interface.

## Features

- **Virtual Displays (DuoStream):** Manage and stream virtual displays using the Duo Indirect Display Driver (IDD) and Sunshine host.
- **USB Redirection (USB/IP):** Redirect local USB devices to remote hosts over the network using a WHLK-certified UDE and filter driver stack.
- **Integrated Logging:** Real-time log viewer capturing application events, USB/IP core logs, and driver status.
- **Self-Elevation:** Automatically requests administrator privileges when required to install or configure drivers.

## Requirements

- **Operating System:** Windows 10 or Windows 11 (64-bit)
- **Framework:** Qt 6.x (specifically Qt6Core, Qt6Gui, Qt6Widgets, and Qt6Network)
- **Build Tools:** CMake 3.16+ and MSVC 2022 (or compatible compiler)
- **Drivers (Bundled):**
  - USB/IP UDE and Filter drivers
  - Duo Indirect Display Driver

## Project Structure

- [CMakeLists.txt](CMakeLists.txt): Build and packaging configuration.
- [main.cpp](main.cpp): Application entry point and self-elevation logic.
- [mainwindow.cpp](mainwindow.cpp): Main user interface, driver verification, and Duo/USBip integration.
- [logwindow.cpp](logwindow.cpp): Logging window implementation.
- [Drivers/](Drivers/): Bundled USB/IP driver binaries and installation files.
- [ProjectFiles/Duo/](ProjectFiles/Duo/): Bundled Duo display driver and Sunshine host binaries.

## Building from Source

1. Open the project folder in Visual Studio Code or Visual Studio.
2. Ensure Qt 6 is installed and the path is configured in [CMakeLists.txt](CMakeLists.txt).
3. Configure the project using CMake:
   ```shell
   cmake -B build -S .
   ```
4. Build the project:
   ```shell
   cmake --build build --config Release
   ```

## Generating the Installer

The project is configured with CPack to generate a clean Windows NSIS installer containing all required drivers and dependencies:

1. Build the project in Release mode.
2. Run CPack from the build directory:
   ```shell
   cd build
   cpack -C Release
   ```
3. The installer executable (e.g., `OmniStream Client-1.0.0-beta-win64.exe`) will be generated in the `build` directory.

## Installation & Usage

1. Run the generated installer executable.
2. Follow the setup wizard to install OmniStream Client to `C:\Program Files\OmniStream`.
3. Launch the application from the desktop shortcut or Start Menu.
4. The application will automatically verify and install the required USB/IP and display drivers on first launch (requires administrator privileges).
