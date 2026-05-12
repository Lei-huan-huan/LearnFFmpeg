package com.lhh.learnffmpeg.player

import android.view.Surface

/**
 * Thin Kotlin wrapper over the native FFmpeg-based player.
 *
 * Native side dynamically registers JNI methods in JNI_OnLoad, see native-lib.cpp.
 */
class NativePlayer {

    /** Opaque native handle. 0 means released. */
    @Volatile
    private var nativeHandle: Long = 0L

    /** Lifecycle callback bridged from native player thread. */
    interface Listener {
        fun onPrepared(widthPx: Int, heightPx: Int, durationMs: Long) {}
        fun onError(code: Int, message: String) {}
        fun onCompleted() {}
    }

    private var listener: Listener? = null

    fun setListener(l: Listener?) {
        listener = l
    }

    fun create() {
        if (nativeHandle == 0L) {
            nativeHandle = nativeCreate()
        }
    }

    fun setSurface(surface: Surface?) {
        if (nativeHandle != 0L) nativeSetSurface(nativeHandle, surface)
    }

    fun start(url: String) {
        if (nativeHandle == 0L) create()
        nativeStart(nativeHandle, url)
    }

    fun stop() {
        if (nativeHandle != 0L) nativeStop(nativeHandle)
    }

    fun pause() {
        if (nativeHandle != 0L) nativePause(nativeHandle)
    }

    fun resume() {
        if (nativeHandle != 0L) nativeResume(nativeHandle)
    }

    fun release() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0L
        }
        listener = null
    }

    // ---- Called from native (worker thread) ----
    @Suppress("unused")
    private fun onPreparedFromNative(width: Int, height: Int, durationMs: Long) {
        listener?.onPrepared(width, height, durationMs)
    }

    @Suppress("unused")
    private fun onErrorFromNative(code: Int, message: String) {
        listener?.onError(code, message)
    }

    @Suppress("unused")
    private fun onCompletedFromNative() {
        listener?.onCompleted()
    }

    // ---- Native methods (dynamically registered) ----
    private external fun nativeCreate(): Long
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeStart(handle: Long, url: String)
    private external fun nativeStop(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeResume(handle: Long)
    private external fun nativeRelease(handle: Long)

    companion object {
        init {
            System.loadLibrary("learnffmpeg")
        }
    }
}
