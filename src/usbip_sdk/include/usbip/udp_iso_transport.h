/*
 * Copyright (c) 2026 Vadym Hrynchyshyn & Senior C++ Systems & Network Engineer
 * Dynamic Dual-Transport Layer for C++ USBIP implementation (UDP ISO Transport & Jitter Buffer)
 */

#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <iostream>
#include <cstring>
#include <cassert>

#include "transport_policy.h"
#include "proto.h"

namespace usbip
{

#pragma pack(push, 1)
/**
 * @brief Compact binary header (20 bytes) for UDP Isochronous transfers.
 */
struct ShortUdpIsoHeader
{
    uint32_t seq_number;    // Monotonically increasing sequence ID
    uint32_t seq_ack;       // Heartbeat / Acknowledgement
    uint32_t urb_id;        // Matches USBIP URB sequence number (seqnum)
    uint8_t  ep_num;        // Target USB Endpoint number
    uint8_t  padding[3];    // Alignment padding (explictly defined)
    uint16_t frame_count;   // Number of ISO frames in payload
    uint16_t total_length;  // ISO payload byte count
};
#pragma pack(pop)

static_assert(sizeof(ShortUdpIsoHeader) == 20, "ShortUdpIsoHeader size must be exactly 20 bytes for optimal network packing.");

// Forward declared network-to-host/host-to-network helper functions for ShortUdpIsoHeader
inline ShortUdpIsoHeader byteswap(const ShortUdpIsoHeader& h)
{
    ShortUdpIsoHeader swapped;
    swapped.seq_number   = htonl(h.seq_number);
    swapped.seq_ack      = htonl(h.seq_ack);
    swapped.urb_id       = htonl(h.urb_id);
    swapped.ep_num       = h.ep_num;
    swapped.padding[0]   = h.padding[0];
    swapped.padding[1]   = h.padding[1];
    swapped.padding[2]   = h.padding[2];
    swapped.frame_count  = htons(h.frame_count);
    swapped.total_length = htons(h.total_length);
    return swapped;
}

/**
 * @brief Pre-allocated entry in the JitterBuffer ring buffer slot pool.
 * No dynamic memory allocations occur inside the processing loop.
 */
struct JitterBufferSlot
{
    uint32_t seq_number = 0;
    uint32_t urb_id = 0;
    uint8_t ep_num = 0;
    uint16_t frame_count = 0;
    uint16_t total_length = 0;
    bool occupied = false;
    bool dispatched = false;
    std::vector<uint8_t> payload_buffer; // Pre-allocated storage
};

/**
 * @brief Sliding window Jitter and Reassembly Buffer for low-latency UDP streams.
 * Arranges incoming UDP ISO packets by seq_number and handles packet loss/timeouts.
 */
class JitterBuffer
{
public:
    explicit JitterBuffer(uint32_t max_slots = 256, uint32_t max_payload_size = 65536)
        : m_max_slots(max_slots)
        , m_next_expected_seq(0)
        , m_buffer_pool(max_slots)
    {
        // Pre-allocate buffer pools to completely avoid heap allocations in the hot path
        for (uint32_t i = 0; i < max_slots; ++i) {
            m_buffer_pool[i].payload_buffer.resize(max_payload_size);
            m_buffer_pool[i].occupied = false;
            m_buffer_pool[i].dispatched = false;
        }
    }

    ~JitterBuffer() = default;

    /**
     * @brief Reset state and clear slots safely.
     */
    void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& slot : m_buffer_pool) {
            slot.occupied = false;
            slot.dispatched = false;
            slot.seq_number = 0;
            slot.urb_id = 0;
        }
        m_next_expected_seq = 0;
    }

    /**
     * @brief Inserts an incoming UDP packet payload into its designated ring buffer slot based on sequence ID.
     */
    void insert(const ShortUdpIsoHeader& header, const uint8_t* payload, size_t payload_len)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Discard ancient packets arriving late
        if (header.seq_number < m_next_expected_seq && (m_next_expected_seq - header.seq_number) < (m_max_slots / 2)) {
            return; 
        }

        uint32_t index = header.seq_number % m_max_slots;
        auto& slot = m_buffer_pool[index];

        // Check if we are overwriting an undispatched packet
        if (slot.occupied && !slot.dispatched && slot.seq_number != header.seq_number) {
            // Drop older packet to allow sliding window to advance
            slot.occupied = false;
        }

        // Fast payload copy into pre-allocated memory (zero allocation!)
        size_t bytes_to_copy = (std::min)(payload_len, slot.payload_buffer.size());
        std::memcpy(slot.payload_buffer.data(), payload, bytes_to_copy);

        slot.seq_number = header.seq_number;
        slot.urb_id = header.urb_id;
        slot.ep_num = header.ep_num;
        slot.frame_count = header.frame_count;
        slot.total_length = static_cast<uint16_t>(bytes_to_copy);
        slot.occupied = true;
        slot.dispatched = false;

        m_cv.notify_all();
    }

    /**
     * @brief Fetches payload for the requested sequence ID. If late or missing,
     * it times out and outputs a zero-filled buffer to preserve microframe timing.
     */
    bool fetch(uint32_t seq_number, uint8_t* out_buffer, uint16_t max_length, uint16_t& out_actual_length, uint32_t timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        uint32_t index = seq_number % m_max_slots;
        auto& slot = m_buffer_pool[index];

        // Wait with a strict real-time timeout window for the specific packet to arrive
        auto timeout_duration = std::chrono::milliseconds(timeout_ms);
        m_cv.wait_for(lock, timeout_duration, [this, seq_number, &slot]() {
            return slot.occupied && slot.seq_number == seq_number;
        });

        if (slot.occupied && slot.seq_number == seq_number) {
            // Packet arrived in time! Assemble payload.
            uint16_t bytes_to_copy = (std::min)(max_length, slot.total_length);
            std::memcpy(out_buffer, slot.payload_buffer.data(), bytes_to_copy);
            out_actual_length = bytes_to_copy;

            slot.dispatched = true;
            slot.occupied = false;
            m_next_expected_seq = seq_number + 1;
            return true;
        }

        // Timeout or packet loss happened! Emit a zero-filled timing compensation buffer
        // to prevent virtual USB bus driver from hanging or losing synchrony.
        std::memset(out_buffer, 0, max_length);
        out_actual_length = max_length;
        m_next_expected_seq = seq_number + 1;

        // Clean up the stale slots that were missed
        slot.occupied = false;
        slot.dispatched = true;

        return false; // Packet lost but timing is strictly maintained
    }

private:
    uint32_t m_max_slots;
    uint32_t m_next_expected_seq;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<JitterBufferSlot> m_buffer_pool;
};

/**
 * @brief Manages a thread-safe UDP Isochronous transport channel tied tightly to a TCP control session.
 */
class UdpIsoTransportSession
{
public:
    UdpIsoTransportSession(const std::string& remote_ip, uint16_t local_udp_port, uint16_t remote_udp_port)
        : m_remote_ip(remote_ip)
        , m_local_port(local_udp_port)
        , m_remote_port(remote_udp_port)
        , m_socket(INVALID_SOCKET)
        , m_running(false)
        , m_next_tx_seq(0)
    {
        m_jitter_buffer = std::make_unique<JitterBuffer>();
    }

    ~UdpIsoTransportSession()
    {
        stop();
    }

    /**
     * @brief Initialize Winsock / sockets and start high-priority receiver loop.
     */
    bool start()
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (m_running) return true;

        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            std::cerr << "Failed to create DGRAM socket for UDP ISO tunnel. Error: " << WSAGetLastError() << "\n";
            return false;
        }

        // Allow address reuse to recover from fast restarts
        BOOL reuse = TRUE;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        // Configure receive buffer size to prevent kernel-level UDP drops
        int buffer_size = 4 * 1024 * 1024; // 4MB receive window
        setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer_size), sizeof(buffer_size));

        // Bind local socket
        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;
        local_addr.sin_port = htons(m_local_port);

        if (bind(m_socket, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR) {
            std::cerr << "Failed to bind local DGRAM socket. Error: " << WSAGetLastError() << "\n";
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }

        // Configure remote endpoint address structure
        std::memset(&m_remote_addr, 0, sizeof(m_remote_addr));
        m_remote_addr.sin_family = AF_INET;
        m_remote_addr.sin_port = htons(m_remote_port);
        inet_pton(AF_INET, m_remote_ip.c_str(), &m_remote_addr.sin_addr);

        m_running = true;
        m_receiver_thread = std::thread(&UdpIsoTransportSession::receive_loop, this);

        // Raise thread priority for low latency handling of real-time packets
        #ifdef _WIN32
        SetThreadPriority(m_receiver_thread.native_handle(), THREAD_PRIORITY_HIGHEST);
        #endif

        return true;
    }

    /**
     * @brief Tear down the UDP session and release all socket handles.
     */
    void stop()
    {
        m_running = false;

        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket); // Interrupts blocking recvfrom immediately
        }

        if (m_receiver_thread.joinable()) {
            m_receiver_thread.join();
        }

        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_socket = INVALID_SOCKET;
        m_jitter_buffer->reset();
        m_next_tx_seq = 0;
    }

    /**
     * @brief Dynamic transmission of an Isochronous payload over the lightweight UDP framing layer.
     */
    bool send_iso_packet(uint32_t urb_id, uint8_t ep_num, uint16_t frame_count, const uint8_t* payload, uint16_t length)
    {
        if (m_socket == INVALID_SOCKET || !m_running) return false;

        ShortUdpIsoHeader h{};
        h.seq_number   = m_next_tx_seq.fetch_add(1, std::memory_order_relaxed);
        h.seq_ack      = 0;
        h.urb_id       = urb_id;
        h.ep_num       = ep_num;
        h.frame_count  = frame_count;
        h.total_length = length;

        ShortUdpIsoHeader wire_header = byteswap(h);

        // Stack-allocated packet framing (Header + Payload) to prevent dynamic allocations
        constexpr size_t max_stack_packet = 16384;
        alignas(ShortUdpIsoHeader) uint8_t stack_packet[max_stack_packet];
        
        size_t total_packet_size = sizeof(ShortUdpIsoHeader) + length;
        if (total_packet_size > max_stack_packet) {
            // Fallback safely if packet exceeds the high MTU stack limit
            auto heap_packet = std::make_unique<uint8_t[]>(total_packet_size);
            std::memcpy(heap_packet.get(), &wire_header, sizeof(ShortUdpIsoHeader));
            std::memcpy(heap_packet.get() + sizeof(ShortUdpIsoHeader), payload, length);
            
            int sent = sendto(m_socket, reinterpret_cast<const char*>(heap_packet.get()), static_cast<int>(total_packet_size), 0,
                              reinterpret_cast<const sockaddr*>(&m_remote_addr), sizeof(m_remote_addr));
            return sent != SOCKET_ERROR;
        }

        std::memcpy(stack_packet, &wire_header, sizeof(ShortUdpIsoHeader));
        std::memcpy(stack_packet + sizeof(ShortUdpIsoHeader), payload, length);

        int sent = sendto(m_socket, reinterpret_cast<const char*>(stack_packet), static_cast<int>(total_packet_size), 0,
                          reinterpret_cast<const sockaddr*>(&m_remote_addr), sizeof(m_remote_addr));
        return sent != SOCKET_ERROR;
    }

    /**
     * @brief Access the jitter buffer for dispatch of incoming payloads.
     */
    JitterBuffer& jitter_buffer() { return *m_jitter_buffer; }

private:
    /**
     * @brief Fast socket read loop running in a separate worker thread.
     */
    void receive_loop()
    {
        std::vector<uint8_t> recv_buffer(65536 + sizeof(ShortUdpIsoHeader));
        sockaddr_in sender_addr{};
        int sender_addr_len = sizeof(sender_addr);

        while (m_running) {
            int received = recvfrom(m_socket, reinterpret_cast<char*>(recv_buffer.data()), static_cast<int>(recv_buffer.size()), 0,
                                    reinterpret_cast<sockaddr*>(&sender_addr), &sender_addr_len);

            if (received == SOCKET_ERROR || received < static_cast<int>(sizeof(ShortUdpIsoHeader))) {
                if (m_running) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                continue;
            }

            // Parse short header
            ShortUdpIsoHeader wire_header;
            std::memcpy(&wire_header, recv_buffer.data(), sizeof(ShortUdpIsoHeader));

            // Byteswap header fields
            ShortUdpIsoHeader host_header;
            host_header.seq_number   = ntohl(wire_header.seq_number);
            host_header.seq_ack      = ntohl(wire_header.seq_ack);
            host_header.urb_id       = ntohl(wire_header.urb_id);
            host_header.ep_num       = wire_header.ep_num;
            host_header.frame_count  = ntohs(wire_header.frame_count);
            host_header.total_length = ntohs(wire_header.total_length);

            size_t payload_len = received - sizeof(ShortUdpIsoHeader);
            if (payload_len > host_header.total_length) {
                payload_len = host_header.total_length;
            }

            const uint8_t* payload_ptr = recv_buffer.data() + sizeof(ShortUdpIsoHeader);

            // Thread-safe insertion into Jitter Buffer
            m_jitter_buffer->insert(host_header, payload_ptr, payload_len);
        }
    }

    std::string m_remote_ip;
    uint16_t m_local_port;
    uint16_t m_remote_port;
    SOCKET m_socket;
    std::atomic<bool> m_running;
    std::atomic<uint32_t> m_next_tx_seq;
    sockaddr_in m_remote_addr;

    std::thread m_receiver_thread;
    std::unique_ptr<JitterBuffer> m_jitter_buffer;
    std::mutex m_session_mutex;
};

/**
 * @brief Dynamic Demuxing / Routing Helper. Intercepts incoming USBIP request packets,
 * and routes Isochronous URBs over the high-performance UDP channel when enabled.
 */
class UsbipTransportDemuxer
{
public:
    /**
     * @brief Intercepts packet submitting logic and decides transport path.
     * @return true if ISO UDP transport is chosen, false to fall back to default TCP.
     */
    static bool should_route_iso_over_udp(const std::string& dev_id, uint16_t vid, uint16_t pid, 
                                          const header& usbip_hdr, int number_of_packets)
    {
        // 1. Get transport policy for the device
        auto config = TransportPolicyRegistry::instance().get_device_config(dev_id, vid, pid);
        if (config.policy != TransportPolicy::HYBRID_TCP_UDP) {
            return false;
        }

        // 2. Validate request is CMD_SUBMIT and is an ISO URB
        // In the USBIP standard, Isochronous packets are marked by number_of_packets > 0,
        // since Bulk/Control/Interrupt has -1 (number_of_packets_non_isoch)
        bool is_iso = (usbip_hdr.command == CMD_SUBMIT) && (number_of_packets > 0);
        return is_iso;
    }
};

} // namespace usbip
