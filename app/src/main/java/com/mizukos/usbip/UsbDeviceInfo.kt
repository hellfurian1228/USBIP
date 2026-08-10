package com.mizukos.usbip

enum class ConnectionState {
    DISCONNECTED, CONNECTING, CONNECTED
}

data class UsbDeviceInfo(
    val deviceName: String,
    val deviceId: Int,
    val devicePath: String,
    val connectionState: ConnectionState = ConnectionState.DISCONNECTED
)
