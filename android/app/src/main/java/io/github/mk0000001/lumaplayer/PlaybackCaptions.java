package io.github.mk0000001.lumaplayer;

import android.app.Activity;
import android.content.ContentResolver;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;
import androidx.media3.common.C;
import androidx.media3.common.Player;
import androidx.media3.common.TrackSelectionParameters;
import androidx.media3.common.text.Cue;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.text.Subtitle;
import androidx.media3.ui.CaptionStyleCompat;
import androidx.media3.ui.PlayerView;
import androidx.media3.ui.SubtitleView;
import androidx.media3.ui.TrackSelectionDialogBuilder;
import java.io.IOException;
import java.io.InputStream;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.stream.Collectors;

@UnstableApi
final class PlaybackCaptions {
    interface Actions {
        Player player();
        long positionMs();
        int delayMs();
        void chooseFile();
        void openPlaybackSettings();
    }

    private enum Source { PC, FILE, TRACK }

    private final Activity activity;
    private final Actions actions;
    private final PlaybackSettingsPanel settings;
    private final SubtitleView overlay;
    private final ImageButton button;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService network = Executors.newSingleThreadExecutor();
    private final ExecutorService files = Executors.newSingleThreadExecutor();
    private final Runnable poll = this::pollHost;
    private final Runnable render = this::renderCues;
    private Future<?> polling, importing;
    private LanClient client;
    private LanSession session;
    private RemoteCaptionState state;
    private Subtitle hostCues, fileCues;
    private List<Cue> displayed = Collections.emptyList();
    private int generation, importGeneration, fileMessage, commandMessage;
    private boolean commandPending, pollAgain, pollFailed;
    private boolean controlsVisible;
    private boolean hidden, textTrackDisabled;
    private Source source = Source.TRACK;
    private TextView status, guidance, localStatus;
    private PlaybackSettingsPanel.Row ai, pcDisplay, original, reading, translation, retry, localDisplay, remove, tracks;

    PlaybackCaptions(Activity activity, PlayerView video, PlaybackSettingsPanel settings, Actions actions) {
        this.activity = activity;
        this.actions = actions;
        this.settings = settings;
        ViewGroup frame = video.findViewById(androidx.media3.ui.R.id.exo_content_frame);
        overlay = new SubtitleView(activity);
        overlay.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        overlay.setVisibility(View.GONE);
        overlay.setApplyEmbeddedFontSizes(false);
        overlay.setStyle(new CaptionStyleCompat(Color.WHITE, 0xb3000000, Color.TRANSPARENT,
                CaptionStyleCompat.EDGE_TYPE_NONE, Color.BLACK, activity.getResources().getFont(R.font.noto_sans_cjk_kr)));
        frame.addView(overlay, frame.indexOfChild(video.getSubtitleView()) + 1,
                new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        overlay.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (bottom - top != oldBottom - oldTop) updateCaptionPadding();
        });
        button = activity.findViewById(R.id.playback_caption_options);
        button.setOnClickListener(view -> show(false));
    }

    void attach(LanClient client, LanSession session) {
        clear();
        this.client = client;
        this.session = session;
        selectSource(session.liveCaptions ? Source.PC : Source.TRACK);
        if (session.liveCaptions) main.post(poll);
    }

    void clear() {
        ++generation;
        ++importGeneration;
        main.removeCallbacksAndMessages(null);
        if (polling != null) polling.cancel(true);
        if (importing != null) importing.cancel(true);
        polling = importing = null;
        client = null;
        session = null;
        state = null;
        hostCues = fileCues = null;
        pollAgain = pollFailed = commandPending = false;
        hidden = false;
        textTrackDisabled = false;
        fileMessage = commandMessage = 0;
        source = Source.TRACK;
        showCues(Collections.emptyList());
        updatePanel();
    }

    void close() {
        clear();
        network.shutdownNow();
        files.shutdownNow();
    }

    void onPlayerChanged() {
        renderCues();
        updatePanel();
    }

    void controlsVisible(boolean visible) {
        controlsVisible = visible;
        updateCaptionPadding();
    }

    void toggleVisibility() {
        hidden = !hidden;
        applyTrackVisibility();
    }

    private void updateCaptionPadding() {
        int controlsHeight = Math.round(92 * activity.getResources().getDisplayMetrics().density);
        float padding = controlsVisible && overlay.getHeight() > 0 ? Math.min(.33f, controlsHeight / (float) overlay.getHeight())
                : SubtitleView.DEFAULT_BOTTOM_PADDING_FRACTION;
        overlay.setBottomPaddingFraction(padding);
    }

    void show(boolean fromSettings) {
        if (session == null) return;
        settings.showPage(R.string.playback_captions, fromSettings ? actions::openPlaybackSettings : null);
        settings.addSection(R.string.captions_pc_section);
        status = settings.addHint("");
        guidance = settings.addHint("");
        ai = settings.addRow(R.string.captions_pc_ai, "", this::toggleAi, false);
        pcDisplay = settings.addRow(R.string.captions_display, "", () ->
                selectSource(source == Source.PC && !hidden ? Source.TRACK : Source.PC), false);
        original = settings.addRow(R.string.captions_original, "", () -> command("caption-original"), false);
        reading = settings.addRow(R.string.captions_reading, "", () -> command("caption-reading"), false);
        translation = settings.addRow(R.string.captions_translation, "", () -> command("caption-translation"), false);
        retry = settings.addRow(R.string.captions_retry, null, () -> command("caption-retry"), false);
        settings.addSection(R.string.captions_local_section);
        localStatus = settings.addHint("");
        settings.addRow(R.string.captions_import, null, actions::chooseFile);
        localDisplay = settings.addRow(R.string.captions_local_display, "", () ->
                selectSource(source == Source.FILE && !hidden ? defaultSource() : Source.FILE), false);
        remove = settings.addRow(R.string.captions_remove, null, this::removeFile, false);
        tracks = settings.addRow(R.string.captions_tracks, "", this::chooseTracks, false);
        updatePanel();
    }

    boolean showForPcCommand(String action) {
        if (session == null || !session.liveCaptions) return false;
        switch (action) {
            case "caption-ai" -> {
                show(false);
                toggleAi();
            }
            case "caption-original", "caption-reading", "caption-translation" -> {
                show(false);
                command(action);
            }
            default -> { return false; }
        }
        return true;
    }

    void importFile(Uri uri) {
        if (session == null || uri == null) return;
        if (importing != null) importing.cancel(true);
        int request = ++importGeneration;
        if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) {
            finishImport(null);
            return;
        }
        fileMessage = R.string.captions_loading_file;
        updatePanel();
        importing = files.submit(() -> {
            Subtitle result = null;
            try (InputStream data = activity.getContentResolver().openInputStream(uri)) {
                if (data != null) result = CaptionFile.read(data);
            } catch (IOException | SecurityException | IllegalArgumentException ignored) {
                // The selected document may be gone or unavailable from its provider.
            }
            Subtitle parsed = result;
            main.post(() -> {
                if (request == importGeneration) finishImport(parsed);
            });
        });
    }

    private void finishImport(Subtitle cues) {
        importing = null;
        if (cues == null) {
            fileMessage = R.string.captions_bad_file;
            if (!settings.isShowing(R.string.playback_captions)) {
                Toast.makeText(activity, fileMessage, Toast.LENGTH_LONG).show();
            }
            if (fileCues == null && state != null && "off".equals(state.state()) && state.canAI()) {
                command("caption-ai-on");
            }
        } else {
            fileCues = cues;
            fileMessage = R.string.captions_local_loaded;
            selectSource(Source.FILE);
        }
        updatePanel();
    }

    private void removeFile() {
        ++importGeneration;
        if (importing != null) importing.cancel(true);
        importing = null;
        fileCues = null;
        fileMessage = 0;
        if (source == Source.FILE) selectSource(defaultSource());
        else updatePanel();
    }

    private Source defaultSource() {
        return session.liveCaptions ? Source.PC : Source.TRACK;
    }

    private void chooseTracks() {
        Player player = actions.player();
        if (player == null || !player.getCurrentTracks().isTypeSupported(C.TRACK_TYPE_TEXT)) return;
        var parameters = player.getTrackSelectionParameters();
        var textGroups = player.getCurrentTracks().getGroups().stream()
                .filter(group -> group.getType() == C.TRACK_TYPE_TEXT).collect(Collectors.toList());
        new TrackSelectionDialogBuilder(activity, activity.getText(R.string.captions_tracks), textGroups,
                (disabled, overrides) -> {
                    if (actions.player() != player) return;
                    TrackSelectionParameters.Builder builder = player.getTrackSelectionParameters().buildUpon()
                            .clearOverridesOfType(C.TRACK_TYPE_TEXT).setTrackTypeDisabled(C.TRACK_TYPE_TEXT, disabled);
                    overrides.values().forEach(builder::addOverride);
                    source = Source.TRACK;
                    hidden = false;
                    textTrackDisabled = disabled;
                    player.setTrackSelectionParameters(builder.build());
                    onPlayerChanged();
                }).setOverrides(parameters.overrides)
                .setIsDisabled(source != Source.TRACK || parameters.disabledTrackTypes.contains(C.TRACK_TYPE_TEXT))
                .setShowDisableOption(true).build().show();
    }

    private void selectSource(Source selected) {
        source = selected;
        hidden = false;
        applyTrackVisibility();
    }

    private void applyTrackVisibility() {
        Player player = actions.player();
        if (player != null) {
            player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon()
                    .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, hidden || source != Source.TRACK || textTrackDisabled).build());
        }
        onPlayerChanged();
    }

    private void toggleAi() {
        if (state != null && state.canAI()) command(state.enabled() ? "caption-ai-off" : "caption-ai-on");
    }

    private void command(String action) {
        if (commandPending || pollFailed || state == null || !state.canControl()) return;
        LanClient active = client;
        String revision = session.mediaRevision;
        int request = generation;
        commandPending = true;
        commandMessage = 0;
        updatePanel();
        network.execute(() -> {
            int error = 0;
            try {
                active.controlCaptions(action, revision);
            } catch (LanClient.HttpException status) {
                error = status.status == 403 ? R.string.captions_permission : R.string.captions_request_failed;
            } catch (IOException ignored) {
                error = R.string.captions_request_failed;
            }
            int result = error;
            main.post(() -> {
                if (request != generation) return;
                commandPending = false;
                commandMessage = result;
                if (result == 0 && "caption-ai-on".equals(action)) selectSource(Source.PC);
                updatePanel();
                refreshHostSoon();
            });
        });
    }

    private void refreshHostSoon() {
        if (polling != null) {
            pollAgain = true;
        } else {
            main.removeCallbacks(poll);
            main.post(poll);
        }
    }

    private void pollHost() {
        if (session == null || !session.liveCaptions || polling != null) return;
        int request = generation;
        LanClient active = client;
        String media = session.mediaRevision;
        long position = actions.positionMs();
        long revision = state == null ? -1 : state.revision();
        polling = network.submit(() -> {
            try {
                RemoteCaptionState response = active.fetchCaptions(media, position, revision);
                Subtitle updated = response.revision() == revision ? null : CaptionFile.parseRemote(response.vtt());
                main.post(() -> receiveHostState(request, response, updated));
            } catch (IOException error) {
                main.post(() -> receiveHostState(request, null, null));
            }
        });
    }

    private void receiveHostState(int request, RemoteCaptionState response, Subtitle updated) {
        if (request != generation) return;
        polling = null;
        pollFailed = response == null;
        if (response != null) {
            if (state == null || response.revision() != state.revision()) hostCues = updated;
            state = response;
            renderCues();
        }
        updatePanel();
        boolean playing = actions.player() != null && actions.player().getPlayWhenReady();
        main.postDelayed(poll, pollAgain ? 100 : pollFailed ? 2_000 : playing ? 850 : 1_800);
        pollAgain = false;
    }

    private void renderCues() {
        main.removeCallbacks(render);
        Player player = actions.player();
        Subtitle timeline = source == Source.FILE ? fileCues : source == Source.PC ? hostCues : null;
        if (hidden || timeline == null || player == null || player.getPlaybackState() == Player.STATE_IDLE
                || player.getPlaybackState() == Player.STATE_ENDED) {
            showCues(Collections.emptyList());
            return;
        }
        long position = (actions.positionMs() - actions.delayMs()) * 1000L;
        showCues(timeline.getCues(position));
        int next = timeline.getNextEventTimeIndex(position);
        if (player.isPlaying() && next >= 0) {
            double speed = player.getPlaybackParameters().speed;
            long untilCue = (long) Math.ceil((timeline.getEventTime(next) - position) / (1000.0 * speed));
            main.postDelayed(render, Math.min(250, Math.max(16, untilCue)));
        }
    }

    private void showCues(List<Cue> cues) {
        if (displayed.equals(cues)) return;
        displayed = cues;
        overlay.setCues(cues);
        overlay.setVisibility(cues.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void updatePanel() {
        boolean hasLive = session != null && session.liveCaptions;
        boolean hasTrack = actions.player() != null && actions.player().getCurrentTracks().isTypeSupported(C.TRACK_TYPE_TEXT);
        boolean showing = (source == Source.FILE && fileCues != null) || (source == Source.PC && state != null
                && (state.enabled() || "file".equals(state.state()) || "prepared".equals(state.state())))
                || (source == Source.TRACK && hasTrack && actions.player().getCurrentTracks().isTypeSelected(C.TRACK_TYPE_TEXT));
        showing = showing && !hidden;
        if (button.isSelected() != showing) {
            button.setSelected(showing);
            button.setColorFilter(activity.getColor(showing ? R.color.luma_accent : android.R.color.white));
            button.setContentDescription(activity.getText(showing ? R.string.captions_active_description : R.string.playback_captions));
        }
        if (!settings.isShowing(R.string.playback_captions) || status == null) return;
        boolean canChange = hasLive && state != null && state.canControl() && !pollFailed && !commandPending;
        setHint(status, pcStatus(hasLive));
        int detail = commandMessage != 0 ? commandMessage
                : hasLive && state != null && !state.canControl() ? R.string.captions_permission
                : hasLive && state != null && !state.canAI() ? R.string.captions_unavailable : 0;
        setHint(guidance, detail == 0 ? "" : activity.getText(detail));
        setHint(localStatus, fileMessage == 0 ? "" : activity.getText(fileMessage));
        setRow(ai, canChange && state.canAI(), state == null ? "" : switchLabel(state.enabled()));
        setRow(pcDisplay, hasLive, switchLabel(hasLive && !hidden && source == Source.PC));
        setRow(original, canChange, state == null ? "" : switchLabel(state.original()));
        setRow(reading, canChange, state == null ? "" : switchLabel(state.reading()));
        setRow(translation, canChange, translationStatus());
        retry.setEnabled(canChange && state.canAI() && state.enabled()
                && ("failed".equals(state.state()) || "failed".equals(state.translationState())));
        setRow(localDisplay, fileCues != null, switchLabel(source == Source.FILE && !hidden));
        remove.setEnabled(fileCues != null);
        setRow(tracks, hasTrack, activity.getText(hasTrack ? source == Source.TRACK
                && actions.player().getCurrentTracks().isTypeSelected(C.TRACK_TYPE_TEXT)
                ? R.string.captions_displaying : R.string.captions_off : R.string.settings_option_unavailable));
    }

    private CharSequence pcStatus(boolean live) {
        if (!live) return activity.getText(R.string.captions_legacy);
        if (commandPending) return activity.getText(R.string.captions_requesting);
        if (pollFailed) return activity.getText(R.string.captions_connection_lost);
        if (state == null) return activity.getText(R.string.captions_connecting);
        String message = switch (state.state()) {
            case "off" -> activity.getString(R.string.captions_pc_off);
            case "processing" -> activity.getString(R.string.captions_processing);
            case "ready" -> activity.getString(R.string.captions_ready, Math.round(state.aheadMs() / 1000.0));
            case "failed" -> activity.getString(R.string.captions_failed);
            case "file" -> activity.getString(R.string.captions_file);
            case "prepared" -> activity.getString(R.string.captions_prepared);
            default -> activity.getString(R.string.captions_waiting);
        };
        return state.backend().isEmpty() ? message : activity.getString(R.string.format_with_detail, message, state.backend());
    }

    private CharSequence translationStatus() {
        if (state == null) return "";
        if (!state.translation()) return switchLabel(false);
        return switch (state.translationState()) {
            case "waiting" -> activity.getText(R.string.captions_translation_waiting);
            case "processing" -> activity.getText(R.string.captions_translation_processing);
            case "failed" -> activity.getText(R.string.captions_translation_failed);
            default -> switchLabel(true);
        };
    }

    private CharSequence switchLabel(boolean enabled) {
        return activity.getText(enabled ? R.string.captions_on : R.string.captions_off);
    }

    private static void setHint(TextView view, CharSequence message) {
        view.setVisibility(message.length() == 0 ? View.GONE : View.VISIBLE);
        if (!message.equals(view.getText())) view.setText(message);
    }

    private static void setRow(PlaybackSettingsPanel.Row row, boolean enabled, CharSequence value) {
        row.setEnabled(enabled);
        row.setValue(value);
    }
}
