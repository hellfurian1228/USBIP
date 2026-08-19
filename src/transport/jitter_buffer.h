/*
 * jitter_buffer.h
 *
 * Lock-free sliding-window jitter buffer for incoming UDP ISO frames.
 *
 * Design:
 *  - Fixed-capacity ring of pre-allocated slots (no heap in hot path).
 *  - Each slot holds one reassembled ISO URB payload keyed by seq_number.
 *  - Consumer calls pop_ready() on a periodic timer (e.g., every 1 ms).
 *    If the expected sequence slot is filled, it is returned immediately.
 *    If the slot is still empty when the deadline expires, a zero-filled
 *    "comfort noise" buffer is returned so the USB stack never stalls.
 *  - Thread-safe: one producer thread (UDP recv loop), one consumer thread
 *    (USB dispatch loop). Uses std::atomic for slot state.
 */

#pragma once

#include "udp_iso_frame.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <optional>
#include <span>

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Slot capacity: must be a power of two for cheap modulo via bitmask.
// 64 slots × up to ~8 KB each = 512 KB pre-allocated per device session.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kJitterSlots = 64;
static_assert((kJitterSlots & (kJitterSlots - 1)) == 0,
              "kJitterSlots must be a power of two");

inline constexpr std::size_t kSlotDataSize = 8192; // bytes per slot

// ---------------------------------------------------------------------------
// Slot state machine
// ---------------------------------------------------------------------------
enum class SlotState : uint8_t
{
    Empty,    ///< Available for a new incoming packet
    Filling,  ///< Producer is writing into this slot (transient)
    Ready,    ///< Payload is complete and ready for consumption
};

// ---------------------------------------------------------------------------
// A single ring-buffer slot
// ---------------------------------------------------------------------------
struct alignas(64) JitterSlot  // cache-line aligned to avoid false sharing
{
    std::atomic<SlotState> state{SlotState::Empty};
    uint32_t seq_number{0};
    uint32_t urb_id{0};
    uint16_t frame_count{0};
    uint16_t data_length{0};
    std::array<uint8_t, kSlotDataSize> data{};

    void reset() noexcept
    {
        seq_number  = 0;
        urb_id      = 0;
        frame_count = 0;
        data_length = 0;
        state.store(SlotState::Empty, std::memory_order_release);
    }
};

// ---------------------------------------------------------------------------
// Reassembled ISO frame returned to the caller
// ---------------------------------------------------------------------------
struct IsoFrame
{
    uint32_t seq_number{};
    uint32_t urb_id{};
    uint16_t frame_count{};
    uint16_t data_length{};
    bool     is_comfort_noise{false}; ///< true if synthesised due to timeout
    std::array<uint8_t, kSlotDataSize> data{};
};

// ---------------------------------------------------------------------------
// JitterBuffer
// ---------------------------------------------------------------------------
class JitterBuffer
{
public:
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    explicit JitterBuffer(Duration window = Duration{4}) noexcept
        : m_window(window)
    {
        // Pre-zero all slots (already zero-initialised by default, but be explicit)
        for (auto &slot : m_ring)
            slot.reset();
    }

    // Non-copyable, non-movable (atomic members)
    JitterBuffer(const JitterBuffer &) = delete;
    JitterBuffer &operator=(const JitterBuffer &) = delete;

    // -----------------------------------------------------------------------
    // Producer API (called from UDP receive thread)
    // -----------------------------------------------------------------------

    /**
     * Push an incoming ISO frame into the buffer.
     * @return false if the slot is already occupied (packet too late / duplicate).
     */
    bool push(const UdpIsoFrameHeader &hdr,
              std::span<const uint8_t>  payload) noexcept
    {
        // Drop packets that are already late or too far in the future to prevent wrap-around collision
        const uint32_t next = m_next_seq.load(std::memory_order_relaxed);
        const int32_t diff = static_cast<int32_t>(hdr.seq_number - next);
        if (diff < 0 || diff >= static_cast<int32_t>(kJitterSlots)) {
            return false;
        }

        const std::size_t idx = slot_index(hdr.seq_number);
        JitterSlot &slot = m_ring[idx];

        // Expect the slot to be empty; claim it atomically
        SlotState expected = SlotState::Empty;
        if (!slot.state.compare_exchange_strong(
                expected, SlotState::Filling,
                std::memory_order_acquire,
                std::memory_order_relaxed))
        {
            // Slot busy (duplicate or very late packet) – drop
            return false;
        }

        // Write payload (no allocation)
        slot.seq_number  = hdr.seq_number;
        slot.urb_id      = hdr.urb_id;
        slot.frame_count = hdr.frame_count;

        const std::size_t copy_len =
            (payload.size() <= kSlotDataSize) ? payload.size() : kSlotDataSize;
        std::memcpy(slot.data.data(), payload.data(), copy_len);
        slot.data_length = static_cast<uint16_t>(copy_len);

        slot.state.store(SlotState::Ready, std::memory_order_release);
        return true;
    }

    // -----------------------------------------------------------------------
    // Consumer API (called from USB dispatch thread on a periodic timer)
    // -----------------------------------------------------------------------

    /**
     * Attempt to pop the next expected frame.
     *
     * If the slot is Ready, returns it immediately.
     * If the slot is still Empty after the jitter window expires,
     * returns a zero-filled comfort-noise frame so the USB stack
     * never stalls waiting for a missing microframe.
     *
     * @param deadline  Absolute time by which the frame must arrive.
     *                  Pass Clock::now() + m_window on first call per frame.
     */
    IsoFrame pop_ready(Clock::time_point deadline) noexcept
    {
        const uint32_t seq = m_next_seq.load(std::memory_order_relaxed);
        const std::size_t idx = slot_index(seq);
        JitterSlot &slot = m_ring[idx];

        // Spin-wait until Ready or deadline exceeded
        while (slot.state.load(std::memory_order_acquire) != SlotState::Ready) {
            if (Clock::now() >= deadline)
                return make_comfort_noise(seq);
        }

        // Consume the slot
        IsoFrame frame;
        frame.seq_number    = slot.seq_number;
        frame.urb_id        = slot.urb_id;
        frame.frame_count   = slot.frame_count;
        frame.data_length   = slot.data_length;
        frame.is_comfort_noise = false;
        std::memcpy(frame.data.data(), slot.data.data(), slot.data_length);

        slot.reset();
        m_next_seq.fetch_add(1, std::memory_order_relaxed);
        return frame;
    }

    /**
     * Non-blocking poll: returns the next frame only if it is already Ready.
     */
    std::optional<IsoFrame> try_pop() noexcept
    {
        const uint32_t seq = m_next_seq.load(std::memory_order_relaxed);
        const std::size_t idx = slot_index(seq);
        JitterSlot &slot = m_ring[idx];

        if (slot.state.load(std::memory_order_acquire) != SlotState::Ready)
            return std::nullopt;

        IsoFrame frame;
        frame.seq_number    = slot.seq_number;
        frame.urb_id        = slot.urb_id;
        frame.frame_count   = slot.frame_count;
        frame.data_length   = slot.data_length;
        frame.is_comfort_noise = false;
        std::memcpy(frame.data.data(), slot.data.data(), slot.data_length);

        slot.reset();
        m_next_seq.fetch_add(1, std::memory_order_relaxed);
        return frame;
    }

    // -----------------------------------------------------------------------
    // Flush all pending slots (call on device disconnect)
    // -----------------------------------------------------------------------
    void flush() noexcept
    {
        for (auto &slot : m_ring)
            slot.reset();
        // Do NOT reset m_next_seq – the session is being torn down anyway.
    }

    Duration window() const noexcept { return m_window; }

    uint32_t next_expected_seq() const noexcept
    {
        return m_next_seq.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t slot_index(uint32_t seq) noexcept
    {
        return seq & (kJitterSlots - 1);
    }

    static IsoFrame make_comfort_noise(uint32_t seq) noexcept
    {
        IsoFrame frame{};
        frame.seq_number       = seq;
        frame.is_comfort_noise = true;
        // data is already zero-initialised
        return frame;
    }

    std::array<JitterSlot, kJitterSlots> m_ring{};
    std::atomic<uint32_t>                m_next_seq{0};
    Duration                             m_window;
};

} // namespace usbip::transport
