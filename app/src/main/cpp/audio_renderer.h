#ifndef LEARNFFMPEG_AUDIO_RENDERER_H
#define LEARNFFMPEG_AUDIO_RENDERER_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lhh {

/**
 * Tiny OpenSL ES PCM player.
 * Accepts interleaved S16LE / 2 channels / 44100Hz PCM frames pushed via enqueue().
 *
 * Uses a small fixed-size ring of byte buffers; enqueue() blocks the producer
 * thread when the OpenSL queue is saturated, providing natural back-pressure
 * (decoder thread will pause and the demux loop won't run away).
 */
class AudioRenderer {
 public:
    AudioRenderer() = default;
    ~AudioRenderer();

    bool init(int sampleRate, int channels);
    void release();

    void enqueue(const uint8_t* data, size_t bytes);

    // Pause/resume OpenSL playback. Buffer-done callbacks pause naturally
    // when paused, which back-pressures the audio decoder via enqueue().
    void setPaused(bool paused);

    // Approximate playback time in microseconds since playback started.
    int64_t playedUs() const { return playedUs_.load(); }

 private:
    static void onBufferDone(SLAndroidSimpleBufferQueueItf bq, void* ctx);
    void handleBufferDone();

    static constexpr int kSlots = 8;

    SLObjectItf engineObj_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLObjectItf outputMixObj_ = nullptr;
    SLObjectItf playerObj_ = nullptr;
    SLPlayItf playItf_ = nullptr;
    SLAndroidSimpleBufferQueueItf bqItf_ = nullptr;

    std::vector<uint8_t> slots_[kSlots];
    int writeIdx_ = 0;
    int pending_ = 0;          // buffers currently in OpenSL ES queue
    std::mutex mutex_;
    std::condition_variable cv_;

    int sampleRate_ = 44100;
    int channels_ = 2;
    std::atomic<int64_t> playedUs_{0};
    std::atomic<bool> running_{false};
};

}  // namespace lhh

#endif  // LEARNFFMPEG_AUDIO_RENDERER_H
