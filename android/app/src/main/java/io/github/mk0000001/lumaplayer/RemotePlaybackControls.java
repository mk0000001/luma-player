package io.github.mk0000001.lumaplayer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.TextView;
import androidx.media3.common.C;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.ui.TrackSelectionDialogBuilder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.function.IntConsumer;

@UnstableApi
final class RemotePlaybackControls {
    interface Actions {
        long positionMs();
        long bufferedPositionMs();
        Player player();
        void seekTo(long positionMs);
        void scrubbingChanged(boolean active);
        void streamOptionsChanged();
        void aspectChanged();
        void showWirelessDisplay();
        void showPlaybackInfo();
    }

    private final Activity activity;
    private final Actions actions;
    private final SharedPreferences preferences;
    private final View panel;
    private final SeekBar seek;
    private final TextView position;
    private final View previous;
    private final View next;
    private final Button back;
    private final Button forward;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable tick = this::refresh;
    private final View chapterPanel;
    private final ListView chapterList;
    private LanSession session;
    private long appliedRevision = -1;
    private long displayedPositionSeconds = -1;
    private String durationLabel;
    private boolean foreground;
    private boolean scrubbing, controlsVisible, portraitChapters;
    private int selectedChapter = -1;
    private int displayedSkipSeconds;
    private String profile;
    private String hdr;
    private int skipSeconds;
    private int subtitleDelayMs;
    private boolean fill;

    RemotePlaybackControls(Activity activity, Actions actions) {
        this.activity = activity;
        this.actions = actions;
        preferences = activity.getSharedPreferences("playback", Context.MODE_PRIVATE);
        panel = activity.findViewById(R.id.playback_navigation);
        chapterPanel = activity.findViewById(R.id.portrait_chapters);
        chapterList = activity.findViewById(R.id.portrait_chapter_list);
        chapterList.setOnItemClickListener((parent, view, index, id) -> {
            if (session != null && index < session.chaptersMs.size()) {
                actions.seekTo(session.chaptersMs.get(index));
            }
        });
        seek = activity.findViewById(R.id.remote_seek);
        position = activity.findViewById(R.id.remote_position);
        previous = activity.findViewById(R.id.previous_chapter);
        next = activity.findViewById(R.id.next_chapter);
        back = activity.findViewById(R.id.skip_back);
        forward = activity.findViewById(R.id.skip_forward);
        previous.setOnClickListener(view -> selectChapter(false));
        next.setOnClickListener(view -> selectChapter(true));
        back.setOnClickListener(view -> actions.seekTo(actions.positionMs() - skipSeconds * 1000L));
        forward.setOnClickListener(view -> actions.seekTo(actions.positionMs() + skipSeconds * 1000L));
        activity.findViewById(R.id.playback_settings).setOnClickListener(view -> showSettings());
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar view, int progress, boolean fromUser) {
                if (fromUser && session != null) {
                    long target = Math.round((double) progress * session.durationMs / seek.getMax());
                    showTime(target);
                    if (!scrubbing) {
                        actions.seekTo(target);
                    }
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar view) {
                scrubbing = true;
                actions.scrubbingChanged(true);
            }

            @Override
            public void onStopTrackingTouch(SeekBar view) {
                scrubbing = false;
                actions.scrubbingChanged(false);
                if (session != null) {
                    actions.seekTo(Math.round((double) seek.getProgress() * session.durationMs / seek.getMax()));
                }
            }
        });
        restorePreferences();
    }

    private void restorePreferences() {
        profile = preferences.getString("profile", "auto");
        hdr = preferences.getString("hdr", "auto");
        skipSeconds = preferences.getInt("skipSeconds", 10);
        subtitleDelayMs = preferences.getInt("subtitleDelayMs", 0);
        fill = preferences.getBoolean("fill", true);
    }

    void setSession(LanSession session) {
        this.session = session;
        appliedRevision = -1;
        restorePreferences();
        scrubbing = false;
        displayedPositionSeconds = -1;
        durationLabel = session != null && session.durationMs > 0 ? timeLabel(session.durationMs) : null;
        if (session != null) {
            readRemoteOptions(session.options);
        } else {
            controlsVisible = false;
        }
        selectedChapter = -1;
        List<String> chapterRows = new ArrayList<>();
        if (session != null) {
            for (int index = 0; index < session.chaptersMs.size(); ++index) {
                chapterRows.add(activity.getString(R.string.chapter_row, index + 1,
                        timeLabel(session.chaptersMs.get(index))));
            }
        }
        chapterList.setAdapter(new ArrayAdapter<>(activity, R.layout.playback_chapter_item, chapterRows));
        updateChapterVisibility();
        refresh();
    }

    void applyRemoteOptions(RemotePlaybackOptions options) {
        String before = selectedProfile();
        int oldDelay = subtitleDelayMs;
        boolean oldFill = fill;
        if (readRemoteOptions(options)) {
            if (!before.equals(selectedProfile()) || oldDelay != subtitleDelayMs) {
                actions.streamOptionsChanged();
            }
            if (oldFill != fill) {
                actions.aspectChanged();
            }
            refresh();
        }
    }

    private boolean readRemoteOptions(RemotePlaybackOptions options) {
        if (options == null || options.revision <= appliedRevision) {
            return false;
        }
        appliedRevision = options.revision;
        restorePreferences();
        if (options.profile != null) {
            profile = options.profile;
        }
        if (options.hdr != null) {
            hdr = options.hdr;
        }
        if (options.skipSeconds != null) {
            skipSeconds = options.skipSeconds;
        }
        if (options.subtitleDelayMs != null) {
            subtitleDelayMs = options.subtitleDelayMs;
        }
        if (options.fill != null) {
            fill = options.fill;
        }
        return true;
    }

    void setVisible(boolean visible) {
        controlsVisible = visible && session != null;
        refresh();
    }

    void setForeground(boolean foreground) {
        this.foreground = foreground;
        refresh();
    }

    void setInsets(int left, int right, int bottom) {
        panel.setPadding(left, 0, right, bottom);
    }

    void setPortraitChapters(boolean show) {
        if (portraitChapters != show) {
            portraitChapters = show;
            updateChapterVisibility();
            refresh();
        }
    }

    private void updateChapterVisibility() {
        chapterPanel.setVisibility(portraitChapters && canSeek() && !session.chaptersMs.isEmpty()
                ? View.VISIBLE : View.GONE);
    }

    private boolean canSeek() {
        return session != null && session.durationMs > 0 && (session.seekable || session.restartable);
    }

    boolean fillScreen() {
        return fill;
    }

    int subtitleDelayMs() {
        return session != null && session.supportsCaptionOffset && !session.subtitles.isEmpty() ? subtitleDelayMs : 0;
    }

    String selectedProfile() {
        if (session == null) {
            return "original";
        }
        boolean hdrSource = "pq".equals(session.videoRange) || "hlg".equals(session.videoRange);
        if (hdrSource && "original".equals(hdr)) {
            return "original";
        }
        if (hdrSource && "sdr".equals(hdr)) {
            if (session.profiles.contains(profile) && !"original".equals(profile)) {
                return profile;
            }
            for (String supported : Arrays.asList("1080p", "720p", "480p")) {
                if (session.profiles.contains(supported)) {
                    return supported;
                }
            }
        }
        return session.profiles.contains(profile) ? profile : "original";
    }

    String lowerAutomaticProfile(String currentProfile) {
        if (session == null || !"auto".equals(profile) || "original".equals(hdr)) {
            return currentProfile;
        }
        List<String> levels = Arrays.asList("original", "1080p", "720p", "480p");
        for (int index = levels.indexOf(currentProfile) + 1; index < levels.size(); index++) {
            String lower = levels.get(index);
            if (!"original".equals(lower) && session.profiles.contains(lower)) {
                return lower;
            }
        }
        return currentProfile;
    }

    private void refresh() {
        handler.removeCallbacks(tick);
        boolean chaptersVisible = chapterPanel.getVisibility() == View.VISIBLE;
        if (!foreground || session == null || (!controlsVisible && !chaptersVisible)) {
            return;
        }
        long current = actions.positionMs();
        if (chaptersVisible) {
            int chapter = Collections.binarySearch(session.chaptersMs, current);
            if (chapter < 0) {
                chapter = -chapter - 2;
            }
            if (chapter != selectedChapter) {
                chapterList.clearChoices();
                if (chapter >= 0) {
                    chapterList.setItemChecked(chapter, true);
                }
                selectedChapter = chapter;
            }
        }
        if (controlsVisible) {
            boolean seekable = canSeek();
            boolean hasChapters = seekable && !session.chaptersMs.isEmpty();
            seek.setEnabled(seekable);
            back.setEnabled(seekable && current > 0);
            forward.setEnabled(seekable && current < session.durationMs - 250);
            previous.setVisibility(hasChapters ? View.VISIBLE : View.GONE);
            next.setVisibility(hasChapters ? View.VISIBLE : View.GONE);
            previous.setEnabled(hasChapters && current > 0);
            next.setEnabled(hasChapters && session.chaptersMs.get(session.chaptersMs.size() - 1) > current + 500);
            if (displayedSkipSeconds != skipSeconds) {
                back.setText(activity.getString(R.string.skip_back_format, skipSeconds));
                forward.setText(activity.getString(R.string.skip_forward_format, skipSeconds));
                displayedSkipSeconds = skipSeconds;
            }
            if (!scrubbing) {
                seek.setProgress(timeBarValue(current));
                seek.setSecondaryProgress(timeBarValue(actions.bufferedPositionMs()));
                showTime(current);
            }
        }
        handler.postDelayed(tick, 250);
    }

    private int timeBarValue(long timeMs) {
        return session.durationMs > 0 ? (int) Math.round((double) timeMs * seek.getMax() / session.durationMs) : 0;
    }

    private void showTime(long time) {
        long seconds = Math.max(0, time / 1000);
        if (displayedPositionSeconds != seconds) {
            displayedPositionSeconds = seconds;
            String current = timeLabel(time);
            position.setText(durationLabel == null ? current : current + " / " + durationLabel);
        }
    }

    private static String timeLabel(long timeMs) {
        long seconds = Math.max(0, timeMs / 1000);
        return seconds >= 3600
                ? String.format(Locale.ROOT, "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60)
                : String.format(Locale.ROOT, "%d:%02d", seconds / 60, seconds % 60);
    }

    private void selectChapter(boolean forwardDirection) {
        if (session == null) {
            return;
        }
        long current = actions.positionMs();
        long previousTime = 0;
        for (long chapter : session.chaptersMs) {
            if (forwardDirection && chapter > current + 500) {
                actions.seekTo(chapter);
                return;
            }
            if (!forwardDirection && chapter < current - 2500) {
                previousTime = chapter;
            }
        }
        if (!forwardDirection) {
            actions.seekTo(previousTime);
        }
    }

    private void showSettings() {
        if (session == null) {
            return;
        }
        Player player = actions.player();
        String speed = player == null ? "1" : speedLabel(player.getPlaybackParameters().speed);
        String[] options = {
                activity.getString(R.string.stream_quality_format, qualityLabel(profile)),
                activity.getString(R.string.hdr_format, hdrLabel(hdr)),
                activity.getString(R.string.skip_interval_format, skipSeconds),
                activity.getString(R.string.screen_ratio_format, fill ? activity.getString(R.string.screen_fill)
                        : activity.getString(R.string.screen_fit)),
                activity.getString(R.string.subtitle_delay_format, subtitleDelayMs / 1000.0),
                activity.getString(R.string.playback_speed_format, speed),
                activity.getString(R.string.playback_audio_tracks),
                activity.getString(R.string.wireless_display),
                activity.getString(R.string.playback_info_action),
                activity.getString(R.string.quality_presets)
        };
        new AlertDialog.Builder(activity).setTitle(R.string.playback_settings)
                .setItems(options, (dialog, index) -> {
                    switch (index) {
                        case 0 -> showQuality();
                        case 1 -> showHdr();
                        case 2 -> showSkip();
                        case 3 -> choose(R.string.playback_settings,
                                new String[]{activity.getString(R.string.screen_fill),
                                        activity.getString(R.string.screen_fit)}, fill ? 0 : 1, choice -> {
                                    fill = choice == 0;
                                    preferences.edit().putBoolean("fill", fill).apply();
                                    actions.aspectChanged();
                                });
                        case 4 -> showSubtitleDelay();
                        case 5 -> showSpeed();
                        case 6 -> showAudioTracks();
                        case 7 -> actions.showWirelessDisplay();
                        case 8 -> actions.showPlaybackInfo();
                        case 9 -> showQualityPresets();
                        default -> { }
                    }
                }).show();
    }

    private static String speedLabel(float speed) {
        return speed == Math.round(speed) ? Integer.toString(Math.round(speed)) : Float.toString(speed);
    }

    private void showSpeed() {
        Player player = actions.player();
        if (player == null) {
            return;
        }
        List<Float> speeds = Arrays.asList(.5f, .75f, 1f, 1.25f, 1.5f, 1.75f, 2f);
        String[] labels = speeds.stream().map(speed -> speedLabel(speed) + "×").toArray(String[]::new);
        choose(R.string.playback_speed, labels, speeds.indexOf(player.getPlaybackParameters().speed), choice -> {
            if (actions.player() == player) {
                player.setPlaybackSpeed(speeds.get(choice));
            }
        });
    }

    private void showAudioTracks() {
        Player player = actions.player();
        if (player != null) {
            new TrackSelectionDialogBuilder(activity, activity.getText(R.string.playback_audio_tracks),
                    player, C.TRACK_TYPE_AUDIO).build().show();
        }
    }

    private String qualityLabel(String value) {
        if ("auto".equals(value)) {
            return activity.getString(R.string.stream_auto);
        }
        return "original".equals(value) ? activity.getString(R.string.stream_original) : value;
    }

    private String hdrLabel(String value) {
        return switch (value) {
            case "original" -> activity.getString(R.string.hdr_original);
            case "sdr" -> activity.getString(R.string.hdr_sdr);
            default -> activity.getString(R.string.stream_auto);
        };
    }

    private void showQuality() {
        if (session.profiles.size() < 2) {
            new AlertDialog.Builder(activity).setMessage(R.string.quality_pc_upgrade)
                    .setPositiveButton(android.R.string.ok, null).show();
            return;
        }
        List<String> values = new ArrayList<>();
        values.add("auto");
        values.addAll(session.profiles);
        String[] labels = values.stream().map(this::qualityLabel).toArray(String[]::new);
        choose(R.string.quality_title, labels, values.indexOf(profile), choice -> {
            profile = values.get(choice);
            preferences.edit().putString("profile", profile).apply();
            actions.streamOptionsChanged();
        });
    }

    private void showQualityPresets() {
        List<String> profiles = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        profiles.add("auto");
        labels.add(activity.getString(R.string.preset_maximum));
        if (session.profiles.contains("1080p")) {
            profiles.add("1080p");
            labels.add(activity.getString(R.string.preset_balanced));
        }
        if (session.profiles.contains("480p")) {
            profiles.add("480p");
            labels.add(activity.getString(R.string.preset_saver));
        }
        String presetHdr = "auto".equals(profile) ? "auto" : "sdr";
        int selected = presetHdr.equals(hdr) ? profiles.indexOf(profile) : -1;
        choose(R.string.quality_presets, labels.toArray(new String[0]), selected, choice -> {
            profile = profiles.get(choice);
            hdr = choice == 0 ? "auto" : "sdr";
            preferences.edit().putString("profile", profile).putString("hdr", hdr).apply();
            actions.streamOptionsChanged();
        });
    }

    private void showHdr() {
        String[] values = {"auto", "original", "sdr"};
        choose(R.string.hdr_title, new String[]{hdrLabel(values[0]), hdrLabel(values[1]), hdrLabel(values[2])},
                Arrays.asList(values).indexOf(hdr), choice -> {
                    hdr = values[choice];
                    preferences.edit().putString("hdr", hdr).apply();
                    actions.streamOptionsChanged();
                });
    }

    private void showSkip() {
        List<Integer> values = Arrays.asList(5, 10, 15, 30);
        String[] labels = values.stream().map(value -> activity.getString(R.string.interval_seconds, value))
                .toArray(String[]::new);
        choose(R.string.skip_title, labels, values.indexOf(skipSeconds), choice -> {
            skipSeconds = values.get(choice);
            preferences.edit().putInt("skipSeconds", skipSeconds).apply();
            refresh();
        });
    }

    private void showSubtitleDelay() {
        if (!session.supportsCaptionOffset || session.subtitles.isEmpty()) {
            new AlertDialog.Builder(activity).setMessage(R.string.subtitle_delay_unavailable)
                    .setPositiveButton(android.R.string.ok, null).show();
            return;
        }
        String[] labels = new String[21];
        for (int index = 0; index < labels.length; index++) {
            labels[index] = activity.getString(R.string.subtitle_delay_value, (index - 10) * 0.5);
        }
        choose(R.string.subtitle_delay_title, labels, subtitleDelayMs / 500 + 10, choice -> {
            subtitleDelayMs = (choice - 10) * 500;
            preferences.edit().putInt("subtitleDelayMs", subtitleDelayMs).apply();
            actions.streamOptionsChanged();
        });
    }

    private void choose(int title, String[] labels, int selected, IntConsumer callback) {
        new AlertDialog.Builder(activity).setTitle(title)
                .setSingleChoiceItems(labels, selected, (dialog, choice) -> {
                    dialog.dismiss();
                    callback.accept(choice);
                }).setNegativeButton(android.R.string.cancel, null).show();
    }
}
