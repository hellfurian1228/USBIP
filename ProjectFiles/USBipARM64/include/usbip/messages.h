// Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
//
#pragma once

#include <minwindef.h>

using USBIP_STATUS = DWORD;
#define USBIP_ERROR_SUCCESS              ((USBIP_STATUS)0x00000000L)

#ifdef _NTSTATUS_

namespace usbip
{

static_assert(sizeof(USBIP_STATUS) == sizeof(NTSTATUS));
static_assert(USBIP_ERROR_SUCCESS == STATUS_SUCCESS);

constexpr auto as_ntstatus(_In_ USBIP_STATUS status)
{
       return static_cast<NTSTATUS>(status);
}

constexpr auto as_usbip_status(_In_ NTSTATUS status)
{
       return static_cast<USBIP_STATUS>(status);
}

} // namespace usbip

#endif // #ifdef _NTSTATUS_

//
//  Values are 32 bit values laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//
#define FACILITY_DRIVER                  0x100
#define FACILITY_LIBRARY                 0x101
#define FACILITY_DEVICE                  0x102


//
// Define the severity codes
//


//
// MessageId: USBIP_ERROR_GENERAL
//
// MessageText:
//
// Driver command completed unsuccessfully.
//
#define USBIP_ERROR_GENERAL              ((USBIP_STATUS)0xE1000001L)

//
// MessageId: USBIP_ERROR_VERSION
//
// MessageText:
//
// Incompatible USB/IP protocol version.
//
#define USBIP_ERROR_VERSION              ((USBIP_STATUS)0xE1000002L)

//
// MessageId: USBIP_ERROR_PROTOCOL
//
// MessageText:
//
// USB/IP protocol violation.
//
#define USBIP_ERROR_PROTOCOL             ((USBIP_STATUS)0xE1000003L)

//
// MessageId: USBIP_ERROR_PORTFULL
//
// MessageText:
//
// No free port on USB/IP hub.
//
#define USBIP_ERROR_PORTFULL             ((USBIP_STATUS)0xE1000004L)

//
// MessageId: USBIP_ERROR_ABI
//
// MessageText:
//
// ABI mismatch, unexpected size of the input structure
//
#define USBIP_ERROR_ABI                  ((USBIP_STATUS)0xE1000005L)

//
// MessageId: USBIP_ERROR_ST_NA
//
// MessageText:
//
// Device not available.
//
#define USBIP_ERROR_ST_NA                ((USBIP_STATUS)0xE1020001L)

//
// MessageId: USBIP_ERROR_ST_DEV_BUSY
//
// MessageText:
//
// Device busy (already exported).
//
#define USBIP_ERROR_ST_DEV_BUSY          ((USBIP_STATUS)0xE1020002L)

//
// MessageId: USBIP_ERROR_ST_DEV_ERR
//
// MessageText:
//
// Device in error state.
//
#define USBIP_ERROR_ST_DEV_ERR           ((USBIP_STATUS)0xE1020003L)

//
// MessageId: USBIP_ERROR_ST_NODEV
//
// MessageText:
//
// Device not found by bus id.
//
#define USBIP_ERROR_ST_NODEV             ((USBIP_STATUS)0xE1020004L)

//
// MessageId: USBIP_ERROR_ST_ERROR
//
// MessageText:
//
// Device unexpected response.
//
#define USBIP_ERROR_ST_ERROR             ((USBIP_STATUS)0xE1020005L)

//
// MessageId: USBIP_ERROR_VHCI_NOT_FOUND
//
// MessageText:
//
// VHCI device not found, driver not loaded?
//
#define USBIP_ERROR_VHCI_NOT_FOUND       ((USBIP_STATUS)0xE1010001L)

//
// MessageId: USBIP_ERROR_DEVICE_INTERFACE_LIST
//
// MessageText:
//
// Multiple instances of VHCI device interface found.
//
#define USBIP_ERROR_DEVICE_INTERFACE_LIST ((USBIP_STATUS)0xE1010002L)

//
// MessageId: USBIP_ERROR_DRIVER_RESPONSE
//
// MessageText:
//
// Unexpected response from the driver (length, content, etc.).
//
#define USBIP_ERROR_DRIVER_RESPONSE      ((USBIP_STATUS)0xE1010003L)

//
// MessageId: USBIP_ERROR_SERIAL_NUMBER
//
// MessageText:
//
// USB device serial number must contain no more than 15 alphanumeric ASCII characters.
//
#define USBIP_ERROR_SERIAL_NUMBER        ((USBIP_STATUS)0xE1000006L)

