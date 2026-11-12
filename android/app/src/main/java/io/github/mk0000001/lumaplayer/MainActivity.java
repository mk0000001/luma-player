package io.github.mk0000001.lumaplayer;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;
import android.window.OnBackInvokedDispatcher;
import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.mediacodec.MediaCodecInfo;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.exoplayer.upstream.DefaultLoadErrorHandlingPolicy;
import androidx.media3.extractor.DefaultExtractorsFactory;
import androidx.media3.extractor.mkv.MatroskaExtractor;
import androidx.media3.ui.PlayerView;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

@UnstableApi
public final class MainActivity extends Activity {
    private final ScheduledExecutorService connectionExecutor = Executors.newSingleThreadScheduledExecutor();

    private View root;
    private ConnectionScreen connectionScreen;
    private View playbackScreen;
    private View playbackHeader;
    private View playbackFooter;
    private TextView playbackStatus;
    private TextView sessionTitle;
    private TextView sourceSummary;
    private PlayerView playerView;

    private LanClient client;
    private ExoPlayer player;
    private PlaybackInfo playbackInfo;
    private ScheduledFuture<?> sessionCheck;
    private boolean fullscreen;

    @Override
    protected void attachBaseContext(Context base) {
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
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
        connectionScreen = new ConnectionScreen(this, connectionExecutor);
        playbackScreen = findViewById(R.id.playback_screen);
        playbackHeader = findViewById(R.id.playback_header);
        playbackFooter = findViewById(R.id.playback_footer);
        playbackStatus = findViewById(R.id.playback_status);
        sessionTitle = findViewById(R.id.session_title);
        sourceSummary = findViewById(R.id.source_summary);

        root.setOnApplyWindowInsetsListener(this::applyWindowInsets);
        setFullscreen(false);
        findViewById(R.id.disconnect_button).setOnClickListener(view -> disconnect());
        findViewById(R.id.device_info).setOnClickListener(view -> showPlaybackInfo());
        findViewById(R.id.playback_info).setOnClickListener(view -> showPlaybackInfo());
        playerView.setFullscreenButtonClickListener(this::setFullscreen);
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
        DefaultExtractorsFactory extractors = new DefaultExtractorsFactory();
        if (session.isBluRay()) {
            extractors.setMatroskaExtractorFlags(MatroskaExtractor.FLAG_DISABLE_SEEK_FOR_CUES);
        }
        DefaultMediaSourceFactory mediaSources = new DefaultMediaSourceFactory(
                attempt.mediaDataSourceFactory(), extractors);
        if (session.isBluRay()) {
            mediaSources.setLoadErrorHandlingPolicy(new DefaultLoadErrorHandlingPolicy(0));
        }
        DefaultRenderersFactory renderers = new DefaultRenderersFactory(this).setEnableDecoderFallback(true);
        if (Build.VERSION.SDK_INT >= 29) {
            renderers.setMediaCodecSelector((mimeType, secure, tunneling) -> {
                List<MediaCodecInfo> decoders = new ArrayList<>(
                        MediaCodecSelector.DEFAULT.getDecoderInfos(mimeType, secure, tunneling));
                decoders.sort(Comparator.comparing(decoder -> !decoder.hardwareAccelerated));
                return decoders;
            });
        }
        ExoPlayer active = new ExoPlayer.Builder(this, renderers).setMediaSourceFactory(mediaSources).build();
        player = active;
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
            }

            @Override
            public void onPlayerError(PlaybackException error) {
                if (player == active) {
                    setFullscreen(false);
                    playbackStatus.setText(getString(R.string.player_error, error.getErrorCodeName()));
                    playerView.showController();
                }
            }
        });

        List<MediaItem.SubtitleConfiguration> subtitles = new ArrayList<>();
        for (LanSession.Subtitle track : session.subtitles) {
            subtitles.add(new MediaItem.SubtitleConfiguration.Builder(Uri.parse(attempt.url(track.path).toString()))
                    .setMimeType(MimeTypes.TEXT_VTT).setId(track.id)
                    .setLanguage(track.language).setLabel(track.label).build());
        }
        MediaItem media = new MediaItem.Builder().setUri(attempt.url(LanEndpoint.MEDIA_PATH).toString())
                .setMimeType(session.mimeType).setSubtitleConfigurations(subtitles).build();

        sessionTitle.setText(session.title.isEmpty() ? getString(R.string.untitled) : session.title);
        int sourceLabel = session.isBluRay() ? R.string.source_disc
                : session.seekable ? R.string.source_file_seekable : R.string.source_file;
        sourceSummary.setText(sourceLabel);
        playbackStatus.setText(R.string.status_buffering);
        playerView.setKeepScreenOn(true);
        playerView.setShowFastForwardButton(session.seekable);
        playerView.setShowRewindButton(session.seekable);
        playerView.setPlayer(active);
        playbackScreen.setVisibility(View.VISIBLE);
        connectionScreen.hideForPlayback();
        playerView.showController();
        active.setMediaItem(media);
        active.prepare();
        active.play();
        scheduleSessionCheck(attempt, 0);
    }

    private void scheduleSessionCheck(LanClient active, int previousFailures) {
        sessionCheck = connectionExecutor.schedule(() -> {
            int failures = 0;
            try {
                active.checkSession();
            } catch (LanClient.HttpException e) {
                failures = e.status == 401 || e.status == 403 || e.status == 409 || e.status == 410
                        ? 2 : previousFailures + 1;
            } catch (IOException e) {
                failures = previousFailures + 1;
            }
            int consecutiveFailures = failures;
            runOnUiThread(() -> {
                if (client != active || player == null) {
                    return;
                }
                if (consecutiveFailures >= 2) {
                    disconnect(R.string.session_ended, true);
                } else {
                    scheduleSessionCheck(active, consecutiveFailures);
                }
            });
        }, 5, TimeUnit.SECONDS);
    }

    private void updatePlaybackStatus(Player source) {
        int state = source.getPlaybackState();
        int message = R.string.status_buffering;
        if (state == Player.STATE_READY) {
            message = source.isPlaying() ? R.string.status_playing : R.string.status_paused;
        } else if (state == Player.STATE_ENDED) {
            message = R.string.status_ended;
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

    private void disconnect(int message, boolean error) {
        if (sessionCheck != null) {
            sessionCheck.cancel(false);
            sessionCheck = null;
        }
        if (client != null) {
            client.close();
            client = null;
        }
        if (player != null) {
            ExoPlayer previous = player;
            player = null;
            playerView.setPlayer(null);
            previous.release();
        }
        playbackInfo = null;
        playerView.setKeepScreenOn(false);
        playbackScreen.setVisibility(View.GONE);
        setFullscreen(false);
        connectionScreen.showHome(message, error);
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

    @SuppressWarnings("deprecation")
    private WindowInsets applyWindowInsets(View view, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
            view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, keyboard.bottom));
        } else {
            view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
        }
        return insets;
    }

    @SuppressWarnings("deprecation")
    private void setFullscreen(boolean fullscreen) {
        this.fullscreen = fullscreen;
        playbackHeader.setVisibility(fullscreen ? View.GONE : View.VISIBLE);
        playbackFooter.setVisibility(fullscreen ? View.GONE : View.VISIBLE);
        playerView.setFullscreenButtonState(fullscreen);
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
        connectionScreen.start();
    }

    @Override
    protected void onStop() {
        connectionScreen.stop();
        if (client != null) {
            disconnect();
        } else {
            connectionScreen.showHome(0, false);
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        connectionScreen.close();
        connectionExecutor.shutdownNow();
        super.onDestroy();
    }
}
