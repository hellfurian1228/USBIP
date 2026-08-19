cmake_minimum_required(VERSION 3.16)
project(USBIPClient VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# BRUTE FORCE THE PATH - No conditions, no exceptions
set(CMAKE_PREFIX_PATH "C:/Qt/6.12.0/msvc2022_64" CACHE PATH "Force Qt6 Path" FORCE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 OPTIONAL_COMPONENTS Multimedia)

# --- Opus codec (fetched from source) ---
include(FetchContent)

# Opus v1.4 declares cmake_minimum_required(3.1); CMake >= 3.27 rejects < 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Suppress CMP0048 (project() VERSION handling) triggered by old sub-projects.
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()

FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.4
    GIT_SHALLOW    TRUE
)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opus)

# --- usbip-win2 user-mode SDK (built from source to avoid ABI mismatches) ---
set(USBIP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/usbip_sdk")
file(GLOB USBIP_SDK_SOURCES CONFIGURE_DEPENDS
    "${USBIP_SDK_ROOT}/libusbip/src/*.c"
    "${USBIP_SDK_ROOT}/libusbip/src/*.cpp"
)

# Generate the message-table header referenced as <resources/messages.h>
find_program(MC_EXECUTABLE mc REQUIRED)

set(USBIP_SDK_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/usbip_sdk")

add_custom_command(
    OUTPUT "${USBIP_SDK_GENERATED_DIR}/resources/messages.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${USBIP_SDK_GENERATED_DIR}/resources"
    COMMAND ${MC_EXECUTABLE} -h "${USBIP_SDK_GENERATED_DIR}/resources" -r "${USBIP_SDK_GENERATED_DIR}/resources"
        "${USBIP_SDK_ROOT}/include/resources/messages.mc"
    MAIN_DEPENDENCY "${USBIP_SDK_ROOT}/include/resources/messages.mc"
    COMMENT "Generating messages.h from messages.mc"
    VERBATIM
)

add_custom_target(usbip_sdk_messages DEPENDS "${USBIP_SDK_GENERATED_DIR}/resources/messages.h")

add_library(usbip_sdk STATIC ${USBIP_SDK_SOURCES})
add_dependencies(usbip_sdk usbip_sdk_messages)

target_include_directories(usbip_sdk PUBLIC
    "${USBIP_SDK_ROOT}/include"
    "${USBIP_SDK_ROOT}/libusbip"
    "${USBIP_SDK_GENERATED_DIR}"
)

# Private: libusbip/src contains an internal setupapi.h shim that would shadow
# the real Windows SDK <setupapi.h> for consumers if exposed publicly.
target_include_directories(usbip_sdk PRIVATE
    "${USBIP_SDK_ROOT}/libusbip/src"
)

target_compile_definitions(usbip_sdk PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    LIBUSBIP_STATIC
)

# dllspec.h defaults USBIP_API to dllimport unless USBIP_EXPORTS is defined; consumers
# must see the same definition too, otherwise they emit dllimport thunks that don't
# resolve against the dllexport symbols in this static library (LNK2019)
target_compile_definitions(usbip_sdk PUBLIC USBIP_EXPORTS)

target_link_libraries(usbip_sdk PRIVATE
    ws2_32
    setupapi
    cfgmgr32
)

add_executable(USBIPClient
    main.cpp
    mainwindow.cpp
    mainwindow.h
    logwindow.cpp
    logwindow.h
    audiorelaymanager.cpp
    audiorelaymanager.h
    driverinstaller.cpp
    driverinstaller.h
    src/audio/opus_transcoder.cpp
    src/transport/udp_transport.cpp
    src/audio/opus_transcoder.h
)

target_link_libraries(USBIPClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    usbip_sdk
    opus
    setupapi
    newdev
)

target_include_directories(USBIPClient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(TARGET Qt6::Multimedia)
    target_link_libraries(USBIPClient PRIVATE Qt6::Multimedia)
    target_compile_definitions(USBIPClient PRIVATE HAVE_QT_MULTIMEDIA)
endif()

# Self-elevation is handled programmatically in main.cpp, so no UAC manifest is needed here.

set_target_properties(USBIPClient PROPERTIES
    WIN32_EXECUTABLE ON
)

# Deploy Qt dependencies using windeployqt post-build
add_custom_command(TARGET USBIPClient POST_BUILD
    COMMAND "C:/Qt/6.12.0/msvc2022_64/bin/windeployqt.exe"
        --no-compiler-runtime
        --verbose 0
        "$<TARGET_FILE:USBIPClient>"
)

# Deploy the pre-signed kernel driver package next to the executable
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

# Install target executable
install(TARGETS USBIPClient RUNTIME DESTINATION .)

# Install usb.ids
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/usb.ids" DESTINATION .)

# Deploy Qt dependencies using windeployqt
install(CODE "
    execute_process(
        COMMAND \"C:/Qt/6.12.0/msvc2022_64/bin/windeployqt.exe\"
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
set(CPACK_PACKAGE_VERSION "1.0.1")
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
