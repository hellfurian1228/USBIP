#pragma once

#include <cstdint>
#include <functional>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace usbip::audio
{

// UAC delivers 1 ms frames; Opus requires 10 ms (960 samples @ 48 kHz stereo).
inline constexpr int   kSampleRate    = 48000;
inline constexpr int   kChannels      = 2;
inline constexpr int   kFrameSamples  = 960;                          // 20 ms
inline constexpr int   kFrameBytes    = kFrameSamples * kChannels * sizeof(int16_t); // 3840
inline constexpr int   kMaxOpusPacket = 4000;                         // bytes

// Called with each encoded Opus packet; runs on the caller's thread.
using EncodedCallback = std::function<void(const uint8_t *data, int len)>;

class OpusTranscoder
{
public:
    explicit OpusTranscoder(EncodedCallback on_encoded);
    ~OpusTranscoder();

    OpusTranscoder(const OpusTranscoder &) = delete;
    OpusTranscoder &operator=(const OpusTranscoder &) = delete;

    // Feed raw interleaved int16 PCM bytes from a UAC URB payload.
    // Encodes and fires on_encoded whenever a full Opus frame accumulates.
    void encode_pcm(const uint8_t *pcm_bytes, int byte_count);

    // Decode a single Opus packet into interleaved int16 PCM.
    // Returns number of samples per channel decoded, or negative on error.
    int decode_opus(const uint8_t *opus_data, int opus_len,
                    int16_t *pcm_out, int max_samples_per_channel);

    // Discard any partially accumulated PCM (call on stream reset).
    void flush();

    bool valid() const noexcept { return m_enc && m_dec; }

private:
    OpusEncoder          *m_enc{nullptr};
    OpusDecoder          *m_dec{nullptr};
    EncodedCallback       m_on_encoded;
    std::vector<int16_t>  m_accum;                  // PCM accumulation ring
    uint8_t               m_packet_buf[kMaxOpusPacket]{};
};

} // namespace usbip::audio
