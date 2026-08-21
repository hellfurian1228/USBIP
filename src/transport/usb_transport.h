/*
 * usb_transport.h
 *
 * Transport abstraction for vhci::attach(). TcpTransport wraps the existing,
 * stable TCP attach path unchanged. UdpTransport is an experimental stub that
 * currently falls back to TCP so the stable baseline is never affected until
 * a real UDP implementation lands.
 */

#pragma once

#include <vhci.h>

#include <cstdint>
#include <span>
#include <string>

namespace usbip::transport
{

enum class TransportMode
{
    TCP,
    UDP
};

class IUsbTransport
{
public:
    virtual ~IUsbTransport() = default;

    // Attaches the device via the vhci driver, returning the hub port (>=1) or <1 on failure.
    virtual int connect(HANDLE dev, const usbip::device_location &location) = 0;

    // Reserved for future user-mode URB relaying; unused while the kernel driver owns URB I/O.
    virtual bool sendUrb(std::span<const uint8_t> data) = 0;
    virtual bool receiveUrb(std::span<uint8_t> buffer) = 0;

    virtual void disconnect(HANDLE dev, int hubPort) = 0;
};

// Wraps the existing, stable TCP attach path. Behavior is identical to the pre-existing baseline.
class TcpTransport : public IUsbTransport
{
public:
    int connect(HANDLE dev, const usbip::device_location &location) override
    {
        return usbip::vhci::attach(dev, location);
    }

    bool sendUrb(std::span<const uint8_t>) override { return true; }
    bool receiveUrb(std::span<uint8_t>) override { return true; }

    void disconnect(HANDLE dev, int hubPort) override
    {
        usbip::vhci::detach(dev, hubPort);
    }
};

// Experimental. Not yet implemented: falls back to TcpTransport to preserve the stable baseline.
class UdpTransport : public IUsbTransport
{
public:
    int connect(HANDLE dev, const usbip::device_location &location) override
    {
        return m_fallback.connect(dev, location);
    }

    bool sendUrb(std::span<const uint8_t> data) override { return m_fallback.sendUrb(data); }
    bool receiveUrb(std::span<uint8_t> buffer) override { return m_fallback.receiveUrb(buffer); }

    void disconnect(HANDLE dev, int hubPort) override { m_fallback.disconnect(dev, hubPort); }

private:
    TcpTransport m_fallback;
};

} // namespace usbip::transport
