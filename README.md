# OmniStream Client

OmniStream Client is a high-performance Windows client application designed for streaming virtual displays and redirecting USB devices over the network. It integrates virtual display technology (via Duo) and USB-over-IP redirection (via OmniStream USB) into a unified Qt-based interface.

## Features

- **Virtual Displays (DuoStream):** Manage and stream virtual displays using the Duo Indirect Display Driver (IDD) and Sunshine host.
- **USB Redirection (OmniStream USB):** Redirect local USB devices to remote hosts over the network using a WHLK-certified UDE and filter driver stack.
- **Integrated Logging:** Real-time log viewer capturing application events, OmniStream USB core logs, and driver status.
- **Self-Elevation:** Automatically requests administrator privileges when required to install or configure drivers.

## Requirements

- **Operating System:** Windows 10 or Windows 11 (64-bit)
- **Framework:** Qt 6.x (specifically Qt6Core, Qt6Gui, Qt6Widgets, and Qt6Network)
- **Build Tools:** CMake 3.16+ and MSVC 2022 (or compatible compiler)
- **Drivers (Bundled):**
  - OmniStream USB UDE and Filter drivers
  - Duo Indirect Display Driver

## Project Structure

- [CMakeLists.txt](CMakeLists.txt): Build and packaging configuration.
- [main.cpp](main.cpp): Application entry point and self-elevation logic.
- [mainwindow.cpp](mainwindow.cpp): Main user interface, driver verification, and Duo/OmniStream USB integration.
- [logwindow.cpp](logwindow.cpp): Logging window implementation.
- [Drivers/](Drivers/): Bundled OmniStream USB driver binaries and installation files.
- [ProjectFiles/Duo/](ProjectFiles/Duo/): Bundled Duo display driver and Sunshine host binaries.

## Installation

### Installing via the Executable Installer (.exe)

For a quick and clean setup of the beta release, use the pre-compiled installer:

1. Download the installer `OmniStream Client-1.0.0-beta-win64.exe` from the GitHub Releases page.
2. Run the installer. Note that Administrator privileges are required to register the virtual display and USB redirection drivers.
3. Follow the setup wizard to install OmniStream Client to the default directory: `C:\Program Files\OmniStream`.
4. Choose whether to create a desktop shortcut and Start Menu folder.
5. Launch OmniStream Client from the desktop shortcut or Start Menu.
6. On first launch, the application will automatically verify and register the required virtual display and USB redirection drivers.

### Building from Source

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
3. The installer executable (`OmniStream Client-1.0.0-beta-win64.exe`) will be generated in the `build` directory.

## Usage

1. Launch the application (it will request self-elevation to manage drivers).
2. Use the **DuoStream** tab to configure virtual displays, desktop scaling, and start/stop display instances.
3. Use the **USB Redirection** tab to share and redirect local USB devices over the network.
4. View real-time logs in the integrated log window to monitor connection and driver status.
