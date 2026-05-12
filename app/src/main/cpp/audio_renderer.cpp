#include "audio_renderer.h"

#include <android/log.h>
#include <cstring>

#define LOG_TAG "audio_renderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lhh {

AudioRenderer::~AudioRenderer() {
    release();
}

bool AudioRenderer::init(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;
    SLresult res;

    res = slCreateEngine(&engineObj_, 0, nullptr, 0, nullptr, nullptr);
    if (res != SL_RESULT_SUCCESS) { LOGE("slCreateEngine failed"); return false; }
    (*engineObj_)->Realize(engineObj_, SL_BOOLEAN_FALSE);
    (*engineObj_)->GetInterface(engineObj_, SL_IID_ENGINE, &engine_);

    (*engine_)->CreateOutputMix(engine_, &outputMixObj_, 0, nullptr, nullptr);
    (*outputMixObj_)->Realize(outputMixObj_, SL_BOOLEAN_FALSE);

    SLDataLocator_AndroidSimpleBufferQueue locBufQ = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, kSlots};

    SLuint32 chMask = (channels == 1) ? SL_SPEAKER_FRONT_CENTER
                                      : (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);
    SLDataFormat_PCM fmt = {
            SL_DATAFORMAT_PCM,
            static_cast<SLuint32>(channels),
            static_cast<SLuint32>(sampleRate * 1000),  // millihertz
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            chMask,
            SL_BYTEORDER_LITTLEENDIAN,
    };
    SLDataSource src = {&locBufQ, &fmt};

    SLDataLocator_OutputMix locOutMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObj_};
    SLDataSink sink = {&locOutMix, nullptr};

    const SLInterfaceID ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean reqs[] = {SL_BOOLEAN_TRUE};

    res = (*engine_)->CreateAudioPlayer(engine_, &playerObj_, &src, &sink, 1, ids, reqs);
    if (res != SL_RESULT_SUCCESS) {
        LOGE("CreateAudioPlayer failed: %d (sr=%d ch=%d)", res, sampleRate, channels);
        return false;
    }
    (*playerObj_)->Realize(playerObj_, SL_BOOLEAN_FALSE);
    (*playerObj_)->GetInterface(playerObj_, SL_IID_PLAY, &playItf_);
    (*playerObj_)->GetInterface(playerObj_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bqItf_);

    (*bqItf_)->RegisterCallback(bqItf_, &AudioRenderer::onBufferDone, this);
    (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);

    running_ = true;
    pending_ = 0;
    writeIdx_ = 0;
    playedUs_ = 0;
    LOGI("AudioRenderer init ok sr=%d ch=%d", sampleRate, channels);
    return true;
}

void AudioRenderer::release() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    if (playItf_) (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
    if (bqItf_) (*bqItf_)->Clear(bqItf_);

    if (playerObj_) { (*playerObj_)->Destroy(playerObj_); playerObj_ = nullptr; }
    if (outputMixObj_) { (*outputMixObj_)->Destroy(outputMixObj_); outputMixObj_ = nullptr; }
    if (engineObj_) { (*engineObj_)->Destroy(engineObj_); engineObj_ = nullptr; }
    playItf_ = nullptr;
    bqItf_ = nullptr;
    engine_ = nullptr;
    pending_ = 0;
}

void AudioRenderer::enqueue(const uint8_t* data, size_t bytes) {
    if (bytes == 0 || data == nullptr) return;

    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait(lk, [this] { return !running_ || pending_ < kSlots; });
    if (!running_) return;

    auto& slot = slots_[writeIdx_ % kSlots];
    slot.assign(data, data + bytes);
    ++writeIdx_;
    ++pending_;
    lk.unlock();

    SLresult res = (*bqItf_)->Enqueue(bqItf_, slot.data(), slot.size());
    if (res != SL_RESULT_SUCCESS) {
        LOGE("Enqueue failed: %d", res);
        std::lock_guard<std::mutex> lk2(mutex_);
        --pending_;
        cv_.notify_all();
        return;
    }

    // Update an approximate played-time clock based on enqueued bytes.
    int frameBytes = channels_ * 2;  // S16
    if (frameBytes > 0 && sampleRate_ > 0) {
        int64_t addUs = (int64_t) bytes * 1'000'000LL / (sampleRate_ * frameBytes);
        playedUs_.fetch_add(addUs, std::memory_order_relaxed);
    }
}

void AudioRenderer::setPaused(bool paused) {
    if (playItf_ == nullptr) return;
    SLuint32 state = paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING;
    SLresult res = (*playItf_)->SetPlayState(playItf_, state);
    if (res != SL_RESULT_SUCCESS) {
        LOGE("SetPlayState(%s) failed: %d",
             paused ? "PAUSED" : "PLAYING", res);
    }
}

void AudioRenderer::onBufferDone(SLAndroidSimpleBufferQueueItf, void* ctx) {
    static_cast<AudioRenderer*>(ctx)->handleBufferDone();
}

void AudioRenderer::handleBufferDone() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (pending_ > 0) --pending_;
    cv_.notify_all();
}

}  // namespace lhh
