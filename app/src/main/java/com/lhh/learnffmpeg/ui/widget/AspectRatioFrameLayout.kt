package com.lhh.learnffmpeg.ui.widget

import android.content.Context
import android.util.AttributeSet
import android.widget.FrameLayout

/**
 * Frame container that resizes itself to match a given aspect ratio (width / height),
 * always letterboxing inside the parent (i.e. "fit / contain"). When no aspect
 * ratio has been set it behaves as a vanilla [FrameLayout].
 *
 * Used to wrap the video [android.view.SurfaceView] (and later a `GLSurfaceView`
 * when we move rendering onto OpenGL ES) so the video keeps its native ratio
 * regardless of how big the player area is.
 */
class AspectRatioFrameLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : FrameLayout(context, attrs, defStyleAttr) {

    /** width / height; 0 means "no constraint" (default). */
    private var aspectRatio: Float = 0f

    fun setAspectRatio(width: Int, height: Int) {
        val newRatio = if (width > 0 && height > 0) width.toFloat() / height else 0f
        if (newRatio != aspectRatio) {
            aspectRatio = newRatio
            requestLayout()
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        if (aspectRatio <= 0f) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            return
        }
        val parentW = MeasureSpec.getSize(widthMeasureSpec)
        val parentH = MeasureSpec.getSize(heightMeasureSpec)
        if (parentW <= 0 || parentH <= 0) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            return
        }
        val parentRatio = parentW.toFloat() / parentH
        val (w, h) = if (parentRatio > aspectRatio) {
            // parent is wider than the video → fit by height, leave horizontal bars
            (parentH * aspectRatio).toInt() to parentH
        } else {
            // parent is narrower → fit by width, leave vertical bars
            parentW to (parentW / aspectRatio).toInt()
        }
        super.onMeasure(
            MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(h, MeasureSpec.EXACTLY),
        )
    }
}
