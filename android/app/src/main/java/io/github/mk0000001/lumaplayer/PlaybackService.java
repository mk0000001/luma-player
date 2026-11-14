package io.github.mk0000001.lumaplayer;

import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.Player;
import androidx.media3.common.SimpleBasePlayer;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.common.util.Util;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.session.DefaultMediaNotificationProvider;
import androidx.media3.session.MediaSession;
import androidx.media3.session.MediaSessionService;
import androidx.media3.session.SessionCommands;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;
import java.util.Collections;
import java.util.function.Consumer;

@UnstableApi
public final class PlaybackService extends MediaSessionService {
    private static final String ATTACH = "io.github.mk0000001.lumaplayer.ATTACH_PLAYBACK";
    private static final int NOTIFICATION_ID = 0x4c55;
    private static final Player.Commands CONTROL_COMMANDS = new Player.Commands.Builder()
            .add(Player.COMMAND_PLAY_PAUSE)
            .add(Player.COMMAND_STOP)
            .add(Player.COMMAND_GET_CURRENT_MEDIA_ITEM)
            .add(Player.COMMAND_GET_METADATA)
            .add(Player.COMMAND_GET_TIMELINE)
            .build();
    private static final Player.Commands LOCAL_COMMANDS = new Player.Commands.Builder()
            .addAll(CONTROL_COMMANDS).add(Player.COMMAND_RELEASE).build();

    // The service and Activity use the same main looper. The Activity owns and releases the player.
    private static Attachment pending;
    private static PlaybackService running;
    private Attachment current;
    private MediaSession session;
    private ControlsPlayer controls;
    private final Handler main = new Handler(Looper.getMainLooper());

    static boolean attach(Context context, ExoPlayer player, Consumer<Boolean> requestPlayback, Runnable onStopped) {
        if (Looper.myLooper() != Looper.getMainLooper() || player.getApplicationLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("Playback service requires the main player looper");
        }
        if (pending != null) {
            return pending.player == player;
        }
        Context app = context.getApplicationContext();
        pending = new Attachment(player, requestPlayback, onStopped);
        try {
            player.setWakeMode(C.WAKE_MODE_NETWORK);
            if (app.startService(new Intent(app, PlaybackService.class).setAction(ATTACH)) != null) {
                return true;
            }
        } catch (IllegalStateException | SecurityException e) {
            // The Activity can continue foreground playback if Android refuses the service.
        }
        pending = null;
        player.setWakeMode(C.WAKE_MODE_NONE);
        return false;
    }

    static void detach(Context context, ExoPlayer player) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("Playback service requires the main player looper");
        }
        if (pending == null || pending.player != player) {
            return;
        }
        Attachment detached = pending;
        pending = null;
        if (running != null && running.current == detached) {
            running.releaseSession();
        }
        player.setWakeMode(C.WAKE_MODE_NONE);
        context.stopService(new Intent(context, PlaybackService.class));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        running = this;
        setMediaNotificationProvider(new DefaultMediaNotificationProvider.Builder(this)
                .setNotificationId(NOTIFICATION_ID).build());
        setListener(new Listener() {
            @Override
            public void onForegroundServiceStartNotAllowedException() {
                finishPlayback(current);
            }
        });
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ATTACH.equals(intent.getAction()) && current != pending) {
            releaseSession();
            current = pending;
            if (current != null) {
                try {
                    addPlaybackSession(current);
                } catch (RuntimeException e) {
                    finishPlayback(current);
                }
            }
        }
        super.onStartCommand(intent, flags, startId);
        if (current == null) {
            stopSelfResult(startId);
        }
        return START_NOT_STICKY;
    }

    private void addPlaybackSession(Attachment attachment) {
        controls = new ControlsPlayer(attachment.player, getString(R.string.app_name), attachment.requestPlayback,
                () -> main.post(() -> finishPlayback(attachment)));
        Intent reopen = new Intent(this, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent activity = PendingIntent.getActivity(this, 0, reopen, PendingIntent.FLAG_IMMUTABLE);
        session = new MediaSession.Builder(this, controls).setSessionActivity(activity)
                .setCallback(new MediaSession.Callback() {
                    @Override
                    public ListenableFuture<MediaSession.ConnectionResult> onConnectAsync(
                            MediaSession session, MediaSession.ControllerInfo controller) {
                        return Futures.immediateFuture(allowedController(controller)
                                ? MediaSession.ConnectionResult.accept(SessionCommands.EMPTY, CONTROL_COMMANDS)
                                : MediaSession.ConnectionResult.reject());
                    }
                }).build();
        addSession(session);
    }

    private boolean allowedController(MediaSession.ControllerInfo controller) {
        int uid = controller.getUid();
        // Notification-listener access alone does not grant transport control.
        return uid == Process.myUid() || uid == Process.SYSTEM_UID || (uid >= 0 && controller.isTrusted()
                && getPackageManager().checkSignatures(Process.SYSTEM_UID, uid) == PackageManager.SIGNATURE_MATCH);
    }

    @Override
    public MediaSession onGetSession(MediaSession.ControllerInfo controllerInfo) {
        return allowedController(controllerInfo) ? session : null;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        finishPlayback(current != null ? current : pending);
    }

    private void finishPlayback(Attachment attachment) {
        if (attachment == null || attachment != pending) {
            if (current == null && pending == null) {
                stopSelf();
            }
            return;
        }
        pending = null;
        releaseSession();
        stopSelf();
        attachment.stopAndNotify();
    }

    private void releaseSession() {
        current = null;
        MediaSession previous = session;
        session = null;
        if (previous != null) {
            if (isSessionAdded(previous)) {
                removeSession(previous);
            }
            previous.release();
            stopForeground(STOP_FOREGROUND_REMOVE);
            NotificationManager notifications = getSystemService(NotificationManager.class);
            if (notifications != null) {
                notifications.cancel(NOTIFICATION_ID);
            }
        }
        if (controls != null) {
            controls.release();
            controls = null;
        }
    }

    @Override
    public void onDestroy() {
        if (running == this) {
            running = null;
        }
        Attachment abandoned = current;
        releaseSession();
        super.onDestroy();
        if (abandoned != null && abandoned == pending) {
            pending = null;
            abandoned.stopAndNotify();
        }
    }

    private record Attachment(ExoPlayer player, Consumer<Boolean> requestPlayback, Runnable onStopped) {
        void stopAndNotify() {
            player.pause();
            player.setWakeMode(C.WAKE_MODE_NONE);
            onStopped.run();
        }
    }

    // A session view of the player exposes transport state without publishing LAN MediaItems.
    private static final class ControlsPlayer extends SimpleBasePlayer {
        private final ExoPlayer player;
        private final MediaItem displayItem;
        private final Consumer<Boolean> requestPlayback;
        private final Runnable onStop;
        private final Player.Listener listener = new Player.Listener() {
            @Override
            public void onEvents(Player player, Player.Events events) {
                invalidateState();
            }
        };

        ControlsPlayer(ExoPlayer player, String title, Consumer<Boolean> requestPlayback, Runnable onStop) {
            super(player.getApplicationLooper());
            this.player = player;
            this.requestPlayback = requestPlayback;
            this.onStop = onStop;
            displayItem = new MediaItem.Builder().setMediaId("luma-playback")
                    .setMediaMetadata(new MediaMetadata.Builder().setTitle(title).build()).build();
            player.addListener(listener);
        }

        @Override
        protected State getState() {
            boolean hasMedia = player.getMediaItemCount() != 0;
            State.Builder state = new State.Builder().setAvailableCommands(LOCAL_COMMANDS)
                    .setPlaybackState(hasMedia ? player.getPlaybackState() : Player.STATE_IDLE)
                    .setPlayWhenReady(player.getPlayWhenReady(), Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
                    .setPlaybackSuppressionReason(player.getPlaybackSuppressionReason())
                    .setPlaybackParameters(player.getPlaybackParameters());
            if (hasMedia) {
                state.setPlaylist(Collections.singletonList(new MediaItemData.Builder("luma-playback")
                                .setMediaItem(displayItem).setDurationUs(Util.msToUs(player.getDuration())).build()))
                        .setContentPositionMs(player.getContentPosition());
            }
            return state.build();
        }

        @Override
        protected ListenableFuture<?> handleSetPlayWhenReady(boolean playWhenReady) {
            requestPlayback.accept(playWhenReady);
            return Futures.immediateVoidFuture();
        }

        @Override
        protected ListenableFuture<?> handleStop() {
            player.pause();
            onStop.run();
            return Futures.immediateVoidFuture();
        }

        @Override
        protected ListenableFuture<?> handleRelease() {
            player.removeListener(listener);
            return Futures.immediateVoidFuture();
        }
    }
}
