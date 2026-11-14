package io.github.mk0000001.lumaplayer;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.function.IntConsumer;

/** Playback settings attached to the same window as the video surface. */
final class PlaybackSettingsPanel {
    private final Activity activity;
    private final View playback;
    private final View overlay;
    private final View sheet;
    private final TextView title;
    private final View back;
    private final ScrollView scroll;
    private final LinearLayout rows;
    private final Runnable showRoot;
    private Runnable returnAction;
    private int previousAccessibility;
    private int heading;
    private boolean subpage;

    PlaybackSettingsPanel(Activity activity, Runnable showRoot) {
        this.activity = activity;
        this.showRoot = showRoot;
        playback = activity.findViewById(R.id.playback_screen);
        overlay = activity.findViewById(R.id.playback_settings_overlay);
        sheet = activity.findViewById(R.id.playback_settings_sheet);
        title = activity.findViewById(R.id.playback_settings_title);
        back = activity.findViewById(R.id.playback_settings_back);
        scroll = activity.findViewById(R.id.playback_settings_scroll);
        rows = activity.findViewById(R.id.playback_settings_rows);
        back.setOnClickListener(view -> { if (returnAction != null) returnAction.run(); });
        activity.findViewById(R.id.playback_settings_scrim).setOnClickListener(view -> close());
        activity.findViewById(R.id.playback_settings_close).setOnClickListener(view -> close());
        overlay.addOnLayoutChangeListener((view, left, top, right, bottom,
                oldLeft, oldTop, oldRight, oldBottom) -> sizeSheet(right - left, bottom - top));
    }

    void show(int heading, boolean subpage) {
        open(heading, subpage ? showRoot : null);
    }

    void showPage(int heading, Runnable backAction) {
        open(heading, backAction);
    }

    private void open(int heading, Runnable backAction) {
        this.heading = heading;
        returnAction = backAction;
        boolean subpage = backAction != null;
        this.subpage = subpage;
        title.setText(heading);
        sheet.setAccessibilityPaneTitle(title.getText());
        back.setVisibility(subpage ? View.VISIBLE : View.GONE);
        rows.removeAllViews();
        scroll.scrollTo(0, 0);
        if (overlay.getVisibility() != View.VISIBLE) {
            previousAccessibility = playback.getImportantForAccessibility();
            playback.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
            overlay.setVisibility(View.VISIBLE);
        }
    }

    void addSection(int label) {
        TextView section = text(activity.getString(label), 12, R.color.luma_accent, true);
        section.setPadding(dp(12), dp(13), dp(12), dp(5));
        section.setAccessibilityHeading(true);
        rows.addView(section);
    }

    void addRow(int label, String value, Runnable action) {
        addRow(label, value, action, true);
    }

    Row addRow(int label, String value, Runnable action, boolean enabled) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(50));
        row.setPadding(dp(12), dp(8), dp(12), dp(8));
        row.setBackground(rowBackground());
        row.setScreenReaderFocusable(true);
        TextView name = text(activity.getString(label), 14, R.color.luma_text, true);
        row.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 3));
        TextView current = null;
        if (value != null) {
            current = text(value, 13, R.color.luma_muted, false);
            current.setGravity(Gravity.END);
            current.setPaddingRelative(dp(8), 0, 0, 0);
            row.addView(current, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 2));
        }
        TextView arrow = text("›", 22, R.color.luma_muted, false);
        arrow.setPaddingRelative(dp(10), 0, 0, 0);
        arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        row.addView(arrow);
        row.setOnClickListener(view -> action.run());
        rows.addView(row);
        Row result = new Row(row, current);
        result.setEnabled(enabled);
        return result;
    }

    record Row(View container, TextView value) {
        void setEnabled(boolean enabled) {
            container.setEnabled(enabled);
            container.setFocusable(enabled);
            container.setAlpha(enabled ? 1f : .6f);
        }

        void setValue(CharSequence text) {
            if (value != null && !text.equals(value.getText())) value.setText(text);
        }
    }

    void addHint(int message) {
        addHint(activity.getText(message));
    }

    TextView addHint(CharSequence message) {
        TextView hint = text(message.toString(), 14, R.color.luma_muted, false);
        hint.setPadding(dp(12), dp(10), dp(12), dp(12));
        hint.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        rows.addView(hint);
        return hint;
    }

    void choose(int heading, String[] labels, int selected, IntConsumer select) {
        show(heading, true);
        for (int index = 0; index < labels.length; index++) {
            int choice = index;
            RadioButton option = new RadioButton(activity);
            option.setText(labels[index]);
            option.setTextSize(14);
            option.setMinHeight(dp(50));
            option.setPaddingRelative(dp(12), dp(6), dp(12), dp(6));
            option.setBackground(rowBackground());
            option.setChecked(index == selected);
            option.setOnClickListener(view -> {
                select.accept(choice);
                if (overlay.getVisibility() == View.VISIBLE) {
                    showRoot.run();
                }
            });
            rows.addView(option);
        }
    }

    boolean handleBack() {
        if (overlay.getVisibility() != View.VISIBLE) {
            return false;
        }
        if (subpage) {
            returnAction.run();
        } else {
            close();
        }
        return true;
    }

    void close() {
        if (overlay.getVisibility() == View.VISIBLE) {
            overlay.setVisibility(View.GONE);
            playback.setImportantForAccessibility(previousAccessibility);
            rows.removeAllViews();
        }
    }

    boolean isShowing(int heading) {
        return this.heading == heading && isOpen();
    }

    boolean isOpen() {
        return overlay.getVisibility() == View.VISIBLE;
    }

    void setInsets(int left, int top, int right, int bottom) {
        overlay.setPadding(left, top, right, bottom);
        sizeSheet(overlay.getWidth(), overlay.getHeight());
    }

    private void sizeSheet(int width, int height) {
        width -= overlay.getPaddingLeft() + overlay.getPaddingRight();
        height -= overlay.getPaddingTop() + overlay.getPaddingBottom();
        if (width <= dp(24) || height <= dp(24)) {
            return;
        }
        boolean wide = width >= dp(600) && width > height;
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) sheet.getLayoutParams();
        int sheetWidth = Math.min(width - dp(24), dp(wide ? 392 : 480));
        int sheetHeight = Math.min(height - dp(24), wide ? dp(620) : (int) (height * .68f));
        int gravity = wide ? Gravity.END | Gravity.CENTER_VERTICAL : Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        if (params.width != sheetWidth || params.height != sheetHeight || params.gravity != gravity) {
            params.width = sheetWidth;
            params.height = sheetHeight;
            params.gravity = gravity;
            sheet.setLayoutParams(params);
        }
    }

    private TextView text(String value, int sp, int color, boolean bold) {
        TextView text = new TextView(activity);
        text.setText(value);
        text.setTextSize(sp);
        text.setTextColor(activity.getColor(color));
        if (bold) {
            text.setTypeface(text.getTypeface(), Typeface.BOLD);
        }
        return text;
    }

    private RippleDrawable rowBackground() {
        GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadius(dp(12));
        return new RippleDrawable(ColorStateList.valueOf(0x3082BEFF), null, mask);
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
