package com.mizukos.usbip

import java.net.Inet4Address
import java.net.NetworkInterface

/**
 * Utility function to get the device's local IPv4 address.
 * This is used to display the IP for the USB/IP attach command.
 */
fun getDeviceIpAddress(): String {
    try {
        val interfaces = NetworkInterface.getNetworkInterfaces()
        for (intf in interfaces) {
            val addrs = intf.inetAddresses
            for (addr in addrs) {
                if (!addr.isLoopbackAddress && addr is Inet4Address) {
                    val hostAddress = addr.hostAddress
                    if (hostAddress != null && hostAddress.isNotEmpty()) {
                        return hostAddress
                    }
                }
            }
        }
    } catch (e: Exception) {
        e.printStackTrace()
    }
    return "127.0.0.1"
}
