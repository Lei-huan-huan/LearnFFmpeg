package com.lhh.learnffmpeg.ui.main

import android.view.Surface
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.lhh.learnffmpeg.data.PlayerRepository
import com.lhh.learnffmpeg.data.UserPreferences
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class PlayerViewModel @Inject constructor(
    private val repository: PlayerRepository,
    private val userPreferences: UserPreferences,
) : ViewModel() {

    private val _uiState = MutableStateFlow(PlayerUiState())
    val uiState: StateFlow<PlayerUiState> = _uiState.asStateFlow()

    private val _surfaceReady = MutableStateFlow(false)
    private var playJob: Job? = null

    init {
        viewModelScope.launch {
            val saved = userPreferences.lastUrl.first()
            if (saved.isNotEmpty() && _uiState.value.url.isEmpty()) {
                _uiState.update { it.copy(url = saved) }
            }
        }
    }

    fun onUrlChanged(url: String) {
        _uiState.update { it.copy(url = url) }
    }

    fun setSurface(surface: Surface?) {
        repository.setSurface(surface)
        _surfaceReady.value = surface != null
    }

    fun play() {
        val target = _uiState.value.url.trim()
        if (target.isEmpty()) {
            _uiState.update {
                it.copy(status = PlayStatus.Error, errorMessage = "请输入或选择一个播放地址")
            }
            return
        }
        if (!_surfaceReady.value) {
            _uiState.update {
                it.copy(
                    status = PlayStatus.Error,
                    errorMessage = "视频显示区域尚未就绪，请稍候再点播放",
                )
            }
            return
        }

        viewModelScope.launch { userPreferences.setLastUrl(target) }

        playJob?.cancel()
        _uiState.update {
            it.copy(status = PlayStatus.Preparing, errorMessage = null)
        }

        playJob = viewModelScope.launch {
            repository.play(target).collect { event ->
                when (event) {
                    is PlayerRepository.PlayerEvent.Prepared -> _uiState.update {
                        it.copy(
                            status = PlayStatus.Playing,
                            videoWidth = event.width,
                            videoHeight = event.height,
                            durationMs = event.durationMs,
                            errorMessage = null,
                        )
                    }
                    is PlayerRepository.PlayerEvent.Error -> _uiState.update {
                        it.copy(
                            status = PlayStatus.Error,
                            errorMessage = "播放失败 (${event.code}): ${event.message}",
                        )
                    }
                    PlayerRepository.PlayerEvent.Completed -> _uiState.update {
                        it.copy(status = PlayStatus.Completed)
                    }
                }
            }
        }
    }

    fun stop() {
        playJob?.cancel()
        playJob = null
        repository.stop()
        _uiState.update { it.copy(status = PlayStatus.Idle) }
    }

    /**
     * Called when the host Activity goes into the background.
     * Only pauses if a playback is currently in progress; preserves the
     * underlying native playback so [onEnterForeground] can resume it.
     */
    fun onLeaveForeground() {
        val status = _uiState.value.status
        if (status != PlayStatus.Playing && status != PlayStatus.Preparing) return
        repository.pause()
        _uiState.update { it.copy(status = PlayStatus.Paused) }
    }

    /** Called when the host Activity returns to the foreground. */
    fun onEnterForeground() {
        if (_uiState.value.status != PlayStatus.Paused) return
        repository.resume()
        _uiState.update { it.copy(status = PlayStatus.Playing) }
    }

    override fun onCleared() {
        super.onCleared()
        playJob?.cancel()
        repository.release()
    }
}
