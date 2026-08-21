/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_speed.h"
#include <cassert>

USB_DEVICE_SPEED usbip::win_speed(usb_device_speed speed) noexcept
{
        switch (speed) {
        case USB_SPEED_SUPER_PLUS:
        case USB_SPEED_SUPER:
                return UsbSuperSpeed;
        case USB_SPEED_WIRELESS:
        case USB_SPEED_HIGH:
                return UsbHighSpeed;
        case USB_SPEED_FULL:
                return UsbFullSpeed;
        case USB_SPEED_LOW: 
        case USB_SPEED_UNKNOWN:
                return UsbLowSpeed;
        }

        assert(!"win_speed");
        return UsbLowSpeed;
}

usb_device_speed usbip::to_usbip_speed(USB_DEVICE_SPEED speed) noexcept
{
        switch (speed) {
        case UsbSuperSpeed:
                return USB_SPEED_SUPER;
        case UsbHighSpeed:
                return USB_SPEED_HIGH;
        case UsbFullSpeed:
                return USB_SPEED_FULL;
        case UsbLowSpeed:
        default:
                return USB_SPEED_LOW;
        }
}

const char* usbip::win_speed_name(USB_DEVICE_SPEED speed) noexcept
{
        switch (speed) {
        case UsbSuperSpeed:
                return "Super (5 Gbps)";
        case UsbHighSpeed:
                return "High (480 Mbps)";
        case UsbFullSpeed:
                return "Full (12 Mbps)";
        case UsbLowSpeed:
        default:
                return "Low (1.5 Mbps)";
        }
}

