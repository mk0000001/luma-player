package io.github.mk0000001.lumaplayer;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.PictureInPictureParams;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Rational;
import android.view.Display;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.window.OnBackInvokedDispatcher;
import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.datasource.HttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.mediacodec.MediaCodecInfo;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.exoplayer.upstream.DefaultLoadErrorHandlingPolicy;
import androidx.media3.extractor.DefaultExtractorsFactory;
import androidx.media3.extractor.mkv.MatroskaExtractor;
import androidx.media3.ui.AspectRatioFrameLayout;
import androidx.media3.ui.PlayerView;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

@UnstableApi
public final class MainActivity extends Activity {
    private static final int CONTROLS_TIMEOUT_MS = 5000;
    private static final int NO_ORIENTATION_OVERRIDE = Integer.MIN_VALUE;
    private static final long DISPLAY_PICKER_TIMEOUT_MS = 120_000L;
    private final ScheduledThreadPoolExecutor connectionExecutor = new ScheduledThreadPoolExecutor(1);
    private final Handler lifecycleHandler = new Handler(Looper.getMainLooper());
    private final Runnable expireDisplayPicker = () -> {
        if (this.displayPickerOpen) {
            disconnect(R.string.session_ended, false);
        }
    };

    private View root;
    private ConnectionScreen connectionScreen;
    private LinearLayout playbackScreen;
    private View playbackCanvas;
    private View playbackDetails;
    private View playbackHeader;
    private View playbackFooter;
    private TextView playbackStatus;
    private TextView sessionTitle;
    private TextView sourceSummary;
    private PlayerView playerView;
    private RemotePlaybackControls navigation;
    private PlaybackGestures gestures;

    private LanClient client;
    private LanClient.RequestScope mediaRequests;
    private LanSession playbackSession;
    private ExoPlayer player;
    private PlaybackInfo playbackInfo;
    private ScheduledFuture<?> sessionCheck;
    private boolean fullscreen;
    private boolean backgroundPlaybackReady;
    private int beforeFullscreenOrientation = NO_ORIENTATION_OVERRIDE;
    private boolean returningToPortrait;
    private boolean displayPickerOpen, playAfterPicker, playedSinceLoad;
    private long streamBaseMs, bufferingSinceMs;
    private String streamProfile = "original";
    private int streamSubtitleDelayMs;

    @Override
    protected void attachBaseContext(Context base) {
        // Override only the language. Copying the entire current configuration
        // also overrides orientation and size after rotation or unfolding.
        Configuration configuration = new Configuration();
        configuration.setLocale(Locale.KOREAN);
        super.attachBaseContext(base.createConfigurationContext(configuration));
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 33) {
            setRecentsScreenshotEnabled(false);
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, () -> {
                        if (!handleBack()) {
                            finish();
                        }
                    });
        }
        if (Build.VERSION.SDK_INT >= 30 && Build.VERSION.SDK_INT < 35) {
            getWindow().setDecorFitsSystemWindows(false);
        }

        ViewGroup windowContent = findViewById(android.R.id.content);
        View content = getLayoutInflater().inflate(R.layout.main_activity, windowContent, false);
        playerView = content.findViewById(R.id.player_view);
        SurfaceView videoSurface = (SurfaceView) playerView.getVideoSurfaceView();
        videoSurface.setSecure(true);
        setContentView(content);

        root = findViewById(R.id.root);
        connectionExecutor.setRemoveOnCancelPolicy(true);
        connectionScreen = new ConnectionScreen(this, connectionExecutor);
        playbackScreen = findViewById(R.id.playback_screen);
        playbackCanvas = findViewById(R.id.playback_canvas);
        playbackDetails = findViewById(R.id.playback_details);
        playbackHeader = findViewById(R.id.playback_header);
        playbackFooter = findViewById(R.id.playback_footer);
        playbackStatus = findViewById(R.id.playback_status);
        sessionTitle = findViewById(R.id.session_title);
        sourceSummary = findViewById(R.id.source_summary);
        navigation = new RemotePlaybackControls(this, new RemotePlaybackControls.Actions() {
            @Override
            public long positionMs() {
                return absolutePosition();
            }

            @Override
            public long bufferedPositionMs() {
                return player == null ? 0 : toAbsolutePosition(player.getBufferedPosition());
            }

            @Override
            public Player player() {
                return player;
            }

            @Override
            public void seekTo(long positionMs) {
                seekPlayback(positionMs);
            }

            @Override
            public void scrubbingChanged(boolean active) {
                playerView.setControllerShowTimeoutMs(active ? 0 : CONTROLS_TIMEOUT_MS);
                playerView.showController();
            }

            @Override
            public void streamOptionsChanged() {
                updateStreamOptions();
            }

            @Override
            public void aspectChanged() {
                applyVideoAspect();
            }

            @Override
            public void showWirelessDisplay() {
                MainActivity.this.showWirelessDisplay();
            }

            @Override
            public void showPlaybackInfo() {
                MainActivity.this.showPlaybackInfo();
            }
        });
        gestures = new PlaybackGestures(playerView, findViewById(R.id.skip_feedback), new PlaybackGestures.Actions() {
            @Override public long positionMs() { return absolutePosition(); }
            @Override public long seekLimitMs() { return MainActivity.this.seekLimitMs(); }
            @Override public void seekTo(long targetMs) { seekPlayback(targetMs); }
            @Override public boolean fullscreen() { return fullscreen; }
            @Override public boolean canEnterFullscreen() { return player != null && !fullscreen && isPortrait(); }
            @Override public void setFullscreen(boolean enter) { MainActivity.this.setFullscreen(enter); }
        });

        root.setOnApplyWindowInsetsListener(this::applyWindowInsets);
        playbackScreen.addOnLayoutChangeListener((view, left, top, right, bottom,
                oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) {
                applyPlaybackLayout();
            }
        });
        playerView.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) ->
                updatePictureInPicture());
        setFullscreen(false);
        findViewById(R.id.disconnect_button).setOnClickListener(view -> disconnect());
        findViewById(R.id.device_info).setOnClickListener(view -> showPlaybackInfo());
        findViewById(R.id.playback_info).setOnClickListener(view -> showPlaybackInfo());
        playerView.setFullscreenButtonClickListener(this::setFullscreen);
        playerView.setControllerShowTimeoutMs(CONTROLS_TIMEOUT_MS);
        playerView.setControllerVisibilityListener((PlayerView.ControllerVisibilityListener)
                visibility -> navigation.setVisible(visibility == View.VISIBLE));
    }


    void openPlayback(LanClient attempt, LanSession session) {
        if (isFinishing()) {
            return;
        }
        if (!hasSecureDisplay()) {
            connectionScreen.showHome(R.string.secure_surface_unavailable, true);
            return;
        }
        try {
            client = attempt;
            startPlayback(attempt, session);
        } catch (RuntimeException e) {
            disconnect(R.string.player_start_error, true);
        }
    }

    private void startPlayback(LanClient attempt, LanSession session) {
        DefaultRenderersFactory renderers = new DefaultRenderersFactory(getApplicationContext())
                .setEnableDecoderFallback(true);
        if (Build.VERSION.SDK_INT >= 29) {
            renderers.setMediaCodecSelector((mimeType, secure, tunneling) -> {
                List<MediaCodecInfo> decoders = new ArrayList<>(
                        MediaCodecSelector.DEFAULT.getDecoderInfos(mimeType, secure, tunneling));
                decoders.sort(Comparator.comparing(decoder -> !decoder.hardwareAccelerated));
                return decoders;
            });
        }
        ExoPlayer active = new ExoPlayer.Builder(getApplicationContext(), renderers).build();
        player = active;
        backgroundPlaybackReady = PlaybackService.attach(this, active, () -> {
            if (player == active) {
                disconnect();
            }
        });
        playbackSession = session;
        navigation.setSession(session);
        playbackInfo = new PlaybackInfo();
        active.addAnalyticsListener(playbackInfo);
        active.setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA)
                .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).setAllowedCapturePolicy(C.ALLOW_CAPTURE_BY_NONE)
                .build(), true);
        active.setHandleAudioBecomingNoisy(true);
        active.addListener(new Player.Listener() {
            @Override
            public void onEvents(Player source, Player.Events events) {
                if (player == active && source.getPlayerError() == null) {
                    updatePlaybackStatus(source);
                }
                if (player == active) {
                    updatePictureInPicture();
                }
            }

            @Override
            public void onPlayerError(PlaybackException error) {
                if (player == active) {
                    if ("original".equals(streamProfile) && canUseCompatibleStream(error)) {
                        String compatible = navigation.lowerAutomaticProfile(streamProfile);
                        if (!compatible.equals(streamProfile)) {
                            long position = absolutePosition();
                            boolean playing = active.getPlayWhenReady();
                            playerView.post(() -> {
                                if (player == active && active.getPlayerError() == error) {
                                    loadStream(position, compatible, playing);
                                }
                            });
                            return;
                        }
                    }
                    playbackStatus.setText(getString(R.string.player_error, error.getErrorCodeName()));
                    playerView.showController();
                }
            }
        });

        sessionTitle.setText(session.title.isEmpty() ? getString(R.string.untitled) : session.title);
        int sourceLabel = session.isBluRay() ? R.string.source_disc
                : session.seekable ? R.string.source_file_seekable : R.string.source_file;
        int rangeLabel = switch (session.videoRange) {
            case "sdr" -> R.string.range_sdr;
            case "pq" -> R.string.range_hdr10;
            case "hlg" -> R.string.range_hlg;
            default -> R.string.range_unknown;
        };
        sourceSummary.setText(getString(R.string.video_range_format, getString(sourceLabel), getString(rangeLabel)));
        playbackStatus.setText(R.string.status_buffering);
        playerView.setKeepScreenOn(true);
        playerView.setPlayer(active);
        playbackScreen.setVisibility(View.VISIBLE);
        applyPlaybackLayout();
        connectionScreen.hideForPlayback();
        applyVideoAspect();
        playerView.showController();
        navigation.setVisible(true);
        loadStream(0, navigation.selectedProfile(), true);
        scheduleSessionCheck(attempt, 0);
    }

    private static boolean canUseCompatibleStream(PlaybackException error) {
        return switch (error.errorCode) {
            case PlaybackException.ERROR_CODE_DECODER_INIT_FAILED,
                    PlaybackException.ERROR_CODE_DECODER_QUERY_FAILED,
                    PlaybackException.ERROR_CODE_DECODING_FORMAT_EXCEEDS_CAPABILITIES,
                    PlaybackException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED -> true;
            default -> false;
        };
    }

    private long absolutePosition() {
        return player == null ? 0 : toAbsolutePosition(player.getCurrentPosition());
    }

    private long toAbsolutePosition(long relativeMs) {
        if (playbackSession == null) {
            return 0;
        }
        long current = streamBaseMs + Math.max(0, relativeMs);
        return playbackSession.durationMs > 0 ? Math.min(current, playbackSession.durationMs) : current;
    }

    private long seekLimitMs() {
        if (playbackSession == null || !(playbackSession.restartable || playbackSession.seekable)) {
            return 0;
        }
        return Math.max(0, playbackSession.durationMs - 250);
    }

    private void seekPlayback(long positionMs) {
        long limit = seekLimitMs();
        if (player == null || limit == 0) {
            return;
        }
        long target = Math.max(0, Math.min(positionMs, limit));
        if ("original".equals(streamProfile) && playbackSession.seekable && player.isCurrentMediaItemSeekable()) {
            player.seekTo(target);
        } else if (playbackSession.restartable || playbackSession.seekable) {
            loadStream(target, streamProfile, player.getPlayWhenReady());
        }
    }

    private void updateStreamOptions() {
        if (player == null || playbackSession == null) {
            return;
        }
        String profile = navigation.selectedProfile();
        if (!profile.equals(streamProfile) || navigation.subtitleDelayMs() != streamSubtitleDelayMs) {
            long position = absolutePosition();
            if (playbackSession.restartable || playbackSession.seekable) {
                loadStream(position, profile, player.getPlayWhenReady());
            }
        }
    }

    private void loadStream(long positionMs, String profile, boolean playing) {
        LanSession session = playbackSession;
        long start = session.durationMs > 0
                ? Math.max(0, Math.min(positionMs, session.durationMs - 250)) : Math.max(0, positionMs);
        boolean directFile = session.seekable && "original".equals(profile);
        long base = directFile ? 0 : start;
        boolean variant = !"original".equals(profile) || (base > 0 && session.restartable);
        String path = variant ? LanEndpoint.mediaVariantPath(profile, base) : LanEndpoint.MEDIA_PATH;
        long subtitleOrigin = base - navigation.subtitleDelayMs();
        List<MediaItem.SubtitleConfiguration> subtitles = new ArrayList<>();
        for (LanSession.Subtitle track : session.subtitles) {
            String subtitlePath = session.supportsCaptionOffset && subtitleOrigin != 0
                    ? LanEndpoint.subtitleAtPath(track.id, subtitleOrigin) : track.path;
            subtitles.add(new MediaItem.SubtitleConfiguration.Builder(Uri.parse(client.url(subtitlePath).toString()))
                    .setMimeType(MimeTypes.TEXT_VTT).setId(track.id)
                    .setLanguage(track.language).setLabel(track.label).build());
        }
        MediaItem media = new MediaItem.Builder().setUri(client.url(path).toString())
                .setMimeType(variant ? MimeTypes.VIDEO_MATROSKA : session.mimeType)
                .setMediaMetadata(new MediaMetadata.Builder().setTitle(sessionTitle.getText()).build())
                .setSubtitleConfigurations(subtitles).build();
        LanClient.RequestScope previousRequests = mediaRequests;
        mediaRequests = client.mediaRequests();
        DefaultExtractorsFactory extractors = new DefaultExtractorsFactory();
        boolean progressive = session.isBluRay() || variant;
        if (progressive) {
            extractors.setMatroskaExtractorFlags(MatroskaExtractor.FLAG_DISABLE_SEEK_FOR_CUES);
        }
        DefaultMediaSourceFactory sources = new DefaultMediaSourceFactory(mediaRequests.dataSourceFactory(), extractors);
        if (progressive) {
            sources.setLoadErrorHandlingPolicy(new DefaultLoadErrorHandlingPolicy(0) {
                @Override
                public long getRetryDelayMsFor(LoadErrorInfo info) {
                    if (info.exception instanceof HttpDataSource.InvalidResponseCodeException response
                            && response.responseCode == 503 && response.headerFields.keySet().stream()
                                    .anyMatch("Retry-After"::equalsIgnoreCase) && info.errorCount <= 3) {
                        return 500L * info.errorCount;
                    }
                    return C.TIME_UNSET;
                }
            });
        }
        streamBaseMs = base;
        streamProfile = profile;
        streamSubtitleDelayMs = navigation.subtitleDelayMs();
        playedSinceLoad = false;
        bufferingSinceMs = 0;
        try {
            player.setMediaSource(sources.createMediaSource(media), directFile ? start : 0);
        } finally {
            if (previousRequests != null) {
                previousRequests.close();
            }
        }
        player.prepare();
        player.setPlayWhenReady(playing);
    }

    private void scheduleSessionCheck(LanClient active, int previousFailures) {
        sessionCheck = connectionExecutor.schedule(() -> {
            int failures = 0;
            LanSession currentSession = null;
            try {
                currentSession = active.checkSession();
            } catch (LanClient.HttpException e) {
                failures = e.status == 401 || e.status == 403 || e.status == 409 || e.status == 410
                        ? 2 : previousFailures + 1;
            } catch (IOException e) {
                failures = previousFailures + 1;
            }
            int consecutiveFailures = failures;
            LanSession received = currentSession;
            runOnUiThread(() -> {
                if (client != active || player == null) {
                    return;
                }
                if (consecutiveFailures >= 2) {
                    disconnect(R.string.session_ended, true);
                } else {
                    if (received != null) {
                        navigation.applyRemoteOptions(received.options);
                    }
                    scheduleSessionCheck(active, consecutiveFailures);
                }
            });
        }, 5, TimeUnit.SECONDS);
    }

    private void updatePlaybackStatus(Player source) {
        int state = source.getPlaybackState();
        int message = R.string.status_buffering;
        if (state == Player.STATE_READY) {
            playedSinceLoad |= source.isPlaying();
            bufferingSinceMs = 0;
            message = source.isPlaying() ? R.string.status_playing : R.string.status_paused;
        } else if (state == Player.STATE_ENDED) {
            message = R.string.status_ended;
        } else if (state == Player.STATE_BUFFERING && playedSinceLoad && source.getPlayWhenReady()
                && bufferingSinceMs == 0 && !navigation.lowerAutomaticProfile(streamProfile).equals(streamProfile)) {
            long started = SystemClock.elapsedRealtime();
            bufferingSinceMs = started;
            playerView.postDelayed(() -> {
                if (player == source && bufferingSinceMs == started && source.getPlayWhenReady()
                        && source.getPlaybackState() == Player.STATE_BUFFERING) {
                    String lower = navigation.lowerAutomaticProfile(streamProfile);
                    if (!lower.equals(streamProfile)) {
                        loadStream(absolutePosition(), lower, true);
                    }
                }
            }, 4000);
        }
        CharSequence text = getText(message);
        if (!text.equals(playbackStatus.getText())) {
            playbackStatus.setText(text);
        }
        playerView.setKeepScreenOn(source.getPlayWhenReady() && state != Player.STATE_ENDED);
    }


    private void disconnect() {
        disconnect(R.string.disconnected, false);
    }

    private void closeMediaRequests() {
        if (mediaRequests != null) {
            mediaRequests.close();
            mediaRequests = null;
        }
    }

    private void disconnect(int message, boolean error) {
        lifecycleHandler.removeCallbacks(expireDisplayPicker);
        displayPickerOpen = playAfterPicker = false;
        gestures.cancel();
        if (sessionCheck != null) {
            sessionCheck.cancel(false);
            sessionCheck = null;
        }
        closeMediaRequests();
        if (client != null) {
            client.close();
            client = null;
        }
        if (player != null) {
            ExoPlayer previous = player;
            player = null;
            backgroundPlaybackReady = false;
            PlaybackService.detach(this, previous);
            playerView.setPlayer(null);
            previous.release();
        }
        playbackInfo = null;
        playbackSession = null;
        navigation.setSession(null);
        updatePictureInPicture();
        playerView.setControllerShowTimeoutMs(CONTROLS_TIMEOUT_MS);
        playerView.setKeepScreenOn(false);
        playbackScreen.setVisibility(View.GONE);
        setFullscreen(false);
        connectionScreen.showHome(message, error);
    }

    void onNearbyNetworkLost() {
        if (client != null) {
            disconnect(R.string.nearby_wifi_lost, true);
        }
    }


    void protectPairingForm(boolean protect) {
        if (protect) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_SECURE);
        }
    }

    @SuppressWarnings("deprecation")
    private boolean hasSecureDisplay() {
        Display display = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
        return display != null && (display.getFlags() & Display.FLAG_SECURE) != 0;
    }

    private void showPlaybackInfo() {
        new AlertDialog.Builder(this).setTitle(player == null ? R.string.capability_action : R.string.playback_info_action)
                .setMessage(PlaybackInfo.report(this, player, playbackInfo))
                .setPositiveButton(android.R.string.ok, null).show();
    }

    private void showWirelessDisplay() {
        try {
            displayPickerOpen = true;
            playAfterPicker = player != null && player.getPlayWhenReady();
            updatePictureInPicture();
            startActivity(new Intent(Settings.ACTION_CAST_SETTINGS));
            if (player != null) {
                player.pause();
            }
        } catch (ActivityNotFoundException | SecurityException e) {
            displayPickerOpen = false;
            playAfterPicker = false;
            updatePictureInPicture();
            new AlertDialog.Builder(this).setTitle(R.string.wireless_display).setMessage(R.string.smart_view_help)
                    .setPositiveButton(android.R.string.ok, null).show();
        }
    }

    @SuppressWarnings("deprecation")
    private WindowInsets applyWindowInsets(View view, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
            if (fullscreen || isInPictureInPictureMode()) {
                view.setPadding(0, 0, 0, 0);
                navigation.setInsets(bars.left, bars.right, bars.bottom);
            } else {
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, keyboard.bottom));
                navigation.setInsets(0, 0, 0);
            }
        } else {
            if (fullscreen || isInPictureInPictureMode()) {
                view.setPadding(0, 0, 0, 0);
                navigation.setInsets(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom());
            } else {
                view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
                navigation.setInsets(0, 0, 0);
            }
        }
        return insets;
    }

    @SuppressWarnings("deprecation")
    private void setFullscreen(boolean fullscreen) {
        boolean changed = this.fullscreen != fullscreen;
        this.fullscreen = fullscreen;
        if (changed) {
            updateFullscreenOrientation(fullscreen);
        }
        playbackHeader.setVisibility(fullscreen ? View.GONE : View.VISIBLE);
        playbackFooter.setVisibility(fullscreen ? View.GONE : View.VISIBLE);
        applyPlaybackLayout();
        playerView.setFullscreenButtonState(fullscreen);
        ImageView fullscreenIcon = playerView.findViewById(androidx.media3.ui.R.id.exo_fullscreen);
        fullscreenIcon.setImageResource(fullscreen ? R.drawable.ic_playback_exit_fullscreen
                : R.drawable.ic_playback_fullscreen);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = fullscreen
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
        getWindow().setAttributes(attributes);
        applyVideoAspect();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                if (fullscreen) {
                    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    controller.hide(WindowInsets.Type.systemBars());
                } else {
                    controller.show(WindowInsets.Type.systemBars());
                }
            }
        } else {
            int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
            if (fullscreen) {
                flags |= View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            }
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
        root.requestApplyInsets();
    }

    private boolean isPortrait() {
        if (Build.VERSION.SDK_INT >= 30) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            return bounds.width() < bounds.height();
        }
        return getResources().getConfiguration().orientation == Configuration.ORIENTATION_PORTRAIT;
    }

    private void applyPlaybackLayout() {
        boolean videoOnly = fullscreen || isInPictureInPictureMode();
        boolean portrait = !videoOnly && isPortrait();
        int width = playbackScreen.getWidth();
        float density = getResources().getDisplayMetrics().density;
        Configuration configuration = getResources().getConfiguration();
        int widthDp = width > 0 ? Math.round(width / density) : configuration.screenWidthDp;
        int height = playbackScreen.getHeight();
        int heightDp = height > 0 ? Math.round(height / density) : configuration.screenHeightDp;
        boolean sidePanel = !videoOnly && widthDp >= 720 && heightDp >= 420 && widthDp >= heightDp;
        navigation.setPortraitChapters(portrait || sidePanel);
        playbackDetails.setVisibility(videoOnly ? View.GONE : View.VISIBLE);
        if (portrait && width == 0) {
            return;
        }
        int orientation = sidePanel ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL;
        if (playbackScreen.getOrientation() != orientation) {
            playbackScreen.setOrientation(orientation);
        }
        if (sidePanel) {
            setPaneSize(playbackCanvas, 0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
            int panelWidth = Math.round(Math.min(360, Math.max(280, widthDp * .29f)) * density);
            setPaneSize(playbackDetails, panelWidth, ViewGroup.LayoutParams.MATCH_PARENT, 0);
        } else {
            int playerHeight = portrait ? Math.round(Math.min(width * 9f / 16f,
                    heightDp * density * .58f)) : 0;
            setPaneSize(playbackCanvas, ViewGroup.LayoutParams.MATCH_PARENT, playerHeight, portrait ? 0 : 1);
            setPaneSize(playbackDetails, ViewGroup.LayoutParams.MATCH_PARENT,
                    portrait ? 0 : ViewGroup.LayoutParams.WRAP_CONTENT, portrait ? 1 : 0);
        }
    }

    private static void setPaneSize(View view, int width, int height, float weight) {
        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) view.getLayoutParams();
        if (params.width != width || params.height != height || params.weight != weight) {
            params.width = width;
            params.height = height;
            params.weight = weight;
            view.setLayoutParams(params);
        }
    }

    private void updateFullscreenOrientation(boolean entering) {
        if (entering && (isPortrait() || returningToPortrait) && !isInMultiWindowMode()) {
            if (beforeFullscreenOrientation == NO_ORIENTATION_OVERRIDE) {
                beforeFullscreenOrientation = getRequestedOrientation();
            }
            returningToPortrait = false;
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        } else if (!entering && beforeFullscreenOrientation != NO_ORIENTATION_OVERRIDE) {
            if (isPortrait() || isInMultiWindowMode()) {
                restoreRequestedOrientation();
            } else {
                returningToPortrait = true;
                setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
            }
        }
    }

    @SuppressLint("WrongConstant") // This is the value returned by getRequestedOrientation().
    private void restoreRequestedOrientation() {
        int requested = beforeFullscreenOrientation;
        returningToPortrait = false;
        beforeFullscreenOrientation = NO_ORIENTATION_OVERRIDE;
        setRequestedOrientation(requested);
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        applyPlaybackLayout();
        if (returningToPortrait && configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
            root.postDelayed(() -> {
                if (returningToPortrait && !fullscreen && !isFinishing()) {
                    restoreRequestedOrientation();
                }
            }, 350);
        }
    }

    private void applyVideoAspect() {
        playerView.setResizeMode(fullscreen && !isInPictureInPictureMode() && navigation.fillScreen()
                ? AspectRatioFrameLayout.RESIZE_MODE_ZOOM : AspectRatioFrameLayout.RESIZE_MODE_FIT);
    }

    private boolean supportsPictureInPicture() {
        return getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE);
    }

    private boolean canAutoEnterPictureInPicture() {
        return player != null && player.getPlayWhenReady() && player.getPlayerError() == null
                && player.getPlaybackState() != Player.STATE_ENDED && player.getVideoSize().width > 0
                && player.getVideoSize().height > 0
                && !displayPickerOpen && !isFinishing();
    }

    private PictureInPictureParams pictureInPictureParams() {
        PictureInPictureParams.Builder params = new PictureInPictureParams.Builder();
        boolean ready = canAutoEnterPictureInPicture();
        if (Build.VERSION.SDK_INT >= 31) {
            params.setAutoEnterEnabled(ready).setSeamlessResizeEnabled(true);
        }
        if (ready) {
            var size = player.getVideoSize();
            int width = Math.round(size.width * size.pixelWidthHeightRatio);
            float ratio = (float) width / size.height;
            params.setAspectRatio(ratio < .4185f ? new Rational(100, 239)
                    : ratio > 2.39f ? new Rational(239, 100) : new Rational(width, size.height));
            Rect videoBounds = new Rect();
            if (playerView.getGlobalVisibleRect(videoBounds)) {
                params.setSourceRectHint(videoBounds);
            }
        }
        return params.build();
    }

    private void updatePictureInPicture() {
        if (supportsPictureInPicture() && !isInPictureInPictureMode()) {
            setPictureInPictureParams(pictureInPictureParams());
        }
    }

    @Override
    protected void onUserLeaveHint() {
        super.onUserLeaveHint();
        if (Build.VERSION.SDK_INT < 31 && supportsPictureInPicture() && canAutoEnterPictureInPicture()) {
            enterPictureInPictureMode(pictureInPictureParams());
        }
    }

    @Override
    public void onPictureInPictureModeChanged(boolean inPictureInPicture, Configuration configuration) {
        super.onPictureInPictureModeChanged(inPictureInPicture, configuration);
        gestures.cancel();
        playerView.setUseController(!inPictureInPicture);
        applyPlaybackLayout();
        applyVideoAspect();
        root.requestApplyInsets();
    }

    private boolean handleBack() {
        if (fullscreen) {
            setFullscreen(false);
        } else if (client != null) {
            disconnect();
        } else {
            return connectionScreen.handleBack();
        }
        return true;
    }

    @Override
    @SuppressWarnings("deprecation")
    @SuppressLint("GestureBackNavigation") // Legacy callback on Android 12 and earlier.
    public void onBackPressed() {
        if (!handleBack()) {
            super.onBackPressed();
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        navigation.setForeground(true);
        connectionScreen.start();
    }

    @Override
    protected void onResume() {
        super.onResume();
        lifecycleHandler.removeCallbacks(expireDisplayPicker);
        if (displayPickerOpen) {
            displayPickerOpen = false;
            if (player != null && playAfterPicker) {
                player.play();
            }
            playAfterPicker = false;
        }
        updatePictureInPicture();
    }

    @Override
    protected void onStop() {
        navigation.setForeground(false);
        connectionScreen.stop();
        if (client != null) {
            if (displayPickerOpen) {
                lifecycleHandler.postDelayed(expireDisplayPicker, DISPLAY_PICKER_TIMEOUT_MS);
            } else if (!isInPictureInPictureMode() && !(backgroundPlaybackReady && player != null
                    && player.getPlayWhenReady() && player.getPlaybackState() != Player.STATE_ENDED
                    && player.getPlayerError() == null)) {
                disconnect();
            }
        } else if (client == null && !connectionScreen.awaitingNearbySystemUi()) {
            connectionScreen.showHome(0, false);
        }
        super.onStop();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        connectionScreen.onRequestPermissionsResult(requestCode);
    }

    @Override
    protected void onDestroy() {
        lifecycleHandler.removeCallbacks(expireDisplayPicker);
        if (client != null) {
            disconnect(0, false);
        }
        gestures.cancel();
        connectionScreen.close();
        connectionExecutor.shutdownNow();
        super.onDestroy();
    }
}
