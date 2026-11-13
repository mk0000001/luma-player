package io.github.mk0000001.lumaplayer;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.widget.SeekBar;

/** Draws the forward buffered interval separately from the played progress. */
public final class BufferedSeekBar extends SeekBar {
    private final Paint bufferPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int bufferedFrom;
    private int bufferedTo;

    public BufferedSeekBar(Context context, AttributeSet attributes) {
        super(context, attributes);
        bufferPaint.setColor(0xc6b9c7db);
        bufferPaint.setStrokeWidth(4 * getResources().getDisplayMetrics().density);
        bufferPaint.setStrokeCap(Paint.Cap.ROUND);
    }

    void setBufferedRange(int start, int end) {
        if (bufferedFrom == start && bufferedTo == end) return;
        bufferedFrom = start;
        bufferedTo = end;
        invalidate();
    }

    @Override
    protected synchronized void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int start = Math.max(getProgress(), bufferedFrom);
        if (bufferedTo <= start || getMax() <= 0 || getProgressDrawable() == null) return;
        boolean rtl = getLayoutDirection() == LAYOUT_DIRECTION_RTL;
        float width = getWidth() - getPaddingLeft() - getPaddingRight();
        float left = getPaddingLeft() + width * (rtl ? 1 - (float) start / getMax() : (float) start / getMax());
        float right = getPaddingLeft() + width * (rtl ? 1 - (float) bufferedTo / getMax() : (float) bufferedTo / getMax());
        if (start == getProgress() && getThumb() != null) {
            left += (rtl ? -1 : 1) * (getThumb().getBounds().width() / 2f + 1);
        }
        if ((right - left) * (rtl ? -1 : 1) >= 0) {
            float center = getPaddingTop() + getProgressDrawable().getBounds().exactCenterY();
            canvas.drawLine(left, center, right, center, bufferPaint);
        }
    }
}
