package com.lhh.learnffmpeg.ui.main

data class PlayerUiState(
    val url: String = "",
    val status: PlayStatus = PlayStatus.Idle,
    val videoWidth: Int = 0,
    val videoHeight: Int = 0,
    val durationMs: Long = 0L,
    val errorMessage: String? = null,
)

enum class PlayStatus {
    Idle,
    Preparing,
    Playing,
    Paused,
    Completed,
    Error,
}
