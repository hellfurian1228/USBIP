/*
 * udp_transport.h
 *
 * UdpIsoTransport – manages the UDP socket for one device session.
 *
 * Responsibilities:
 *  - Open/close a UDP socket bound to an ephemeral local port.
 *  - Send ISO URB payloads (producer side, called from TCP dispatch thread).
 *  - Run a dedicated receive loop that feeds the JitterBuffer (consumer side).
 *  - Tear down cleanly when the TCP control session closes.
 */

#pragma once

#include "udp_iso_frame.h"
#include "jitter_buffer.h"
#include "transport_policy.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <WinSock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   using SOCKET = int;
   inline constexpr SOCKET INVALID_SOCKET = -1;
#endif

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Callback invoked by the receive loop for each reassembled ISO frame.
// The callback runs on the receive thread – keep it short.
// ---------------------------------------------------------------------------
using IsoFrameCallback = std::function<void(IsoFrame)>;

// ---------------------------------------------------------------------------
// UdpIsoTransport
// ---------------------------------------------------------------------------
class UdpIsoTransport
{
public:
    /**
     * @param remote_host  Hostname or IP of the USBIP server.
     * @param remote_port  UDP port on the server for ISO traffic.
     * @param cfg          Device network configuration (jitter window, etc.).
     * @param on_frame     Called for every received ISO frame (may be nullptr
     *                     if the caller polls via jitter_buffer() directly).
     */
    UdpIsoTransport(std::string        remote_host,
                    uint16_t           remote_port,
                    DeviceNetworkConfig cfg,
                    IsoFrameCallback   on_frame = nullptr);

    ~UdpIsoTransport();

    // Non-copyable
    UdpIsoTransport(const UdpIsoTransport &) = delete;
    UdpIsoTransport &operator=(const UdpIsoTransport &) = delete;

    // -----------------------------------------------------------------------
    // Start the UDP receive loop (spawns a background thread).
    // Must be called after construction and before send().
    // -----------------------------------------------------------------------
    bool start();

    // -----------------------------------------------------------------------
    // Stop the receive loop and close the socket.
    // Safe to call multiple times.
    // -----------------------------------------------------------------------
    void stop() noexcept;

    // -----------------------------------------------------------------------
    // Send an ISO URB payload over UDP.
    // Called from the TCP dispatch thread (producer).
    //
    // @param urb_id       USBIP seqnum of the originating URB.
    // @param ep_num       Endpoint number.
    // @param frame_count  Number of ISO frames in payload.
    // @param payload      Raw ISO data bytes.
    // @return true on success.
    // -----------------------------------------------------------------------
    bool send(uint32_t                 urb_id,
              uint8_t                  ep_num,
              uint16_t                 frame_count,
              std::span<const uint8_t> payload) noexcept;

    // -----------------------------------------------------------------------
    // Direct access to the jitter buffer (for polling consumers).
    // -----------------------------------------------------------------------
    JitterBuffer &jitter_buffer() noexcept { return m_jitter; }

    bool is_running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

private:
    void recv_loop() noexcept;

    bool resolve_remote();
    bool create_socket();

    std::string         m_remote_host;
    uint16_t            m_remote_port;
    DeviceNetworkConfig m_cfg;
    IsoFrameCallback    m_on_frame;

    SOCKET              m_sock{INVALID_SOCKET};
    sockaddr_storage    m_remote_addr{};
    int                 m_remote_addr_len{0};

    std::atomic<bool>   m_running{false};
    std::atomic<uint32_t> m_seq{0};  // monotonic send sequence counter

    JitterBuffer        m_jitter;
    std::thread         m_recv_thread;

    // Pre-allocated receive scratch buffer (avoids heap in hot path)
    alignas(64) std::array<uint8_t, kMaxIsoDatagramSize> m_recv_buf{};
};

} // namespace usbip::transport
