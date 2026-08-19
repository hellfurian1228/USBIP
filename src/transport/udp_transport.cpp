/*
 * udp_transport.cpp
 *
 * Implementation of UdpIsoTransport.
 */

#include "udp_transport.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   static void close_socket(SOCKET s) noexcept { ::closesocket(s); }
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
   static void close_socket(SOCKET s) noexcept { ::close(s); }
#endif

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

UdpIsoTransport::UdpIsoTransport(std::string        remote_host,
                                 uint16_t           remote_port,
                                 DeviceNetworkConfig cfg,
                                 IsoFrameCallback   on_frame)
    : m_remote_host(std::move(remote_host))
    , m_remote_port(remote_port)
    , m_cfg(cfg)
    , m_on_frame(std::move(on_frame))
    , m_jitter(JitterBuffer::Duration{cfg.jitter_window_ms})
{}

UdpIsoTransport::~UdpIsoTransport()
{
    stop();
}

// ---------------------------------------------------------------------------
// start()
// ---------------------------------------------------------------------------

bool UdpIsoTransport::start()
{
    if (m_running.load(std::memory_order_acquire))
        return true; // already running

    if (!resolve_remote())
        return false;

    if (!create_socket())
        return false;

    m_running.store(true, std::memory_order_release);
    m_recv_thread = std::thread([this] { recv_loop(); });
    return true;
}

// ---------------------------------------------------------------------------
// stop()
// ---------------------------------------------------------------------------

void UdpIsoTransport::stop() noexcept
{
    if (!m_running.exchange(false, std::memory_order_acq_rel))
        return; // already stopped

    // Flush jitter buffer before closing so no stale data lingers
    m_jitter.flush();

    // Close the socket to unblock recvfrom()
    if (m_sock != INVALID_SOCKET) {
        close_socket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    if (m_recv_thread.joinable())
        m_recv_thread.join();
}

// ---------------------------------------------------------------------------
// send()
// ---------------------------------------------------------------------------

bool UdpIsoTransport::send(uint32_t                 urb_id,
                           uint8_t                  ep_num,
                           uint16_t                 frame_count,
                           std::span<const uint8_t> payload) noexcept
{
    if (m_sock == INVALID_SOCKET || !m_running.load(std::memory_order_acquire))
        return false;

    if (payload.size() > kMaxUdpPayload)
        return false; // payload too large for a single datagram

    // Build header in host order, then encode to network order
    UdpIsoFrameHeader hdr{};
    hdr.seq_number   = m_seq.fetch_add(1, std::memory_order_relaxed);
    hdr.seq_ack      = 0; // heartbeat/ack not used in this direction
    hdr.urb_id       = urb_id;
    hdr.ep_num       = ep_num;
    hdr.frame_count  = frame_count;
    hdr.total_length = static_cast<uint16_t>(payload.size());
    encode(hdr);

    // Use a stack-local scatter-gather buffer to avoid heap allocation.
    // Maximum ISO payload is bounded by kMaxUdpPayload (checked above).
    // We copy header + payload into m_recv_buf (reused as send scratch).
    // NOTE: m_recv_buf is only written here from the send thread and in
    // recv_loop() from the recv thread – they never overlap because send()
    // is called from the TCP dispatch thread and recv_loop() runs on its
    // own thread, each using separate regions of the buffer.
    // To be safe, use a separate stack buffer for sending.
    alignas(4) std::array<uint8_t, sizeof(UdpIsoFrameHeader) + kMaxUdpPayload> send_buf;

    std::memcpy(send_buf.data(), &hdr, sizeof(hdr));
    std::memcpy(send_buf.data() + sizeof(hdr), payload.data(), payload.size());

    const int total = static_cast<int>(sizeof(hdr) + payload.size());

    const int sent = static_cast<int>(
        ::sendto(m_sock,
                 reinterpret_cast<const char *>(send_buf.data()),
                 total,
                 0,
                 reinterpret_cast<const sockaddr *>(&m_remote_addr),
                 m_remote_addr_len));

    return sent == total;
}

// ---------------------------------------------------------------------------
// recv_loop() – runs on m_recv_thread
// ---------------------------------------------------------------------------

void UdpIsoTransport::recv_loop() noexcept
{
    sockaddr_storage from{};
    int from_len = sizeof(from);

    while (m_running.load(std::memory_order_acquire)) {
        const int n = static_cast<int>(
            ::recvfrom(m_sock,
                       reinterpret_cast<char *>(m_recv_buf.data()),
                       static_cast<int>(m_recv_buf.size()),
                       0,
                       reinterpret_cast<sockaddr *>(&from),
                       &from_len));

        if (n < 0) {
            // Socket closed or error – exit loop
            break;
        }

        if (static_cast<std::size_t>(n) < sizeof(UdpIsoFrameHeader))
            continue; // malformed datagram – discard

        // Parse and decode header
        UdpIsoFrameHeader hdr{};
        std::memcpy(&hdr, m_recv_buf.data(), sizeof(hdr));
        decode(hdr);

        // Validate declared payload length
        const std::size_t declared = hdr.total_length;
        const std::size_t available = static_cast<std::size_t>(n) - sizeof(hdr);
        if (declared > available)
            continue; // truncated datagram – discard

        const std::span<const uint8_t> payload{
            m_recv_buf.data() + sizeof(hdr),
            declared
        };

        // Push into jitter buffer
        const bool accepted = m_jitter.push(hdr, payload);

        // Notify callback if registered (and slot was accepted)
        if (accepted && m_on_frame) {
            // Try to pop immediately for low-latency path
            if (auto frame = m_jitter.try_pop(); frame.has_value())
                m_on_frame(std::move(*frame));
        }
    }
}

// ---------------------------------------------------------------------------
// resolve_remote()
// ---------------------------------------------------------------------------

bool UdpIsoTransport::resolve_remote()
{
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    const std::string port_str = std::to_string(m_remote_port);

    addrinfo *result = nullptr;
    const int rc = ::getaddrinfo(m_remote_host.c_str(), port_str.c_str(),
                                 &hints, &result);
    if (rc != 0 || !result)
        return false;

    std::memcpy(&m_remote_addr, result->ai_addr,
                static_cast<std::size_t>(result->ai_addrlen));
    m_remote_addr_len = static_cast<int>(result->ai_addrlen);

    ::freeaddrinfo(result);
    return true;
}

// ---------------------------------------------------------------------------
// create_socket()
// ---------------------------------------------------------------------------

bool UdpIsoTransport::create_socket()
{
    m_sock = ::socket(
        reinterpret_cast<sockaddr *>(&m_remote_addr)->sa_family,
        SOCK_DGRAM,
        IPPROTO_UDP);

    if (m_sock == INVALID_SOCKET)
        return false;

    // Set receive timeout so recvfrom() doesn't block forever on stop()
#ifdef _WIN32
    DWORD timeout_ms = 200;
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms));
#else
    timeval tv{0, 200'000}; // 200 ms
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif

    // 2 MB kernel buffers to absorb high-bandwidth ISO bursts without loss
    int rcvBufSize = 2097152;
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<const char *>(&rcvBufSize), sizeof(rcvBufSize));
    int sndBufSize = 2097152;
    ::setsockopt(m_sock, SOL_SOCKET, SO_SNDBUF,
                 reinterpret_cast<const char *>(&sndBufSize), sizeof(sndBufSize));

    return true;
}

} // namespace usbip::transport
