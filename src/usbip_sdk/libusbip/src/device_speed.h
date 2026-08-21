/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <usbip/ch9.h>

#include <wtypes.h>
#include <usbspec.h>

namespace usbip
{

USB_DEVICE_SPEED win_speed(usb_device_speed speed) noexcept;
usb_device_speed to_usbip_speed(USB_DEVICE_SPEED speed) noexcept;
const char* win_speed_name(USB_DEVICE_SPEED speed) noexcept;

} // namespace usbip