#include "opus_transcoder.h"

#include <opus.h>

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace usbip::audio
{

OpusTranscoder::OpusTranscoder(EncodedCallback on_encoded)
    : m_on_encoded(std::move(on_encoded))
{
    int err = OPUS_OK;

    m_enc = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK)
        throw std::runtime_error(std::string("opus_encoder_create: ") + opus_strerror(err));

    // Target ~128 kbps for stereo audio; caller may override via opus_encoder_ctl.
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(128000));

    m_dec = opus_decoder_create(kSampleRate, kChannels, &err);
    if (err != OPUS_OK) {
        opus_encoder_destroy(m_enc);
        m_enc = nullptr;
        throw std::runtime_error(std::string("opus_decoder_create: ") + opus_strerror(err));
    }

    m_accum.reserve(kFrameSamples * kChannels * 4); // pre-allocate ~4 frames
}

OpusTranscoder::~OpusTranscoder()
{
    if (m_enc) opus_encoder_destroy(m_enc);
    if (m_dec) opus_decoder_destroy(m_dec);
}

void OpusTranscoder::encode_pcm(const uint8_t *pcm_bytes, int byte_count)
{
    if (!m_enc || byte_count <= 0)
        return;

    // Reinterpret raw bytes as int16 samples and append to accumulator.
    const int sample_count = byte_count / static_cast<int>(sizeof(int16_t));
    const auto *samples = reinterpret_cast<const int16_t *>(pcm_bytes);
    m_accum.insert(m_accum.end(), samples, samples + sample_count);

    // Drain complete frames from the front of the accumulator.
    constexpr int kFrameSampleTotal = kFrameSamples * kChannels;
    while (static_cast<int>(m_accum.size()) >= kFrameSampleTotal) {
        const opus_int32 encoded = opus_encode(
            m_enc,
            m_accum.data(),
            kFrameSamples,
            m_packet_buf,
            kMaxOpusPacket);

        if (encoded > 0 && m_on_encoded)
            m_on_encoded(m_packet_buf, static_cast<int>(encoded));

        m_accum.erase(m_accum.begin(), m_accum.begin() + kFrameSampleTotal);
    }
}

int OpusTranscoder::decode_opus(const uint8_t *opus_data, int opus_len,
                                int16_t *pcm_out, int max_samples_per_channel)
{
    if (!m_dec)
        return OPUS_INVALID_STATE;

    return opus_decode(m_dec, opus_data, opus_len, pcm_out, max_samples_per_channel, 0);
}

void OpusTranscoder::flush()
{
    m_accum.clear();
}

} // namespace usbip::audio
