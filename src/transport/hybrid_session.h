/*
 * hybrid_session.h
 *
 * HybridSession – ties together the TCP control socket and the optional
 * UdpIsoTransport for one device session.
 *
 * Lifecycle:
 *   1. Construct with the TCP socket and device metadata.
 *   2. Call start_udp() if the device policy is HYBRID_TCP_UDP.
 *   3. Call demuxer().route() for every incoming CMD_SUBMIT.
 *   4. Call close() (or let the destructor run) on disconnect.
 *
 * Thread safety:
 *   close() may be called from any thread. All other methods are expected
 *   to be called from the TCP dispatch thread.
 */

#pragma once

#include "transport_policy.h"
#include "udp_transport.h"
#include "urb_demuxer.h"

#include <usbip/proto.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace usbip::transport
{

class HybridSession
{
public:
    /**
     * @param devid        USBIP device identifier.
     * @param vid          USB Vendor ID.
     * @param pid          USB Product ID.
     * @param remote_host  Hostname / IP of the USBIP server.
     */
    HybridSession(uint32_t    devid,
                  uint16_t    vid,
                  uint16_t    pid,
                  std::string remote_host)
        : m_devid(devid)
        , m_vid(vid)
        , m_pid(pid)
        , m_remote_host(std::move(remote_host))
    {}

    ~HybridSession() { close(); }

    HybridSession(const HybridSession &) = delete;
    HybridSession &operator=(const HybridSession &) = delete;

    // -----------------------------------------------------------------------
    // Start the UDP transport if the device policy is HYBRID_TCP_UDP.
    // @param on_frame  Optional callback for received ISO frames.
    // @return true if UDP was started (or policy is TCP_ONLY, which is fine).
    // -----------------------------------------------------------------------
    bool start_udp(IsoFrameCallback on_frame = nullptr)
    {
        const auto &registry = TransportPolicyRegistry::instance();
        const VidPid vp{m_vid, m_pid};
        const DeviceNetworkConfig cfg = registry.get(m_devid, vp);

        if (cfg.policy != TransportPolicy::HYBRID_TCP_UDP)
            return true; // TCP_ONLY – nothing to start

        if (cfg.udp_port == 0)
            return false; // misconfigured

        auto udp = std::make_shared<UdpIsoTransport>(
            m_remote_host, cfg.udp_port, cfg, std::move(on_frame));

        if (!udp->start())
            return false;

        m_demuxer.register_udp_transport(m_devid, udp);
        m_udp = std::move(udp);
        return true;
    }

    // -----------------------------------------------------------------------
    // Route a CMD_SUBMIT packet.
    // -----------------------------------------------------------------------
    RouteResult route(const usbip::header      &hdr,
                      std::span<const uint8_t>  payload,
                      const TcpSendFn          &tcp_send)
    {
        if (m_closed.load(std::memory_order_acquire))
            return RouteResult::Error;

        return m_demuxer.route(hdr, payload, tcp_send);
    }

    // -----------------------------------------------------------------------
    // Tear down the session (idempotent, thread-safe).
    // -----------------------------------------------------------------------
    void close() noexcept
    {
        if (m_closed.exchange(true, std::memory_order_acq_rel))
            return; // already closed

        // Stop UDP transport first – flushes jitter buffer and joins recv thread
        m_demuxer.unregister_udp_transport(m_devid);
        m_udp.reset();

        // Remove device from policy registry (optional – keeps registry clean)
        TransportPolicyRegistry::instance().remove(m_devid);
    }

    uint32_t devid()       const noexcept { return m_devid; }
    uint16_t vid()         const noexcept { return m_vid; }
    uint16_t pid()         const noexcept { return m_pid; }
    bool     is_closed()   const noexcept { return m_closed.load(std::memory_order_acquire); }

    UrbDemuxer &demuxer() noexcept { return m_demuxer; }

    // Direct access to the jitter buffer (nullptr if UDP not started)
    JitterBuffer *jitter_buffer() noexcept
    {
        return m_udp ? &m_udp->jitter_buffer() : nullptr;
    }

private:
    uint32_t    m_devid;
    uint16_t    m_vid;
    uint16_t    m_pid;
    std::string m_remote_host;

    std::atomic<bool>                    m_closed{false};
    UrbDemuxer                           m_demuxer;
    std::shared_ptr<UdpIsoTransport>     m_udp;
};

} // namespace usbip::transport
