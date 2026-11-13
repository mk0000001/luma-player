package io.github.mk0000001.lumaplayer;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.function.Consumer;

/** Inline controls for the paired PC, anchored to the video during playback. */
final class PcController {
    private final Activity activity;
    private final FrameLayout root;
    private final View canvas;
    private final FrameLayout dock;
    private final LinearLayout panel;
    private final ScrollView scroll;
    private final Button playPause;
    private Consumer<String> send;
    private boolean inline = true;
    private boolean overlay;
    private boolean suspended;

    PcController(Activity activity) {
        this.activity = activity;
        root = activity.findViewById(R.id.root);
        canvas = activity.findViewById(R.id.playback_canvas);
        dock = activity.findViewById(R.id.playback_pc_dock);
        panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(10), dp(6), dp(10), dp(10));
        panel.setBackground(background(0xee151a22, 20));
        panel.setElevation(dp(8));
        panel.setAccessibilityPaneTitle(activity.getString(R.string.pc_controls));
        panel.setVisibility(View.GONE);
        root.addView(panel, new FrameLayout.LayoutParams(dp(280), -2, Gravity.TOP | Gravity.END));

        LinearLayout header = row();
        TextView title = new TextView(activity);
        title.setText(R.string.pc_controls);
        title.setTextColor(activity.getColor(R.color.luma_text));
        title.setTextSize(15);
        title.setTypeface(title.getTypeface(), Typeface.BOLD);
        header.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        Button close = button("×", activity.getString(R.string.pc_controls_close), this::hide);
        header.addView(close, new LinearLayout.LayoutParams(dp(48), dp(46)));
        panel.addView(header);

        scroll = new ScrollView(activity);
        scroll.setFillViewport(false);
        LinearLayout controls = new LinearLayout(activity);
        controls.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(controls);
        panel.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        playPause = command("재생", "재생 또는 일시정지", "play-pause");
        addRow(controls, command("이전", "이전 챕터", "previous-chapter"),
                command("−10초", "10초 뒤로", "seek-back"), playPause,
                command("+10초", "10초 앞으로", "seek-forward"),
                command("다음", "다음 챕터", "next-chapter"));
        addRow(controls, command("파일 선택", "PC 파일 선택", "file-picker"),
                command("Blu-ray", "Blu-ray 열기", "disc-open"));
        addRow(controls,
                command("메인 메뉴", "디스크 메인 메뉴", "disc-menu"), command("팝업 메뉴", "디스크 팝업 메뉴", "disc-popup"));
        addRow(controls, null, command("↑", "위", "disc-up"), null);
        addRow(controls, command("←", "왼쪽", "disc-left"), command("확인", "선택한 메뉴 실행", "disc-confirm"),
                command("→", "오른쪽", "disc-right"));
        addRow(controls, null, command("↓", "아래", "disc-down"), null);
        addRow(controls, command("음량 −", "음량 낮추기", "volume-down"), command("음소거", "음소거", "mute"),
                command("음량 +", "음량 높이기", "volume-up"));
        addRow(controls, command("전체 화면", "PC 전체 화면", "fullscreen"),
                command("작은 화면", "PC 작은 화면", "picture-in-picture"), command("이어서 보기", "PC에서 이어서 보기", "resume"));
        addRow(controls, command("속도", "다음 PC 재생 속도", "speed-next"),
                command("오디오", "다음 PC 오디오 트랙", "audio-next"), command("자막 트랙", "다음 PC 자막 트랙", "subtitle-next"));
        addRow(controls, command("AI 자막", "PC AI 자막 전환", "caption-ai"),
                command("원문", "PC 자막 원문 전환", "caption-original"));
        addRow(controls, command("한글 독음", "PC 한글 독음 전환", "caption-reading"),
                command("한국어 번역", "PC 한국어 번역 전환", "caption-translation"));
        root.addOnLayoutChangeListener((view, l, t, r, b, ol, ot, or, ob) -> position());
        canvas.addOnLayoutChangeListener((view, l, t, r, b, ol, ot, or, ob) -> position());
    }

    void openInline(Consumer<String> action) {
        send = action;
        inline = true;
        overlay = false;
        scroll.scrollTo(0, 0);
        position();
    }

    void toggle(Consumer<String> action) {
        send = action;
        if (activity.findViewById(R.id.playback_details).isShown()) inline = !inline;
        else overlay = !overlay;
        if (inline || overlay) scroll.scrollTo(0, 0);
        position();
    }

    void reset() {
        send = null;
        inline = true;
        overlay = false;
        position();
    }

    boolean dismissOverlay() {
        if (!overlay) return false;
        overlay = false;
        position();
        return true;
    }

    void setVideoOnly(boolean videoOnly, boolean pip) {
        if (!videoOnly || pip) overlay = false;
        suspended = pip;
        position();
    }

    void setPlaying(boolean playing) {
        playPause.setText(playing ? R.string.status_pause_action : R.string.status_play_action);
    }

    private void hide() {
        if (!dismissOverlay()) {
            inline = false;
            position();
        }
    }

    private void position() {
        boolean docked = activity.findViewById(R.id.playback_details).isShown();
        boolean visible = send != null && !suspended && (docked ? inline : overlay);
        panel.setVisibility(visible ? View.VISIBLE : View.GONE);
        dock.setVisibility(visible && docked ? View.VISIBLE : View.GONE);
        if (!visible) return;
        if (docked) {
            int height = canvas.getWidth() == root.getWidth() ? 170 : 215;
            if (panel.getParent() != dock) {
                ((FrameLayout) panel.getParent()).removeView(panel);
                dock.addView(panel, new FrameLayout.LayoutParams(-1, dp(height)));
            } else if (panel.getLayoutParams().height != dp(height)) {
                panel.getLayoutParams().height = dp(height);
                panel.requestLayout();
            }
            return;
        }
        if (root.getWidth() == 0) return;
        if (panel.getParent() != root) {
            dock.removeView(panel);
            dock.setVisibility(View.GONE);
            root.addView(panel, new FrameLayout.LayoutParams(dp(280), -2, Gravity.TOP | Gravity.END));
        }
        int top = dp(12);
        int end = dp(12);
        int availableWidth = root.getWidth() - root.getPaddingLeft() - root.getPaddingRight();
        int available = root.getHeight() - root.getPaddingTop() - root.getPaddingBottom() - dp(24);
        if (canvas.isShown() && canvas.getHeight() > dp(100)) {
            int[] origin = new int[2], video = new int[2];
            root.getLocationInWindow(origin);
            canvas.getLocationInWindow(video);
            top += video[1] - origin[1] - root.getPaddingTop();
            int canvasStart = video[0] - origin[0];
            end += root.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL
                    ? canvasStart - root.getPaddingLeft()
                    : root.getWidth() - root.getPaddingRight() - canvasStart - canvas.getWidth();
            availableWidth = canvas.getWidth();
            available = canvas.getHeight() - dp(24);
        } else {
            top += dp(64);
            available -= dp(64);
        }
        FrameLayout.LayoutParams bounds = (FrameLayout.LayoutParams) panel.getLayoutParams();
        int width = Math.min(dp(300), Math.max(dp(160), availableWidth - dp(24)));
        int height = Math.min(dp(470), Math.max(dp(100), available));
        if (bounds.width == width && bounds.height == height && bounds.topMargin == top && bounds.getMarginEnd() == end) return;
        bounds.width = width;
        bounds.height = height;
        bounds.topMargin = top;
        bounds.setMarginEnd(end);
        panel.setLayoutParams(bounds);
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER);
        return row;
    }

    private void addRow(LinearLayout target, View... items) {
        LinearLayout row = row();
        for (View item : items) {
            LinearLayout.LayoutParams cell = new LinearLayout.LayoutParams(0, dp(44), 1);
            cell.setMargins(dp(3), dp(3), dp(3), dp(3));
            row.addView(item == null ? new View(activity) : item, cell);
        }
        target.addView(row, new LinearLayout.LayoutParams(-1, -2));
    }

    private Button command(String label, String description, String action) {
        Button control = button(label, description, () -> {
            if (send != null) send.accept(action);
        });
        if (action.equals("disc-confirm") || action.equals("play-pause"))
            control.setBackground(new RippleDrawable(ColorStateList.valueOf(0x55ffffff), background(0xff176ccc, 22), null));
        return control;
    }

    private Button button(String label, String description, Runnable action) {
        Button button = new Button(activity);
        button.setStateListAnimator(null);
        button.setText(label);
        button.setContentDescription(description);
        button.setAllCaps(false);
        button.setTextSize(12);
        button.setSingleLine(true);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(dp(4), 0, dp(4), 0);
        button.setTextColor(activity.getColor(R.color.luma_text));
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x35ffffff), background(0xff29313d, 22), null));
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private GradientDrawable background(int color, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dp(radius));
        return shape;
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
