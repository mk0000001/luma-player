package io.github.mk0000001.lumaplayer;

import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

final class PlaybackGestures implements View.OnTouchListener {
    interface Actions {
        long positionMs();
        long seekLimitMs();
        void seekTo(long targetMs);
        boolean fullscreen();
        boolean canEnterFullscreen();
        void setFullscreen(boolean enter);
    }

    private static final long SKIP_MS = 5000;
    private static final long REPEAT_MS = 350;
    private final View video;
    private final SeekFeedbackView feedback;
    private final Actions actions;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable commit = this::finishTap;
    private final Runnable hideFeedback;
    private final int slop;
    private final float swipeDistance;
    private float downX, downY;
    private boolean tracking;
    private int side, steps;
    private long fromMs, targetMs;

    PlaybackGestures(View video, SeekFeedbackView feedback, Actions actions) {
        this.video = video;
        this.feedback = feedback;
        this.actions = actions;
        slop = ViewConfiguration.get(video.getContext()).getScaledTouchSlop();
        swipeDistance = 48 * video.getResources().getDisplayMetrics().density;
        hideFeedback = feedback::dismiss;
        video.setOnTouchListener(this);
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN -> {
                downX = event.getX();
                downY = event.getY();
                tracking = true;
            }
            case MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> tracking = false;
            case MotionEvent.ACTION_UP -> {
                if (tracking) {
                    finishGesture(event);
                }
                tracking = false;
            }
            default -> { }
        }
        return true;
    }

    private void finishGesture(MotionEvent event) {
        float dx = event.getX() - downX;
        float dy = event.getY() - downY;
        if (Math.abs(dy) >= swipeDistance && Math.abs(dy) > 1.35f * Math.abs(dx)) {
            if ((dy > 0 && actions.fullscreen()) || (dy < 0 && actions.canEnterFullscreen())) {
                cancel();
                actions.setFullscreen(dy < 0);
            }
            return;
        }
        if (Math.hypot(dx, dy) > slop || event.getEventTime() - event.getDownTime()
                > ViewConfiguration.getLongPressTimeout()) {
            return;
        }
        float fraction = downX / Math.max(1, video.getWidth());
        int nextSide = fraction < .4f ? -1 : fraction > .6f ? 1 : 0;
        if (side != 0 && side != nextSide) {
            handler.removeCallbacks(commit);
            finishTap();
        }
        long limit = actions.seekLimitMs();
        if (nextSide == 0 || limit <= 0) {
            video.performClick();
            return;
        }
        handler.removeCallbacks(commit);
        if (side == 0) {
            side = nextSide;
            handler.postDelayed(commit, ViewConfiguration.getDoubleTapTimeout());
            return;
        }
        if (steps == 0) {
            fromMs = targetMs = actions.positionMs();
        }
        ++steps;
        targetMs = Math.max(0, Math.min(limit, targetMs + side * SKIP_MS));
        showFeedback((int) ((Math.abs(targetMs - fromMs) + 999) / 1000));
        handler.postDelayed(commit, REPEAT_MS);
    }

    private void showFeedback(int seconds) {
        handler.removeCallbacks(hideFeedback);
        feedback.showStep(side, seconds);
    }

    private void finishTap() {
        if (side == 0) {
            return;
        }
        int count = steps;
        side = steps = 0;
        if (count == 0) {
            video.performClick();
        } else {
            actions.seekTo(targetMs);
            handler.postDelayed(hideFeedback, 450);
        }
    }

    void cancel() {
        handler.removeCallbacks(commit);
        handler.removeCallbacks(hideFeedback);
        side = steps = 0;
        tracking = false;
        feedback.clear();
    }
}
