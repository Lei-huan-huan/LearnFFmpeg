#include <jni.h>
#include <android/log.h>

#include <string>

#include "ff_player.h"

#define LOG_TAG "native-lib"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr const char* kClassName = "com/lhh/learnffmpeg/player/NativePlayer";

JavaVM* gVm = nullptr;

inline lhh::FfPlayer* asPlayer(jlong handle) {
    return reinterpret_cast<lhh::FfPlayer*>(handle);
}

jlong nativeCreate(JNIEnv* env, jobject thiz) {
    jobject globalRef = env->NewGlobalRef(thiz);
    auto* player = new lhh::FfPlayer(gVm, globalRef);
    return reinterpret_cast<jlong>(player);
}

void nativeSetSurface(JNIEnv* env, jobject /* thiz */, jlong handle, jobject surface) {
    auto* player = asPlayer(handle);
    if (player != nullptr) player->setSurface(env, surface);
}

void nativeStart(JNIEnv* env, jobject /* thiz */, jlong handle, jstring jurl) {
    auto* player = asPlayer(handle);
    if (player == nullptr || jurl == nullptr) return;
    const char* cstr = env->GetStringUTFChars(jurl, nullptr);
    std::string url(cstr ? cstr : "");
    if (cstr) env->ReleaseStringUTFChars(jurl, cstr);
    player->start(url);
}

void nativeStop(JNIEnv* /* env */, jobject /* thiz */, jlong handle) {
    auto* player = asPlayer(handle);
    if (player != nullptr) player->stop();
}

void nativeRelease(JNIEnv* /* env */, jobject /* thiz */, jlong handle) {
    auto* player = asPlayer(handle);
    delete player;
}

const JNINativeMethod kMethods[] = {
        {"nativeCreate",     "()J",                                 reinterpret_cast<void*>(nativeCreate)},
        {"nativeSetSurface", "(JLandroid/view/Surface;)V",          reinterpret_cast<void*>(nativeSetSurface)},
        {"nativeStart",      "(JLjava/lang/String;)V",              reinterpret_cast<void*>(nativeStart)},
        {"nativeStop",       "(J)V",                                reinterpret_cast<void*>(nativeStop)},
        {"nativeRelease",    "(J)V",                                reinterpret_cast<void*>(nativeRelease)},
};

bool registerNatives(JNIEnv* env) {
    jclass clazz = env->FindClass(kClassName);
    if (clazz == nullptr) {
        LOGE("FindClass failed: %s", kClassName);
        return false;
    }
    int count = sizeof(kMethods) / sizeof(kMethods[0]);
    if (env->RegisterNatives(clazz, kMethods, count) < 0) {
        LOGE("RegisterNatives failed for %s", kClassName);
        env->DeleteLocalRef(clazz);
        return false;
    }
    env->DeleteLocalRef(clazz);
    return true;
}

}  // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    gVm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("GetEnv failed");
        return JNI_ERR;
    }
    if (!registerNatives(env)) return JNI_ERR;
    return JNI_VERSION_1_6;
}
