cmake_minimum_required(VERSION 3.16)
project(USBIPClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# BRUTE FORCE THE PATH - No conditions, no exceptions
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Force Qt6 Path" FORCE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

set(USBIP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip")

# Build libusbip from source to ensure ABI compatibility (Debug/Release)
add_library(libusbip SHARED
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/device_speed.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/dllmain.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/file_ver.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/format_message.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/output.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/persistent.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/proto_op.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/remote.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/strconv.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/usb_ids.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/vhci.cpp
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip/src/win_socket.cpp
)

target_compile_definitions(libusbip PRIVATE
    USBIP_EXPORTS
    _WINDOWS
    _USRDLL
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_include_directories(libusbip PRIVATE
    ProjectFiles/usbip-win2-SOURCE/include
    ProjectFiles/usbip-win2-SOURCE/userspace/libusbip
    ProjectFiles/usbip-win2-SOURCE/userspace
)

target_link_libraries(libusbip PRIVATE
    Version
    ws2_32
    CfgMgr32
)

add_executable(USBIPClient
    main.cpp
    mainwindow.cpp
    mainwindow.h
    logwindow.cpp
    logwindow.h
    audiorelaymanager.cpp
    audiorelaymanager.h
)

target_include_directories(USBIPClient PRIVATE
    "${USBIP_SDK_DIR}/include"
)

target_link_libraries(USBIPClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    libusbip
)

# Self-elevation is handled programmatically in main.cpp, so no UAC manifest is needed here.

set_target_properties(USBIPClient PROPERTIES
    WIN32_EXECUTABLE OFF
)

# Deploy Qt dependencies using windeployqt post-build
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND "C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe"
        --no-compiler-runtime
        --verbose 0
        "$<TARGET_FILE:USBIPClient>"
)

# Copy Drivers folder to build directory
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers"
)

# Copy usb.ids to build directory
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids"
        "$<TARGET_FILE_DIR:USBIPClient>/usb.ids"
)

# --- Installation and Packaging Configuration ---

# Install target executable and libusbip DLL
install(TARGETS USBIPClient libusbip RUNTIME DESTINATION .)

# Install usb.ids
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids" DESTINATION .)

# Install Drivers directory
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/" DESTINATION Drivers)

# Deploy Qt dependencies using windeployqt
install(CODE "
    execute_process(
        COMMAND \"C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe\"
                --no-compiler-runtime
                --verbose 0
                \"\${CMAKE_INSTALL_PREFIX}/USBIPClient.exe\"
    )
")

# CPack Configuration
set(CPACK_PACKAGE_NAME "USBIP Client")
set(CPACK_PACKAGE_VENDOR "Tyler Long")
set(CPACK_PACKAGE_VERSION "1.0.0-beta")
set(CPACK_GENERATOR "NSIS")

# NSIS Specifics
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "USBIP Client")
set(CPACK_NSIS_DISPLAY_NAME "USBIP Client")
set(CPACK_NSIS_PACKAGE_NAME "USBIP Client")

# Create desktop shortcut
set(CPACK_NSIS_CREATE_ICONS_EXTRA "  CreateShortCut \\\"$DESKTOP\\\\USBIP Client.lnk\\\" \\\"$INSTDIR\\\\USBIPClient.exe\\\"")
set(CPACK_NSIS_DELETE_ICONS_EXTRA "  Delete \\\"$DESKTOP\\\\USBIP Client.lnk\\\"")

include(CPack)