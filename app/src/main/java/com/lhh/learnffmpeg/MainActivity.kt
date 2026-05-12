package com.lhh.learnffmpeg

import android.app.Activity
import android.content.Intent
import android.content.res.ColorStateList
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.SurfaceHolder
import android.webkit.MimeTypeMap
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.widget.doAfterTextChanged
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.lhh.learnffmpeg.databinding.ActivityMainBinding
import com.lhh.learnffmpeg.ui.main.PlayStatus
import com.lhh.learnffmpeg.ui.main.PlayerUiState
import com.lhh.learnffmpeg.ui.main.PlayerViewModel
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.Locale
import java.util.UUID

@AndroidEntryPoint
class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val viewModel: PlayerViewModel by viewModels()

    private val pickFileLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            result.data?.data?.let { uri -> handlePickedUri(uri) }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                viewModel.setSurface(holder.surface)
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {
                viewModel.setSurface(holder.surface)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                viewModel.setSurface(null)
            }
        })

        binding.editUrl.doAfterTextChanged { editable ->
            val text = editable?.toString().orEmpty()
            if (text != viewModel.uiState.value.url) {
                viewModel.onUrlChanged(text)
            }
        }

        binding.btnPlay.setOnClickListener { viewModel.play() }
        binding.btnStop.setOnClickListener { viewModel.stop() }
        binding.btnPickFile.setOnClickListener { pickLocalFile() }

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.uiState.collect(::renderState)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        viewModel.onEnterForeground()
    }

    override fun onStop() {
        viewModel.onLeaveForeground()
        super.onStop()
    }

    private fun renderState(state: PlayerUiState) {
        if (binding.editUrl.text?.toString() != state.url) {
            binding.editUrl.setText(state.url)
            binding.editUrl.setSelection(state.url.length)
        }
        binding.tvStatus.text = renderStatusText(state.status, state.errorMessage)
        binding.btnPlay.isEnabled = state.status != PlayStatus.Preparing

        val dotColor = when (state.status) {
            PlayStatus.Idle -> R.color.status_idle
            PlayStatus.Preparing -> R.color.status_preparing
            PlayStatus.Playing -> R.color.status_playing
            PlayStatus.Paused -> R.color.status_preparing
            PlayStatus.Completed -> R.color.status_idle
            PlayStatus.Error -> R.color.status_error
        }
        binding.statusDot.backgroundTintList = ColorStateList.valueOf(
            ContextCompat.getColor(this, dotColor)
        )

        if (state.videoWidth > 0 && state.videoHeight > 0) {
            binding.tvResolution.text = "${state.videoWidth} x ${state.videoHeight}"
            binding.playerContainer.setAspectRatio(state.videoWidth, state.videoHeight)
        } else {
            binding.tvResolution.text = "—"
            binding.playerContainer.setAspectRatio(0, 0)
        }
        binding.tvDuration.text = formatDuration(state.durationMs)
    }

    private fun formatDuration(durationMs: Long): String {
        if (durationMs <= 0L) return "—"
        val totalSec = durationMs / 1000
        val h = totalSec / 3600
        val m = (totalSec % 3600) / 60
        val s = totalSec % 60
        return if (h > 0) {
            String.format(Locale.US, "%d:%02d:%02d", h, m, s)
        } else {
            String.format(Locale.US, "%d:%02d", m, s)
        }
    }

    private fun pickLocalFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "video/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        pickFileLauncher.launch(intent)
    }

    private fun handlePickedUri(uri: Uri) {
        lifecycleScope.launch {
            val path = withContext(Dispatchers.IO) { resolveUriToPlayablePath(uri) }
            if (path.isNullOrEmpty()) {
                Toast.makeText(
                    this@MainActivity,
                    "无法读取所选文件，请换一个视频或检查权限",
                    Toast.LENGTH_LONG,
                ).show()
            } else {
                viewModel.onUrlChanged(path)
            }
        }
    }

    private fun resolveUriToPlayablePath(uri: Uri): String? = when (uri.scheme) {
        "file" -> uri.path?.takeIf { path ->
            path.isNotEmpty() && File(path).let { it.exists() && it.length() > 0L }
        }
        "content" -> copyContentUriToCache(uri)
        else -> null
    }

    private fun copyContentUriToCache(uri: Uri): String? {
        val rawName = queryDisplayName(uri) ?: "video"
        val safeBase = sanitizeFileName(rawName)
        val hasExt = safeBase.contains('.')
        val ext = if (hasExt) "" else extensionFromMime(uri)
        val unique = "${UUID.randomUUID().toString().take(8)}_$safeBase$ext"
        val target = File(cacheDir, unique)

        return runCatching {
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(target).use { output -> input.copyTo(output) }
            } ?: run {
                if (target.exists()) target.delete()
                return null
            }
            if (!target.exists() || target.length() == 0L) {
                target.delete()
                null
            } else {
                target.absolutePath
            }
        }.getOrElse {
            if (target.exists()) target.delete()
            null
        }
    }

    private fun sanitizeFileName(name: String): String {
        val trimmed = name.trim().ifEmpty { "video" }
        return trimmed.replace(Regex("""[^A-Za-z0-9._-]"""), "_")
            .trim('_')
            .ifEmpty { "video" }
            .take(120)
    }

    private fun extensionFromMime(uri: Uri): String {
        val mime = contentResolver.getType(uri) ?: return ".mp4"
        val raw = MimeTypeMap.getSingleton().getExtensionFromMimeType(mime)
        if (!raw.isNullOrEmpty()) return ".$raw"
        return when {
            mime.contains("webm", ignoreCase = true) -> ".webm"
            mime.contains("3gpp", ignoreCase = true) -> ".3gp"
            mime.contains("quicktime", ignoreCase = true) -> ".mov"
            mime.contains("mkv", ignoreCase = true) -> ".mkv"
            else -> ".mp4"
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        var name: String? = null
        val cursor: Cursor? = contentResolver.query(uri, null, null, null, null)
        cursor?.use {
            val idx = it.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && it.moveToFirst()) name = it.getString(idx)
        }
        return name
    }

    private fun renderStatusText(status: PlayStatus, error: String?): String = when (status) {
        PlayStatus.Idle -> getString(R.string.status_idle)
        PlayStatus.Preparing -> getString(R.string.status_preparing)
        PlayStatus.Playing -> getString(R.string.status_playing)
        PlayStatus.Paused -> getString(R.string.status_paused)
        PlayStatus.Completed -> getString(R.string.status_completed)
        PlayStatus.Error -> error ?: getString(R.string.status_error)
    }
}
