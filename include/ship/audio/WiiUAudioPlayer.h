#pragma once
#include "AudioPlayer.h"

#include <cstdint>

namespace Ship {
/**
 * @brief AudioPlayer implementation backed by the Wii U's AX audio hardware.
 *
 * SDL is not available for the Wii U, so the console drives AX directly. Two looping
 * AX voices — one per output channel — read from a pair of ring buffers that DoPlay()
 * writes ahead of the hardware's current read offset; Buffered() reports the gap
 * between the two so the audio system can pace itself as it does on every other
 * backend.
 *
 * Only stereo output is supported: AX can drive 5.1, but the surround path is not
 * wired up, so a 5.1 channel setting is downmixed to the front pair.
 */
class WiiUAudioPlayer final : public AudioPlayer {
  public:
    /**
     * @brief Constructs a WiiUAudioPlayer with the given audio settings.
     * @param settings Sample rate, buffer size, desired buffered frames, and channel mode.
     */
    WiiUAudioPlayer(AudioSettings settings) : AudioPlayer(settings) {
    }
    ~WiiUAudioPlayer();

    /**
     * @brief Returns the number of audio frames queued ahead of the AX read offset.
     */
    int Buffered() override;

  protected:
    /**
     * @brief Initializes AX and acquires one looping voice per output channel.
     * @return true if AX came up and both voices were acquired.
     */
    bool DoInit() override;

    /**
     * @brief Releases the AX voices and their ring buffers, and shuts AX down.
     */
    void DoClose() override;

    /**
     * @brief De-interleaves PCM into the per-channel ring buffers ahead of AX.
     * @param buf Interleaved sample data.
     * @param len Length of @p buf in bytes.
     */
    void DoPlay(const uint8_t* buf, size_t len) override;

  private:
    /** @brief Returns the AX voice's current read offset, in samples. */
    uint32_t GetReadOffset();

    static constexpr int32_t gChannelCount = 2;    ///< Stereo only; AX surround is not wired up.
    static constexpr uint32_t gRingSamples = 8192; ///< Per-channel ring buffer length, in samples.

    void* mVoices[gChannelCount] = { nullptr }; ///< AXVoice*, kept opaque so the header stays devkit-free.
    int16_t* mRingBuffers[gChannelCount] = { nullptr };
    uint32_t mWriteOffset = 0; ///< Next sample index to write, in [0, gRingSamples).
    bool mAxInitialized = false;
};
} // namespace Ship
