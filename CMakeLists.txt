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

# Copy libusbip.dll to Drivers folder in build directory
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:libusbip>"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/$<TARGET_FILE_NAME:libusbip>"
)

# Copy resources.dll to build directory and Drivers folder
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip/resources.dll"
        "$<TARGET_FILE_DIR:USBIPClient>/resources.dll"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip/resources.dll"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/resources.dll"
)

# Copy usb.ids to build directory
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids"
        "$<TARGET_FILE_DIR:USBIPClient>/usb.ids"
)

# Copy ARM64 binaries and drivers to Drivers/ARM64 folder in build directory
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/usbip.exe"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip.exe"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/devnode.exe"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/devnode.exe"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/libusbip.dll"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/libusbip.dll"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/resources.dll"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/resources.dll"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.cat"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_filter.cat"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.inf"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_filter.inf"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.sys"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_filter.sys"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.cat"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_ude.cat"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.inf"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_ude.inf"
)
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.sys"
        "$<TARGET_FILE_DIR:USBIPClient>/Drivers/ARM64/usbip2_ude.sys"
)

# --- Installation and Packaging Configuration ---

# Install Drivers directory first
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/" DESTINATION Drivers)

# Install target executable and libusbip DLL to root
install(TARGETS USBIPClient RUNTIME DESTINATION .)
install(TARGETS libusbip RUNTIME DESTINATION .)

# Also install libusbip DLL to Drivers directory so usbip.exe can find it
install(FILES "$<TARGET_FILE:libusbip>" DESTINATION Drivers)

# Install resources.dll to root and Drivers directory
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip/resources.dll" DESTINATION .)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip/resources.dll" DESTINATION Drivers)

# Install ARM64 binaries and drivers to Drivers/ARM64
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/usbip.exe" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/devnode.exe" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/libusbip.dll" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBipARM64/resources.dll" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.cat" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.inf" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_filter.sys" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.cat" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.inf" DESTINATION Drivers/ARM64)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/usbip2_ude.sys" DESTINATION Drivers/ARM64)

# Install usb.ids
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids" DESTINATION .)

# Deploy Qt dependencies using windeployqt
install(CODE "
    execute_process(
        COMMAND \"C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe\"
                --no-compiler-runtime
                --verbose 0
                \"\${CMAKE_INSTALL_PREFIX}/USBIPClient.exe\"
    )
")

# Install MSVC runtime libraries
include(InstallRequiredSystemLibraries)
if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
endif()

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