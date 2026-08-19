/*
 * Copyright (c) 2026 Vadym Hrynchyshyn & Senior C++ Systems & Network Engineer
 * Dynamic USBIP Dual-Transport Policy Layer and Configuration Registry.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <cstdint>

namespace usbip
{

enum class TransportPolicy : uint8_t
{
    TCP_ONLY = 0,
    HYBRID_TCP_UDP = 1
};

struct DeviceNetworkConfig
{
    TransportPolicy policy = TransportPolicy::TCP_ONLY;
    std::string server_ip;
    uint16_t tcp_port = 3240;
    uint16_t udp_port = 3241; // Dedicated low-latency UDP port for Isochronous streams
    uint32_t jitter_buffer_size = 256; // Slots in the sliding window ring buffer
    uint32_t jitter_timeout_ms = 5;    // Max latency budget to wait for late ISO frames before zero-filling
};

/**
 * @brief Thread-safe global registry mapping USB devices (indexed by VID:PID or Device ID string)
 * to their dynamic transport and network configurations.
 */
class TransportPolicyRegistry
{
public:
    static TransportPolicyRegistry& instance()
    {
        static TransportPolicyRegistry inst;
        return inst;
    }

    // Disable copy/move sematics
    TransportPolicyRegistry(const TransportPolicyRegistry&) = delete;
    TransportPolicyRegistry& operator=(const TransportPolicyRegistry&) = delete;
    TransportPolicyRegistry(TransportPolicyRegistry&&) = delete;
    TransportPolicyRegistry& operator=(TransportPolicyRegistry&&) = delete;

    /**
     * @brief Set or update configuration for a specific device by VID:PID.
     */
    void set_device_config(uint16_t vid, uint16_t pid, const DeviceNetworkConfig& config)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        uint32_t key = (static_cast<uint32_t>(vid) << 16) | pid;
        m_configs_by_vid_pid[key] = config;
    }

    /**
     * @brief Set or update configuration for a specific device by Device ID string (e.g., "1-1").
     */
    void set_device_config(const std::string& device_id, const DeviceNetworkConfig& config)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_configs_by_id[device_id] = config;
    }

    /**
     * @brief Look up network configuration for a device. Checks Device ID first, then falls back to VID:PID.
     */
    DeviceNetworkConfig get_device_config(const std::string& device_id, uint16_t vid = 0, uint16_t pid = 0) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        // 1. Try Device ID first
        if (!device_id.empty()) {
            auto it = m_configs_by_id.find(device_id);
            if (it != m_configs_by_id.end()) {
                return it->second;
            }
        }

        // 2. Fall back to VID:PID mapping
        if (vid != 0 || pid != 0) {
            uint32_t key = (static_cast<uint32_t>(vid) << 16) | pid;
            auto it = m_configs_by_vid_pid.find(key);
            if (it != m_configs_by_vid_pid.end()) {
                return it->second;
            }
        }

        // Return default configuration (TCP_ONLY)
        return DeviceNetworkConfig{};
    }

    /**
     * @brief Update policy dynamically for active sessions.
     */
    void update_policy(const std::string& device_id, TransportPolicy policy)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!device_id.empty()) {
            m_configs_by_id[device_id].policy = policy;
        }
    }

    /**
     * @brief Clear all registered transport policies.
     */
    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_configs_by_vid_pid.clear();
        m_configs_by_id.clear();
    }

private:
    TransportPolicyRegistry() = default;
    ~TransportPolicyRegistry() = default;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<uint32_t, DeviceNetworkConfig> m_configs_by_vid_pid;
    std::unordered_map<std::string, DeviceNetworkConfig> m_configs_by_id;
};

} // namespace usbip
