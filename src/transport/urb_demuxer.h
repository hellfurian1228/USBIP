/*
 * urb_demuxer.h
 *
 * UrbDemuxer – inspects incoming USBIP_CMD_SUBMIT packets and routes them
 * to the correct transport backend:
 *
 *   - ISO endpoint + HYBRID_TCP_UDP policy  →  UdpIsoTransport::send()
 *   - Everything else                        →  TCP (caller's existing socket)
 *
 * The demuxer is stateless with respect to the TCP socket; it only decides
 * the routing and delegates the actual TCP write back to the caller.
 */

#pragma once

#include "transport_policy.h"
#include "udp_transport.h"
#include "../audio/opus_transcoder.h"

#include <usbip/proto.h>   // usbip::header, usbip::header_cmd_submit, etc.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <mutex>

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Result of a routing decision
// ---------------------------------------------------------------------------
enum class RouteResult
{
    SentOverUdp,   ///< ISO payload dispatched via UDP
    SendOverTcp,   ///< Caller must forward the full packet over TCP
    Error          ///< Routing failed (e.g., UDP socket not ready)
};

// ---------------------------------------------------------------------------
// Callback the demuxer invokes when it needs the caller to send data over TCP.
// Signature: bool(const void* data, size_t len)
// ---------------------------------------------------------------------------
using TcpSendFn = std::function<bool(const void *, std::size_t)>;

// ---------------------------------------------------------------------------
// UrbDemuxer
// ---------------------------------------------------------------------------
class UrbDemuxer
{
public:
    UrbDemuxer() = default;
    ~UrbDemuxer() = default;

    UrbDemuxer(const UrbDemuxer &) = delete;
    UrbDemuxer &operator=(const UrbDemuxer &) = delete;

    // Register an OpusTranscoder for a devid that carries UAC audio.
    // The transcoder's encoded callback must forward packets to the UDP transport.
    void register_audio_transcoder(uint32_t devid,
                                   std::shared_ptr<audio::OpusTranscoder> transcoder)
    {
        std::lock_guard lock(m_mutex);
        m_transcoders[devid] = std::move(transcoder);
    }

    void unregister_audio_transcoder(uint32_t devid) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_transcoders.erase(devid);
    }

    // -----------------------------------------------------------------------
    // Register a UDP transport for a specific devid.
    // Called when a device session is established and the policy is HYBRID.
    // -----------------------------------------------------------------------
    void register_udp_transport(uint32_t devid,
                                std::shared_ptr<UdpIsoTransport> transport)
    {
        std::lock_guard lock(m_mutex);
        m_transports[devid] = std::move(transport);
    }

    // -----------------------------------------------------------------------
    // Remove the UDP transport for a devid (call on device disconnect).
    // -----------------------------------------------------------------------
    void unregister_udp_transport(uint32_t devid) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_transports.find(devid); it != m_transports.end()) {
            it->second->stop();
            m_transports.erase(it);
        }
    }

    // -----------------------------------------------------------------------
    // Route a USBIP_CMD_SUBMIT packet.
    //
    // @param hdr          Parsed USBIP header (host byte order).
    // @param payload      Raw bytes following the header (transfer buffer +
    //                     optional ISO descriptors).
    // @param tcp_send     Callable to forward the packet over TCP if needed.
    //
    // The function does NOT modify the TCP stream; it only sends over UDP
    // when appropriate and returns SendOverTcp when the caller must do so.
    // -----------------------------------------------------------------------
    RouteResult route(const usbip::header      &hdr,
                      std::span<const uint8_t>  payload,
                      const TcpSendFn          &tcp_send)
    {
        // Only CMD_SUBMIT packets are candidates for UDP routing
        if (hdr.command != static_cast<uint32_t>(usbip::request_type::CMD_SUBMIT))
            return RouteResult::SendOverTcp;

        const bool is_iso =
            hdr.cmd_submit.number_of_packets != usbip::number_of_packets_non_isoch
            && hdr.cmd_submit.number_of_packets > 0;

        if (!is_iso)
            return RouteResult::SendOverTcp;

        // Check policy
        const auto &registry = TransportPolicyRegistry::instance();
        if (!registry.is_hybrid(hdr.devid))
            return RouteResult::SendOverTcp;

        // Look up the UDP transport for this device
        std::shared_ptr<UdpIsoTransport> udp;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_transports.find(hdr.devid);
            if (it == m_transports.end() || !it->second->is_running())
                return RouteResult::SendOverTcp; // fall back to TCP
            udp = it->second;
        }

        // Separate the ISO data from the ISO packet descriptors.
        // Layout after the USBIP header for CMD_SUBMIT:
        //   [transfer_buffer (transfer_buffer_length bytes)]
        //   [iso_packet_descriptor × number_of_packets]
        const int32_t buf_len  = hdr.cmd_submit.transfer_buffer_length;
        const int32_t n_pkts   = hdr.cmd_submit.number_of_packets;

        if (buf_len < 0 || n_pkts < 0)
            return RouteResult::Error;

        const std::size_t iso_data_len = static_cast<std::size_t>(buf_len);
        const std::size_t iso_desc_len =
            static_cast<std::size_t>(n_pkts) * sizeof(usbip::iso_packet_descriptor);

        if (payload.size() < iso_data_len + iso_desc_len)
            return RouteResult::Error; // malformed

        const std::span<const uint8_t> iso_data = payload.subspan(0, iso_data_len);

        // If an audio transcoder is registered, compress PCM before sending.
        std::shared_ptr<audio::OpusTranscoder> transcoder;
        {
            std::lock_guard lock(m_mutex);
            if (auto it = m_transcoders.find(hdr.devid); it != m_transcoders.end())
                transcoder = it->second;
        }

        if (transcoder && transcoder->valid()) {
            // encode_pcm fires the callback (which calls udp->send) per Opus frame.
            transcoder->encode_pcm(iso_data.data(), static_cast<int>(iso_data.size()));
            return RouteResult::SentOverUdp;
        }

        const bool ok = udp->send(
            hdr.seqnum,
            static_cast<uint8_t>(hdr.ep),
            static_cast<uint16_t>(n_pkts),
            iso_data);

        return ok ? RouteResult::SentOverUdp : RouteResult::Error;
    }

    // -----------------------------------------------------------------------
    // Convenience: stop and remove all registered transports.
    // -----------------------------------------------------------------------
    void stop_all() noexcept
    {
        std::lock_guard lock(m_mutex);
        for (auto &[devid, transport] : m_transports)
            transport->stop();
        m_transports.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<UdpIsoTransport>>      m_transports;
    std::unordered_map<uint32_t, std::shared_ptr<audio::OpusTranscoder>> m_transcoders;
};

} // namespace usbip::transport
