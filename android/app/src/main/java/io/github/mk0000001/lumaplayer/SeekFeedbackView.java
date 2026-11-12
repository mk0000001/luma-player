package io.github.mk0000001.lumaplayer;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;
import android.view.animation.DecelerateInterpolator;

/** A visual overlay that leaves the video and its touch targets in place. */
public final class SeekFeedbackView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path arrow = new Path();
    private final Typeface font;
    private final float density;
    private final float labelSize;
    private ValueAnimator pulse;
    private float progress;
    private int direction;
    private String label = "";

    public SeekFeedbackView(Context context, AttributeSet attributes) {
        super(context, attributes);
        density = getResources().getDisplayMetrics().density;
        labelSize = android.util.TypedValue.applyDimension(android.util.TypedValue.COMPLEX_UNIT_SP,
                15, getResources().getDisplayMetrics());
        font = Typeface.create(getResources().getFont(R.font.noto_sans_cjk_kr), Typeface.BOLD);
    }

    void showStep(int side, int seconds) {
        direction = side;
        label = getContext().getString(side < 0 ? R.string.skip_back_format : R.string.skip_forward_format, seconds);
        animate().cancel();
        stopPulse();
        setAlpha(1);
        setVisibility(VISIBLE);
        progress = 1;
        if (ValueAnimator.areAnimatorsEnabled()) {
            pulse = ValueAnimator.ofFloat(0, 1);
            pulse.setDuration(460);
            pulse.setInterpolator(new DecelerateInterpolator());
            pulse.addUpdateListener(animation -> {
                progress = (float) animation.getAnimatedValue();
                invalidate();
            });
            pulse.start();
        }
        invalidate();
    }

    void dismiss() {
        if (ValueAnimator.areAnimatorsEnabled()) {
            animate().alpha(0).setDuration(160).withEndAction(this::clear).start();
        } else {
            clear();
        }
    }

    void clear() {
        animate().cancel();
        stopPulse();
        setVisibility(GONE);
    }

    private void stopPulse() {
        if (pulse != null) {
            pulse.cancel();
            pulse = null;
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        clear();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float zone = getWidth() * .38f;
        float centerX = direction < 0 ? zone / 2 : getWidth() - zone / 2;
        float centerY = getHeight() * .46f;
        float radius = Math.min(zone * .48f, getHeight() * .42f);
        if (radius <= 0) {
            return;
        }

        canvas.save();
        canvas.clipRect(direction < 0 ? 0 : getWidth() - zone, 0,
                direction < 0 ? zone : getWidth(), getHeight());
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xB0111B2A);
        canvas.drawCircle(centerX, centerY, radius * .76f, paint);
        if (progress < 1) {
            paint.setColor(0xFF84BCFF);
            paint.setAlpha(Math.round(145 * (1 - progress)));
            paint.setStrokeWidth(2 * density);
            paint.setStyle(Paint.Style.STROKE);
            canvas.drawCircle(centerX, centerY, radius * (.65f + .55f * progress), paint);
        }

        float iconScale = Math.min(density, radius / 32);
        paint.setColor(0xFFFFFFFF);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setStrokeWidth(2.4f * iconScale);
        float arrowY = centerY - 8 * iconScale;
        for (int i = 0; i < 3; ++i) {
            float phase = Math.max(0, Math.min(1, progress * 1.85f - i * .25f));
            paint.setAlpha(Math.round(110 + 145 * phase));
            float x = centerX + direction * ((i - 1) * 11 + 3 * phase) * iconScale;
            arrow.rewind();
            arrow.moveTo(x - direction * 3.5f * iconScale, arrowY - 4.5f * iconScale);
            arrow.lineTo(x + direction * 1.5f * iconScale, arrowY);
            arrow.lineTo(x - direction * 3.5f * iconScale, arrowY + 4.5f * iconScale);
            canvas.drawPath(arrow, paint);
        }
        paint.setAlpha(255);
        paint.setStyle(Paint.Style.FILL);
        paint.setTypeface(font);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(Math.min(labelSize, radius * .45f));
        canvas.drawText(label, centerX, centerY + 14 * iconScale, paint);
        canvas.restore();
    }
}
