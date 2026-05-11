#ifndef LEARNFFMPEG_FF_PLAYER_H
#define LEARNFFMPEG_FF_PLAYER_H

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace lhh {

class AudioRenderer;

class FfPlayer {
 public:
    FfPlayer(JavaVM* vm, jobject ownerGlobalRef);
    ~FfPlayer();

    void setSurface(JNIEnv* env, jobject surface);
    void start(const std::string& url);
    void stop();

 private:
    void playLoop();
    void renderFrame(AVFrame* rgbaFrame, int width, int height);
    void decodeAndPushAudio(AVCodecContext* audioCtx, AVPacket* packet, AVFrame* frame);

    void notifyPrepared(int width, int height, long durationMs);
    void notifyError(int code, const std::string& message);
    void notifyCompleted();

    JNIEnv* attachCurrentThread(bool* attached);
    void detachCurrentThread(bool attached);

    JavaVM* javaVm_ = nullptr;
    jobject ownerRef_ = nullptr;

    jmethodID midOnPrepared_ = nullptr;
    jmethodID midOnError_ = nullptr;
    jmethodID midOnCompleted_ = nullptr;

    std::mutex windowMutex_;
    ANativeWindow* nativeWindow_ = nullptr;

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string url_;

    // Audio side
    std::unique_ptr<AudioRenderer> audioRenderer_;
    SwrContext* swr_ = nullptr;
    uint8_t* audioOutBuf_ = nullptr;
    int audioOutBufSize_ = 0;
    int audioOutSampleRate_ = 44100;
    int audioOutChannels_ = 2;
    int64_t audioStartPtsUs_ = AV_NOPTS_VALUE;  // first audio pts in us
};

}  // namespace lhh

#endif  // LEARNFFMPEG_FF_PLAYER_H
