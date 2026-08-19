/*
 * transport_policy.h
 *
 * Thread-safe Transport Policy Registry.
 * Maps Device ID (devid) or VID:PID to a TransportPolicy.
 * Determines whether ISO URBs are routed over UDP (HYBRID_TCP_UDP)
 * or kept entirely on TCP (TCP_ONLY).
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <optional>

namespace usbip::transport
{

// ---------------------------------------------------------------------------
// Transport policy per device
// ---------------------------------------------------------------------------

enum class TransportPolicy : uint8_t
{
    TCP_ONLY,       ///< All URBs (including ISO) travel over TCP
    HYBRID_TCP_UDP  ///< ISO URBs travel over UDP; all others stay on TCP
};

// ---------------------------------------------------------------------------
// Per-device network configuration
// ---------------------------------------------------------------------------

struct DeviceNetworkConfig
{
    TransportPolicy policy = TransportPolicy::TCP_ONLY;

    // UDP endpoint (populated when policy == HYBRID_TCP_UDP)
    uint16_t udp_port = 0;   ///< Remote UDP port for ISO traffic

    // Jitter buffer window in milliseconds (2-5 ms recommended)
    uint32_t jitter_window_ms = 4;
};

// ---------------------------------------------------------------------------
// VID:PID key helper
// ---------------------------------------------------------------------------

struct VidPid
{
    uint16_t vid;
    uint16_t pid;

    bool operator==(const VidPid &o) const noexcept
    {
        return vid == o.vid && pid == o.pid;
    }
};

struct VidPidHash
{
    std::size_t operator()(const VidPid &v) const noexcept
    {
        return std::hash<uint32_t>{}((static_cast<uint32_t>(v.vid) << 16) | v.pid);
    }
};

// ---------------------------------------------------------------------------
// Thread-safe registry
// ---------------------------------------------------------------------------

class TransportPolicyRegistry
{
public:
    // Singleton accessor
    static TransportPolicyRegistry &instance() noexcept
    {
        static TransportPolicyRegistry s_instance;
        return s_instance;
    }

    // -----------------------------------------------------------------------
    // Set policy by devid (runtime device identifier used by USBIP protocol)
    // -----------------------------------------------------------------------
    void set(uint32_t devid, DeviceNetworkConfig cfg) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_by_devid[devid] = cfg;
    }

    // -----------------------------------------------------------------------
    // Set policy by VID:PID (applied to all devices with matching VID:PID
    // when no devid-specific entry exists)
    // -----------------------------------------------------------------------
    void set(VidPid vp, DeviceNetworkConfig cfg) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_by_vidpid[vp] = cfg;
    }

    // -----------------------------------------------------------------------
    // Remove devid entry (e.g., on device disconnect)
    // -----------------------------------------------------------------------
    void remove(uint32_t devid) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_by_devid.erase(devid);
    }

    // -----------------------------------------------------------------------
    // Lookup: devid takes priority over VID:PID
    // -----------------------------------------------------------------------
    DeviceNetworkConfig get(uint32_t devid, VidPid vp = {}) const noexcept
    {
        std::lock_guard lock(m_mutex);

        if (auto it = m_by_devid.find(devid); it != m_by_devid.end())
            return it->second;

        if (vp.vid || vp.pid) {
            if (auto it = m_by_vidpid.find(vp); it != m_by_vidpid.end())
                return it->second;
        }

        return {}; // default: TCP_ONLY
    }

    TransportPolicy policy_for(uint32_t devid, VidPid vp = {}) const noexcept
    {
        return get(devid, vp).policy;
    }

    bool is_hybrid(uint32_t devid, VidPid vp = {}) const noexcept
    {
        return policy_for(devid, vp) == TransportPolicy::HYBRID_TCP_UDP;
    }

private:
    TransportPolicyRegistry() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, DeviceNetworkConfig>  m_by_devid;
    std::unordered_map<VidPid, DeviceNetworkConfig, VidPidHash> m_by_vidpid;
};

} // namespace usbip::transport
