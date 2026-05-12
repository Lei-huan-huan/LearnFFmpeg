package com.lhh.learnffmpeg.data

import android.view.Surface
import com.lhh.learnffmpeg.player.NativePlayer
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Wraps the native player and exposes its lifecycle as cold flows.
 * The repository owns a single [NativePlayer] instance so that the same
 * playback can survive ViewModel reconfiguration.
 */
@Singleton
class PlayerRepository @Inject constructor() {

    sealed interface PlayerEvent {
        data class Prepared(val width: Int, val height: Int, val durationMs: Long) : PlayerEvent
        data class Error(val code: Int, val message: String) : PlayerEvent
        data object Completed : PlayerEvent
    }

    private val player = NativePlayer().also { it.create() }

    fun setSurface(surface: Surface?) = player.setSurface(surface)

    fun stop() = player.stop()

    fun pause() = player.pause()

    fun resume() = player.resume()

    fun release() = player.release()

    /**
     * Starts playback of [url] and emits player lifecycle events.
     * Cancelling collection stops playback.
     */
    fun play(url: String): Flow<PlayerEvent> = callbackFlow {
        val listener = object : NativePlayer.Listener {
            override fun onPrepared(widthPx: Int, heightPx: Int, durationMs: Long) {
                trySend(PlayerEvent.Prepared(widthPx, heightPx, durationMs))
            }

            override fun onError(code: Int, message: String) {
                trySend(PlayerEvent.Error(code, message))
            }

            override fun onCompleted() {
                trySend(PlayerEvent.Completed)
            }
        }
        player.setListener(listener)
        player.start(url)

        awaitClose {
            player.setListener(null)
            player.stop()
        }
    }
}
