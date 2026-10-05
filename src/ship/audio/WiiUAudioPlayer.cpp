#ifdef __WIIU__
#include "ship/audio/WiiUAudioPlayer.h"

#include <cstring>
#include <malloc.h>

#include <spdlog/spdlog.h>

#include <coreinit/cache.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>

// AX mixes at a fixed volume scale where 0x8000 is unity gain.
#define AX_UNITY_VOLUME 0x8000

// Voice priority. AX drops the lowest-priority voice when it runs out, and this is
// the only audio the game produces, so it claims the top of the range.
#define AX_VOICE_PRIORITY 31

namespace Ship {

WiiUAudioPlayer::~WiiUAudioPlayer() {
    DoClose();
}

bool WiiUAudioPlayer::DoInit() {
    if (!mAxInitialized) {
        AXInitParams params = {};
        params.renderer = AX_INIT_RENDERER_48KHZ;
        params.pipeline = AX_INIT_PIPELINE_SINGLE;
        AXInitWithParams(&params);
        mAxInitialized = true;
    }

    const float srcRatio = static_cast<float>(GetSampleRate()) / static_cast<float>(AXGetInputSamplesPerSec());

    for (int32_t channel = 0; channel < gChannelCount; channel++) {
        // AX reads the ring buffer by DMA, so it has to be cache-line aligned and
        // flushed by hand after every write.
        mRingBuffers[channel] = static_cast<int16_t*>(memalign(64, gRingSamples * sizeof(int16_t)));
        if (mRingBuffers[channel] == nullptr) {
            SPDLOG_ERROR("Failed to allocate the Wii U audio ring buffer for channel {}", channel);
            DoClose();
            return false;
        }

        memset(mRingBuffers[channel], 0, gRingSamples * sizeof(int16_t));
        DCFlushRange(mRingBuffers[channel], gRingSamples * sizeof(int16_t));

        AXVoice* voice = AXAcquireVoice(AX_VOICE_PRIORITY, nullptr, nullptr);
        if (voice == nullptr) {
            SPDLOG_ERROR("Failed to acquire an AX voice for channel {}", channel);
            DoClose();
            return false;
        }
        mVoices[channel] = voice;

        AXVoiceBegin(voice);

        AXSetVoiceType(voice, AX_VOICE_TYPE_UNKNOWN);

        AXVoiceVeData volume = {};
        volume.volume = AX_UNITY_VOLUME;
        AXSetVoiceVe(voice, &volume);

        // One voice per output channel: each is mixed into its own channel only, so
        // channel 0 lands on the left and channel 1 on the right.
        AXVoiceDeviceMixData mix[6] = {};
        mix[channel].bus[0].volume = AX_UNITY_VOLUME;
        AXSetVoiceDeviceMix(voice, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(voice, AX_DEVICE_TYPE_DRC, 0, mix);

        AXSetVoiceSrcType(voice, AX_VOICE_SRC_TYPE_LINEAR);
        AXSetVoiceSrcRatio(voice, srcRatio);

        // The voice loops over the whole ring forever; DoPlay() keeps writing fresh
        // samples in front of wherever AX has got to.
        AXVoiceOffsets offsets = {};
        offsets.dataType = AX_VOICE_FORMAT_LPCM16;
        offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
        offsets.loopOffset = 0;
        offsets.endOffset = gRingSamples - 1;
        offsets.currentOffset = 0;
        offsets.data = mRingBuffers[channel];
        AXSetVoiceOffsets(voice, &offsets);

        AXSetVoiceState(voice, AX_VOICE_STATE_PLAYING);

        AXVoiceEnd(voice);
    }

    // Start writing a full target's worth ahead of the read head so the first frames
    // do not underrun before the game gets into its stride.
    mWriteOffset = static_cast<uint32_t>(GetDesiredBuffered()) % gRingSamples;

    return true;
}

void WiiUAudioPlayer::DoClose() {
    for (int32_t channel = 0; channel < gChannelCount; channel++) {
        if (mVoices[channel] != nullptr) {
            AXVoice* voice = static_cast<AXVoice*>(mVoices[channel]);
            AXSetVoiceState(voice, AX_VOICE_STATE_STOPPED);
            AXFreeVoice(voice);
            mVoices[channel] = nullptr;
        }

        if (mRingBuffers[channel] != nullptr) {
            free(mRingBuffers[channel]);
            mRingBuffers[channel] = nullptr;
        }
    }

    if (mAxInitialized) {
        AXQuit();
        mAxInitialized = false;
    }

    mWriteOffset = 0;
}

uint32_t WiiUAudioPlayer::GetReadOffset() {
    if (mVoices[0] == nullptr) {
        return 0;
    }

    AXVoiceOffsets offsets = {};
    AXGetVoiceOffsets(static_cast<AXVoice*>(mVoices[0]), &offsets);
    return offsets.currentOffset;
}

int WiiUAudioPlayer::Buffered() {
    if (mVoices[0] == nullptr) {
        return 0;
    }

    // The ring holds one sample per frame per channel, so the gap between the write
    // and read heads is already a frame count.
    return static_cast<int>((mWriteOffset + gRingSamples - GetReadOffset()) % gRingSamples);
}

void WiiUAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    if (mVoices[0] == nullptr || mRingBuffers[0] == nullptr) {
        return;
    }

    const int32_t inputChannels = GetNumOutputChannels();
    const size_t frames = len / (sizeof(int16_t) * inputChannels);
    if (frames == 0) {
        return;
    }

    const int16_t* samples = reinterpret_cast<const int16_t*>(buf);
    const uint32_t startOffset = mWriteOffset;

    for (size_t frame = 0; frame < frames; frame++) {
        const uint32_t offset = (startOffset + frame) % gRingSamples;

        for (int32_t channel = 0; channel < gChannelCount; channel++) {
            // A 5.1 setting still arrives interleaved; taking the first two channels
            // keeps the front pair, which is all the stereo AX path can carry.
            mRingBuffers[channel][offset] = samples[frame * inputChannels + channel];
        }
    }

    mWriteOffset = static_cast<uint32_t>((startOffset + frames) % gRingSamples);

    // Flush what was written, in one or two runs depending on whether it wrapped.
    for (int32_t channel = 0; channel < gChannelCount; channel++) {
        if (mWriteOffset > startOffset) {
            DCFlushRange(&mRingBuffers[channel][startOffset], (mWriteOffset - startOffset) * sizeof(int16_t));
        } else {
            DCFlushRange(&mRingBuffers[channel][startOffset], (gRingSamples - startOffset) * sizeof(int16_t));
            if (mWriteOffset > 0) {
                DCFlushRange(&mRingBuffers[channel][0], mWriteOffset * sizeof(int16_t));
            }
        }
    }
}
} // namespace Ship

#endif
