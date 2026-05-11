#include "ff_player.h"

#include <android/log.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "audio_renderer.h"

#define LOG_TAG "ff_player"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lhh {

namespace {

std::string avErrToStr(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

bool is_remote_url(const std::string& url) {
    static const char* kPrefixes[] = {
            "http://",  "https://", "tcp://",   "udp://",   "rtmp://", "rtmps://",
            "rtmpt://", "rtmpts://", "rtmpe://", "rtmpte://", "mmsh://", "mmst://",
    };
    for (const char* p : kPrefixes) {
        const size_t n = strlen(p);
        if (url.size() >= n && url.compare(0, n, p) == 0) return true;
    }
    return false;
}

}  // namespace

FfPlayer::FfPlayer(JavaVM* vm, jobject ownerGlobalRef)
    : javaVm_(vm), ownerRef_(ownerGlobalRef) {
    bool attached = false;
    JNIEnv* env = attachCurrentThread(&attached);
    if (env != nullptr) {
        jclass clazz = env->GetObjectClass(ownerRef_);
        midOnPrepared_ = env->GetMethodID(clazz, "onPreparedFromNative", "(IIJ)V");
        midOnError_ = env->GetMethodID(clazz, "onErrorFromNative", "(ILjava/lang/String;)V");
        midOnCompleted_ = env->GetMethodID(clazz, "onCompletedFromNative", "()V");
        env->DeleteLocalRef(clazz);
    }
    detachCurrentThread(attached);
}

FfPlayer::~FfPlayer() {
    stop();
    {
        std::lock_guard<std::mutex> lk(windowMutex_);
        if (nativeWindow_ != nullptr) {
            ANativeWindow_release(nativeWindow_);
            nativeWindow_ = nullptr;
        }
    }
    if (ownerRef_ != nullptr) {
        bool attached = false;
        JNIEnv* env = attachCurrentThread(&attached);
        if (env != nullptr) {
            env->DeleteGlobalRef(ownerRef_);
        }
        detachCurrentThread(attached);
        ownerRef_ = nullptr;
    }
}

void FfPlayer::setSurface(JNIEnv* env, jobject surface) {
    std::lock_guard<std::mutex> lk(windowMutex_);
    if (nativeWindow_ != nullptr) {
        ANativeWindow_release(nativeWindow_);
        nativeWindow_ = nullptr;
    }
    if (surface != nullptr) {
        nativeWindow_ = ANativeWindow_fromSurface(env, surface);
    }
}

void FfPlayer::start(const std::string& url) {
    stop();
    url_ = url;
    running_ = true;
    worker_ = std::thread([this] { playLoop(); });
}

void FfPlayer::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

JNIEnv* FfPlayer::attachCurrentThread(bool* attached) {
    *attached = false;
    JNIEnv* env = nullptr;
    if (javaVm_ == nullptr) return nullptr;
    int status = javaVm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (javaVm_->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
        *attached = true;
    } else if (status != JNI_OK) {
        return nullptr;
    }
    return env;
}

void FfPlayer::detachCurrentThread(bool attached) {
    if (attached && javaVm_ != nullptr) {
        javaVm_->DetachCurrentThread();
    }
}

void FfPlayer::notifyPrepared(int width, int height, long durationMs) {
    if (ownerRef_ == nullptr || midOnPrepared_ == nullptr) return;
    bool attached = false;
    JNIEnv* env = attachCurrentThread(&attached);
    if (env != nullptr) {
        env->CallVoidMethod(ownerRef_, midOnPrepared_,
                            static_cast<jint>(width),
                            static_cast<jint>(height),
                            static_cast<jlong>(durationMs));
    }
    detachCurrentThread(attached);
}

void FfPlayer::notifyError(int code, const std::string& message) {
    if (ownerRef_ == nullptr || midOnError_ == nullptr) return;
    bool attached = false;
    JNIEnv* env = attachCurrentThread(&attached);
    if (env != nullptr) {
        jstring jmsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(ownerRef_, midOnError_, static_cast<jint>(code), jmsg);
        env->DeleteLocalRef(jmsg);
    }
    detachCurrentThread(attached);
}

void FfPlayer::notifyCompleted() {
    if (ownerRef_ == nullptr || midOnCompleted_ == nullptr) return;
    bool attached = false;
    JNIEnv* env = attachCurrentThread(&attached);
    if (env != nullptr) {
        env->CallVoidMethod(ownerRef_, midOnCompleted_);
    }
    detachCurrentThread(attached);
}

void FfPlayer::renderFrame(AVFrame* rgbaFrame, int width, int height) {
    std::lock_guard<std::mutex> lk(windowMutex_);
    if (nativeWindow_ == nullptr) return;

    ANativeWindow_setBuffersGeometry(nativeWindow_, width, height, WINDOW_FORMAT_RGBA_8888);

    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(nativeWindow_, &buffer, nullptr) < 0) {
        LOGE("ANativeWindow_lock failed");
        return;
    }

    auto* dst = static_cast<uint8_t*>(buffer.bits);
    int dstStride = buffer.stride * 4;
    int srcStride = rgbaFrame->linesize[0];
    int rowBytes = std::min(dstStride, srcStride);
    int rows = std::min(buffer.height, height);
    auto* src = rgbaFrame->data[0];
    for (int y = 0; y < rows; ++y) {
        memcpy(dst + y * dstStride, src + y * srcStride, rowBytes);
    }

    ANativeWindow_unlockAndPost(nativeWindow_);
}

void FfPlayer::decodeAndPushAudio(AVCodecContext* audioCtx, AVPacket* packet, AVFrame* frame) {
    if (audioCtx == nullptr || swr_ == nullptr || audioRenderer_ == nullptr) return;

    int ret = avcodec_send_packet(audioCtx, packet);
    if (ret < 0) return;

    while (running_) {
        ret = avcodec_receive_frame(audioCtx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
        if (ret < 0) return;

        // Resample to S16 / stereo / target sample rate.
        int outSamples = (int) av_rescale_rnd(
                swr_get_delay(swr_, audioCtx->sample_rate) + frame->nb_samples,
                audioOutSampleRate_, audioCtx->sample_rate, AV_ROUND_UP);
        int neededBytes = outSamples * audioOutChannels_ * 2;  // S16
        if (neededBytes > audioOutBufSize_) {
            av_free(audioOutBuf_);
            audioOutBuf_ = (uint8_t*) av_malloc(neededBytes);
            audioOutBufSize_ = neededBytes;
        }

        uint8_t* outPtr[1] = {audioOutBuf_};
        int converted = swr_convert(swr_, outPtr, outSamples,
                                    (const uint8_t**) frame->data, frame->nb_samples);
        if (converted > 0) {
            int bytes = converted * audioOutChannels_ * 2;
            audioRenderer_->enqueue(audioOutBuf_, (size_t) bytes);
        }
        av_frame_unref(frame);
    }
}

void FfPlayer::playLoop() {
    avformat_network_init();

    AVFormatContext* fmtCtx = nullptr;
    AVDictionary* opts = nullptr;
    if (is_remote_url(url_)) {
        av_dict_set(&opts, "stimeout", "5000000", 0);
        av_dict_set(&opts, "rw_timeout", "10000000", 0);
        av_dict_set(&opts, "user_agent", "LearnFFmpeg/1.0", 0);
    }

    std::string openUrl = url_;
    if (!is_remote_url(url_) && !url_.empty() && url_[0] == '/') {
        openUrl = std::string("file:") + url_;
    }
    LOGI("open_input openUrl=%s remote=%d", openUrl.c_str(), is_remote_url(url_) ? 1 : 0);

    int ret = avformat_open_input(&fmtCtx, openUrl.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    LOGI("open_input ret=%d", ret);
    if (ret < 0) {
        notifyError(ret, std::string("open_input: ") + avErrToStr(ret));
        avformat_network_deinit();
        return;
    }

    if ((ret = avformat_find_stream_info(fmtCtx, nullptr)) < 0) {
        notifyError(ret, std::string("find_stream_info: ") + avErrToStr(ret));
        avformat_close_input(&fmtCtx);
        avformat_network_deinit();
        return;
    }
    LOGI("find_stream_info ok, nb_streams=%u", fmtCtx->nb_streams);

    int videoIdx = -1, audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        AVMediaType t = fmtCtx->streams[i]->codecpar->codec_type;
        if (videoIdx < 0 && t == AVMEDIA_TYPE_VIDEO) videoIdx = (int) i;
        else if (audioIdx < 0 && t == AVMEDIA_TYPE_AUDIO) audioIdx = (int) i;
    }
    if (videoIdx < 0) {
        notifyError(-1, "no video stream");
        avformat_close_input(&fmtCtx);
        avformat_network_deinit();
        return;
    }

    // ----- Video init -----
    AVStream* videoStream = fmtCtx->streams[videoIdx];
    const AVCodec* vcodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (vcodec == nullptr) {
        notifyError(-1, "video decoder not found");
        avformat_close_input(&fmtCtx);
        avformat_network_deinit();
        return;
    }
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx, videoStream->codecpar);
    if ((ret = avcodec_open2(vctx, vcodec, nullptr)) < 0) {
        notifyError(ret, std::string("avcodec_open2 (video): ") + avErrToStr(ret));
        avcodec_free_context(&vctx);
        avformat_close_input(&fmtCtx);
        avformat_network_deinit();
        return;
    }

    int width = vctx->width;
    int height = vctx->height;
    long durationMs = (fmtCtx->duration > 0) ? (fmtCtx->duration / 1000) : 0;
    LOGI("video opened: codec=%s %dx%d duration=%ldms",
         vcodec->name, width, height, durationMs);

    SwsContext* sws = sws_getContext(width, height, vctx->pix_fmt,
                                     width, height, AV_PIX_FMT_RGBA,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        notifyError(-1, "sws_getContext failed");
        avcodec_free_context(&vctx);
        avformat_close_input(&fmtCtx);
        avformat_network_deinit();
        return;
    }

    AVFrame* vframe = av_frame_alloc();
    AVFrame* rgba = av_frame_alloc();
    int rgbaSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, 1);
    auto* rgbaBuf = static_cast<uint8_t*>(av_malloc(rgbaSize));
    av_image_fill_arrays(rgba->data, rgba->linesize, rgbaBuf,
                         AV_PIX_FMT_RGBA, width, height, 1);

    // ----- Audio init (optional) -----
    AVCodecContext* actx = nullptr;
    AVFrame* aframe = nullptr;
    AVStream* audioStream = nullptr;
    if (audioIdx >= 0) {
        audioStream = fmtCtx->streams[audioIdx];
        const AVCodec* acodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (acodec == nullptr) {
            LOGE("audio decoder not found, skipping audio");
        } else {
            actx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(actx, audioStream->codecpar);
            if (avcodec_open2(actx, acodec, nullptr) < 0) {
                LOGE("avcodec_open2 (audio) failed, skipping audio");
                avcodec_free_context(&actx);
                actx = nullptr;
            }
        }

        if (actx != nullptr) {
            audioOutSampleRate_ = 44100;
            audioOutChannels_ = 2;
            AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            ret = swr_alloc_set_opts2(&swr_,
                                      &outLayout, AV_SAMPLE_FMT_S16, audioOutSampleRate_,
                                      &actx->ch_layout, actx->sample_fmt, actx->sample_rate,
                                      0, nullptr);
            if (ret < 0 || swr_init(swr_) < 0) {
                LOGE("swr init failed, skipping audio");
                if (swr_) { swr_free(&swr_); }
                avcodec_free_context(&actx);
                actx = nullptr;
            }
        }

        if (actx != nullptr) {
            audioRenderer_.reset(new AudioRenderer());
            if (!audioRenderer_->init(audioOutSampleRate_, audioOutChannels_)) {
                LOGE("audio renderer init failed, skipping audio");
                audioRenderer_.reset();
                if (swr_) swr_free(&swr_);
                avcodec_free_context(&actx);
                actx = nullptr;
            } else {
                aframe = av_frame_alloc();
                LOGI("audio opened: codec=%s sr=%d ch=%d",
                     acodec->name, actx->sample_rate, actx->ch_layout.nb_channels);
            }
        }
    }

    AVPacket* packet = av_packet_alloc();
    notifyPrepared(width, height, durationMs);

    AVRational vtb = videoStream->time_base;
    int64_t startWallUs = av_gettime();
    int64_t startPtsUs = AV_NOPTS_VALUE;

    bool reached_eof = false;
    while (running_) {
        ret = av_read_frame(fmtCtx, packet);
        if (ret == AVERROR_EOF) {
            reached_eof = true;
            break;
        }
        if (ret < 0) {
            notifyError(ret, std::string("read_frame: ") + avErrToStr(ret));
            reached_eof = false;
            break;
        }

        if (packet->stream_index == videoIdx) {
            ret = avcodec_send_packet(vctx, packet);
            av_packet_unref(packet);
            if (ret < 0) continue;

            while (running_) {
                ret = avcodec_receive_frame(vctx, vframe);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    notifyError(ret, std::string("video receive_frame: ") + avErrToStr(ret));
                    running_ = false;
                    break;
                }

                sws_scale(sws, vframe->data, vframe->linesize, 0, height,
                          rgba->data, rgba->linesize);

                int64_t pts = vframe->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE) {
                    int64_t ptsUs = av_rescale_q(pts, vtb, AVRational{1, 1000000});
                    if (startPtsUs == AV_NOPTS_VALUE) {
                        startPtsUs = ptsUs;
                        startWallUs = av_gettime();
                    } else {
                        int64_t target = startWallUs + (ptsUs - startPtsUs);
                        int64_t wait = target - av_gettime();
                        if (wait > 0 && wait < 1'000'000) {
                            std::this_thread::sleep_for(std::chrono::microseconds(wait));
                        }
                    }
                }

                renderFrame(rgba, width, height);
                av_frame_unref(vframe);
            }
        } else if (audioIdx >= 0 && actx != nullptr && packet->stream_index == audioIdx) {
            decodeAndPushAudio(actx, packet, aframe);
            av_packet_unref(packet);
        } else {
            av_packet_unref(packet);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&vframe);
    av_frame_free(&rgba);
    av_free(rgbaBuf);
    sws_freeContext(sws);
    avcodec_free_context(&vctx);

    if (aframe) av_frame_free(&aframe);
    if (actx) avcodec_free_context(&actx);
    if (swr_) { swr_free(&swr_); }
    if (audioOutBuf_) { av_free(audioOutBuf_); audioOutBuf_ = nullptr; audioOutBufSize_ = 0; }
    if (audioRenderer_) { audioRenderer_->release(); audioRenderer_.reset(); }

    avformat_close_input(&fmtCtx);
    avformat_network_deinit();

    if (running_ && reached_eof) notifyCompleted();
    running_ = false;
}

}  // namespace lhh
