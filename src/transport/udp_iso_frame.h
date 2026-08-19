/*
 * udp_iso_frame.h
 *
 * Compact binary framing header for UDP ISO URB payloads.
 * Max 20 bytes (fits in a single cache line alongside payload pointer).
 *
 * Wire layout (all fields in network byte order / big-endian):
 *
 *  0               1               2               3
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           seq_number                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           seq_ack                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            urb_id                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    ep_num     |            frame_count        |               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               +
 * |                        total_length           |               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Total: 4+4+4+1+2+2 = 17 bytes.
 */

#pragma once

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#  include <WinSock2.h>   // htonl / ntohl / htons / ntohs
#else
#  include <arpa/inet.h>
#endif

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Wire header – packed, no padding between fields
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct UdpIsoFrameHeader {
    uint32_t seq_number;
    uint32_t seq_ack;
    uint32_t urb_id;
    uint8_t  ep_num;
    uint16_t frame_count;
    uint16_t total_length;
};
#pragma pack(pop)

static_assert(sizeof(UdpIsoFrameHeader) == 17,
              "UdpIsoFrameHeader must be exactly 17 bytes on the wire");

// ---------------------------------------------------------------------------
// Encode: fill header fields and convert to network byte order in-place.
// Call this on a local copy before sendto().
// ---------------------------------------------------------------------------
inline void encode(UdpIsoFrameHeader &h) noexcept
{
    h.seq_number   = htonl(h.seq_number);
    h.seq_ack      = htonl(h.seq_ack);
    h.urb_id       = htonl(h.urb_id);
    // ep_num is 1 byte – no conversion needed
    h.frame_count  = htons(h.frame_count);
    h.total_length = htons(h.total_length);
}

// ---------------------------------------------------------------------------
// Decode: convert received header from network byte order to host order.
// Call this immediately after recvfrom().
// ---------------------------------------------------------------------------
inline void decode(UdpIsoFrameHeader &h) noexcept
{
    h.seq_number   = ntohl(h.seq_number);
    h.seq_ack      = ntohl(h.seq_ack);
    h.urb_id       = ntohl(h.urb_id);
    h.frame_count  = ntohs(h.frame_count);
    h.total_length = ntohs(h.total_length);
}

// ---------------------------------------------------------------------------
// Maximum UDP datagram payload we will ever send (header + ISO data).
// USB FS/HS ISO max transfer = 1023/1024 bytes per packet × 8 microframes.
// We cap at 64 KB to stay within UDP limits.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kMaxUdpPayload = 65507; // max UDP payload (IPv4)
inline constexpr std::size_t kMaxIsoDatagramSize =
    sizeof(UdpIsoFrameHeader) + kMaxUdpPayload;

} // namespace usbip::transport