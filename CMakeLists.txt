cmake_minimum_required(VERSION 3.16)
project(OmniStreamClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# BRUTE FORCE THE PATH - No conditions, no exceptions
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Force Qt6 Path" FORCE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(USBIP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/USBip")

add_executable(OmniStreamClient
    main.cpp
    mainwindow.cpp
    mainwindow.h
    logwindow.cpp
    logwindow.h
)

target_include_directories(OmniStreamClient PRIVATE
    "${USBIP_SDK_DIR}/include"
)

target_link_libraries(OmniStreamClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    "${USBIP_SDK_DIR}/lib/libusbip.lib"
)

# Self-elevation is handled programmatically in main.cpp, so no UAC manifest is needed here.

set_target_properties(OmniStreamClient PROPERTIES
    WIN32_EXECUTABLE OFF
)

# Copy libusbip.dll next to the executable
add_custom_command(TARGET OmniStreamClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${USBIP_SDK_DIR}/libusbip.dll"
        "$<TARGET_FILE_DIR:OmniStreamClient>/libusbip.dll"
)

# Copy Drivers folder to build directory
add_custom_command(TARGET OmniStreamClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers"
        "$<TARGET_FILE_DIR:OmniStreamClient>/Drivers"
)

# Copy usb.ids to build directory
add_custom_command(TARGET OmniStreamClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids"
        "$<TARGET_FILE_DIR:OmniStreamClient>/usb.ids"
)

# --- Installation and Packaging Configuration ---

# Install target executable
install(TARGETS OmniStreamClient RUNTIME DESTINATION .)

# Install libusbip.dll
install(FILES "${USBIP_SDK_DIR}/libusbip.dll" DESTINATION .)

# Install usb.ids
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids" DESTINATION .)

# Install Drivers directory
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/" DESTINATION Drivers)

# Install Duo directory (excluding uninstaller files)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ProjectFiles/Duo/"
        DESTINATION Duo
        PATTERN "unins000.*" EXCLUDE)

# Deploy Qt dependencies using windeployqt
install(CODE "
    execute_process(
        COMMAND \"C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe\"
                --no-compiler-runtime
                --verbose 0
                \"\${CMAKE_INSTALL_PREFIX}/OmniStreamClient.exe\"
    )
")

# CPack Configuration
set(CPACK_PACKAGE_NAME "OmniStream Client")
set(CPACK_PACKAGE_VENDOR "Tyler Long")
set(CPACK_PACKAGE_VERSION "1.0.0-beta")
set(CPACK_GENERATOR "NSIS")

# NSIS Specifics
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OmniStream")
set(CPACK_NSIS_DISPLAY_NAME "OmniStream Client")
set(CPACK_NSIS_PACKAGE_NAME "OmniStream Client")

# Create desktop shortcut
set(CPACK_NSIS_CREATE_ICONS_EXTRA "  CreateShortCut \\\"$DESKTOP\\\\OmniStream Client.lnk\\\" \\\"$INSTDIR\\\\OmniStreamClient.exe\\\"")
set(CPACK_NSIS_DELETE_ICONS_EXTRA "  Delete \\\"$DESKTOP\\\\OmniStream Client.lnk\\\"")

include(CPack)