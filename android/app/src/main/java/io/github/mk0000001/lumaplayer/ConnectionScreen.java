package io.github.mk0000001.lumaplayer;

import android.app.AlertDialog;
import android.graphics.Typeface;
import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Build;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;
import androidx.media3.common.util.UnstableApi;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.net.SocketException;
import java.security.GeneralSecurityException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import javax.net.ssl.SSLException;
import org.json.JSONException;

@UnstableApi
final class ConnectionScreen {
    private static final long DISCOVERY_REFRESH_MS = 5000;
    private final MainActivity activity;
    private final PcController controller;
    private final ScheduledExecutorService network;
    private final ExecutorService discoveryExecutor = Executors.newSingleThreadExecutor();
    private final RememberedPcStore remembered;
    private final View home;
    private final View pairScreen;
    private final EditText address;
    private final TextView homeStatus;
    private final TextView discoveryStatus;
    private final LinearLayout pcList;
    private final Button refresh;
    private final Button nearbyButton;
    private final Button vpnSearch;
    private final TextView pairName;
    private final TextView pairAddress;
    private final TextView pairStatus;
    private final TextView pairCode;
    private final RadioGroup options;
    private final Button startPairing;
    private final Button confirmCode;
    private final Button startPlayback;
    private final Button pcControls;
    private final Runnable refreshDiscovery = this::refreshOrdinaryDiscovery;
    private int mediaRequestGeneration;
    private boolean controlInFlight;
    private LanClient waitingClient;
    private LanClient menuClient;
    private ScheduledFuture<?> menuRequest;
    private int menuGeneration;

    private boolean foreground;
    private boolean playing;
    private boolean showingVpnResults;
    private LanDiscovery scan;
    private NearbyConnection nearby;
    private AlertDialog vpnDialog;
    private List<LanDiscovery.Pc> discovered = Collections.emptyList();
    private List<LanDiscovery.Pc> ordinaryDiscovered = Collections.emptyList();
    private LanDiscovery.Pc selected;
    private Network selectedNetwork;
    private boolean selectedNearby;
    private PairingConnection attempt;
    private PairingConnection.Pending awaitingConfirmation;
    private ScheduledFuture<?> request;

    ConnectionScreen(MainActivity activity, ScheduledExecutorService network) {
        this.activity = activity;
        this.network = network;
        controller = new PcController(activity);
        remembered = new RememberedPcStore(activity);
        home = activity.findViewById(R.id.connect_screen);
        pairScreen = activity.findViewById(R.id.pair_screen);
        address = activity.findViewById(R.id.address);
        homeStatus = activity.findViewById(R.id.connection_status);
        discoveryStatus = activity.findViewById(R.id.discovery_status);
        pcList = activity.findViewById(R.id.pc_list);
        refresh = activity.findViewById(R.id.refresh_button);
        nearbyButton = activity.findViewById(R.id.nearby_pc_button);
        vpnSearch = activity.findViewById(R.id.vpn_search_button);
        pairName = activity.findViewById(R.id.pair_pc_name);
        pairAddress = activity.findViewById(R.id.pair_pc_address);
        pairStatus = activity.findViewById(R.id.pair_status);
        pairCode = activity.findViewById(R.id.pair_code);
        options = activity.findViewById(R.id.pair_options);
        startPairing = activity.findViewById(R.id.pair_start_button);
        confirmCode = activity.findViewById(R.id.pair_confirm_button);
        startPlayback = activity.findViewById(R.id.pair_playback_button);
        pcControls = activity.findViewById(R.id.pair_pc_controls);

        activity.findViewById(R.id.connect_content).requestFocus();
        refresh.setOnClickListener(view -> discover());
        nearbyButton.setOnClickListener(view -> showNearby());
        vpnSearch.setOnClickListener(view -> showVpnSearch());
        activity.findViewById(R.id.connect_button).setOnClickListener(view -> selectAddress());
        activity.findViewById(R.id.pair_cancel_button).setOnClickListener(view -> showHome(0, false));
        startPairing.setOnClickListener(view -> startApproval());
        confirmCode.setOnClickListener(view -> confirmMatchingCode());
        address.setOnEditorActionListener((view, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_GO) {
                selectAddress();
                return true;
            }
            return false;
        });
    }

    void start() {
        foreground = true;
        if (nearby != null) {
            nearby.setForeground(true);
        }
        if (waitingClient != null) {
            checkWaitingClient(waitingClient, 0);
        } else if (nearby == null && home.getVisibility() == View.VISIBLE) {
            discover();
        }
    }

    void stop() {
        foreground = false;
        stopMenuPreview();
        controller.reset();
        stopDiscovery();
        closeVpnSearch();
        if (nearby != null) {
            nearby.setForeground(false);
        }
        if (!awaitingNearbySystemUi()) {
            if (waitingClient != null) {
                ++mediaRequestGeneration;
                if (request != null) request.cancel(false);
                request = null;
            } else {
                cancelAttempt();
            }
            if (!playing) {
                if (waitingClient == null) closeNearby();
            }
            clearCode();
        }
    }

    void close() {
        stopMenuPreview();
        if (waitingClient != null) {
            waitingClient.close();
            waitingClient = null;
        }
        cancelAttempt();
        stopDiscovery();
        closeVpnSearch();
        closeNearby();
        discoveryExecutor.shutdownNow();
    }

    boolean awaitingNearbySystemUi() {
        return nearby != null && nearby.awaitingSystemUi();
    }

    void onRequestPermissionsResult(int requestCode) {
        if (nearby != null) {
            nearby.onRequestPermissionsResult(requestCode);
        }
    }

    boolean handleBack() {
        if (closePcControls()) return true;
        if (pairScreen.getVisibility() != View.VISIBLE) {
            return false;
        }
        showHome(0, false);
        return true;
    }

    void showHome(int message, boolean error) {
        stopMenuPreview();
        controller.reset();
        if (waitingClient != null) {
            waitingClient.close();
            waitingClient = null;
        }
        cancelAttempt();
        closeNearby();
        playing = false;
        selected = null;
        selectedNetwork = null;
        selectedNearby = false;
        clearCode();
        pairScreen.setVisibility(View.GONE);
        home.setVisibility(View.VISIBLE);
        activity.protectPairingForm(false);
        if (message == 0) {
            homeStatus.setVisibility(View.GONE);
        } else {
            setStatus(homeStatus, message, error);
            homeStatus.setVisibility(View.VISIBLE);
        }
        if (foreground) {
            discover();
        }
    }

    void hideForPlayback(LanClient media) {
        stopMenuPreview();
        playing = true;
        waitingClient = null;
        if (attempt != null) {
            attempt.detachClientForPlayback();
            attempt = null;
        }
        request = null;
        home.setVisibility(View.GONE);
        pairScreen.setVisibility(View.GONE);
        clearCode();
        activity.protectPairingForm(false);
        controller.openInline(action -> sendPcAction(media, action));
    }

    void returnToControls(LanClient media) {
        stopMenuPreview();
        controller.reset();
        playing = false;
        waitingClient = media;
        home.setVisibility(View.GONE);
        pairScreen.setVisibility(View.VISIBLE);
        pcControls.setVisibility(View.VISIBLE);
        pcControls.setOnClickListener(view -> showPcControls(media));
        startPlayback.setVisibility(View.VISIBLE);
        startPlayback.setEnabled(true);
        startPlayback.setOnClickListener(view -> network.execute(() -> {
            try {
                media.requestPlaybackStart();
            } catch (IOException error) {
                activity.runOnUiThread(() -> {
                    if (waitingClient == media) setStatus(pairStatus, R.string.pair_playback_permission, true);
                });
            }
        }));
        setStatus(pairStatus, R.string.pair_wait_media, false);
        controller.openOverlay(action -> sendPcAction(media, action));
        startMenuPreview(media);
        checkWaitingClient(media, 0);
    }

    private void checkWaitingClient(LanClient media, long delayMs) {
        if (!foreground || waitingClient != media) return;
        int generation = ++mediaRequestGeneration;
        request = network.schedule(() -> {
            LanSession session = null;
            boolean ended = false;
            try {
                session = media.checkSession();
            } catch (LanClient.HttpException error) {
                ended = error.status != 409;
            } catch (IOException error) {
                ended = true;
            }
            LanSession available = session;
            boolean disconnected = ended;
            activity.runOnUiThread(() -> {
                if (waitingClient != media || generation != mediaRequestGeneration || !foreground) return;
                if (available != null) activity.openPlayback(media, available);
                else if (disconnected) showHome(R.string.session_ended, true);
                else checkWaitingClient(media, 1500);
            });
        }, delayMs, TimeUnit.MILLISECONDS);
    }

    private void discover() {
        discover(null);
    }

    private void discover(VpnDiscoverySubnet subnet) {
        discover(subnet, false);
    }

    private void discover(VpnDiscoverySubnet subnet, boolean backgroundRefresh) {
        stopDiscovery();
        boolean vpn = subnet != null;
        showingVpnResults = vpn;
        if (!backgroundRefresh) {
            nearbyButton.setVisibility(View.GONE);
        }
        ConnectivityManager connectivity = activity.getSystemService(ConnectivityManager.class);
        if (connectivity == null) {
            setStatus(discoveryStatus, R.string.discovery_empty, false);
            discoveryStatus.setVisibility(View.VISIBLE);
            return;
        }
        LanDiscovery current = new LanDiscovery();
        scan = current;
        if (!backgroundRefresh) {
            refresh.setEnabled(false);
            vpnSearch.setEnabled(false);
            setStatus(discoveryStatus, vpn ? R.string.vpn_discovery_searching : R.string.discovery_searching, false);
            discoveryStatus.setVisibility(View.VISIBLE);
        }
        discoveryExecutor.execute(() -> {
            List<LanDiscovery.Pc> result;
            try {
                result = vpn ? current.findVpn(connectivity, subnet) : current.find(connectivity);
            } catch (SecurityException e) {
                result = Collections.emptyList();
            }
            List<LanDiscovery.Pc> found = result;
            activity.runOnUiThread(() -> {
                if (scan != current || !foreground) {
                    return;
                }
                scan = null;
                List<LanDiscovery.Pc> previous = discovered;
                if (vpn) {
                    Map<String, LanDiscovery.Pc> combined = new LinkedHashMap<>();
                    for (LanDiscovery.Pc pc : ordinaryDiscovered) {
                        combined.put(pc.endpoint.address(), pc);
                    }
                    for (LanDiscovery.Pc pc : found) {
                        combined.putIfAbsent(pc.endpoint.address(), pc);
                    }
                    discovered = new ArrayList<>(combined.values());
                } else {
                    ordinaryDiscovered = found;
                    discovered = found;
                }
                refresh.setEnabled(true);
                vpnSearch.setEnabled(true);
                if (!backgroundRefresh || !sameDiscoveredPcs(previous, discovered)) {
                    renderList();
                }
                setStatus(discoveryStatus, vpn ? R.string.vpn_discovery_empty : R.string.discovery_empty, false);
                discoveryStatus.setVisibility(found.isEmpty() ? View.VISIBLE : View.GONE);
                nearbyButton.setVisibility(discovered.isEmpty() && NearbyConnection.supported(activity)
                        ? View.VISIBLE : View.GONE);
                if (!vpn) {
                    home.postDelayed(refreshDiscovery, DISCOVERY_REFRESH_MS);
                }
            });
        });
    }

    private void refreshOrdinaryDiscovery() {
        if (!foreground || showingVpnResults || home.getVisibility() != View.VISIBLE) {
            return;
        }
        if (nearby != null || vpnDialog != null || scan != null) {
            home.postDelayed(refreshDiscovery, DISCOVERY_REFRESH_MS);
            return;
        }
        discover(null, true);
    }

    private static boolean sameDiscoveredPcs(List<LanDiscovery.Pc> before, List<LanDiscovery.Pc> after) {
        if (before.size() != after.size()) {
            return false;
        }
        for (int i = 0; i < before.size(); i++) {
            LanDiscovery.Pc oldPc = before.get(i);
            LanDiscovery.Pc newPc = after.get(i);
            if (!oldPc.endpoint.address().equals(newPc.endpoint.address())
                    || !oldPc.name.equals(newPc.name) || !Objects.equals(oldPc.network, newPc.network)) {
                return false;
            }
        }
        return true;
    }

    private void showVpnSearch() {
        if (!foreground || vpnDialog != null || nearby != null) {
            return;
        }
        ConnectivityManager connectivity = activity.getSystemService(ConnectivityManager.class);
        boolean activeVpn = false;
        try {
            activeVpn = connectivity != null && LanDiscovery.hasActiveVpn(connectivity);
        } catch (SecurityException ignored) {
            // Android can restrict network access for this application.
        }
        if (!activeVpn) {
            setStatus(discoveryStatus, R.string.vpn_discovery_unavailable, true);
            discoveryStatus.setVisibility(View.VISIBLE);
            return;
        }
        View content = activity.getLayoutInflater().inflate(R.layout.vpn_discovery_dialog, null);
        EditText subnet = content.findViewById(R.id.vpn_subnet);
        AlertDialog dialog = new AlertDialog.Builder(activity).setTitle(R.string.vpn_discovery_action).setView(content)
                .setPositiveButton(R.string.vpn_discovery_search, null)
                .setNegativeButton(android.R.string.cancel, null).create();
        vpnDialog = dialog;
        dialog.setOnDismissListener(ignored -> {
            if (vpnDialog == dialog) vpnDialog = null;
        });
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            try {
                VpnDiscoverySubnet range = VpnDiscoverySubnet.parse(subnet.getText().toString());
                dialog.dismiss();
                if (foreground) {
                    discover(range);
                }
            } catch (IllegalArgumentException e) {
                subnet.setError(activity.getString(R.string.vpn_discovery_invalid));
            }
        });
    }

    private void closeVpnSearch() {
        if (vpnDialog != null) {
            vpnDialog.dismiss();
            vpnDialog = null;
        }
    }

    private void showNearby() {
        if (!foreground || nearby != null || !discovered.isEmpty()
                || !NearbyConnection.supported(activity)) {
            return;
        }
        ConnectivityManager connectivity = activity.getSystemService(ConnectivityManager.class);
        if (connectivity == null) {
            return;
        }
        if (scan != null) {
            stopDiscovery();
            home.postDelayed(refreshDiscovery, DISCOVERY_REFRESH_MS);
        }
        nearby = new NearbyConnection(activity, connectivity, discoveryExecutor, new NearbyConnection.Listener() {
            @Override
            public void selected(NearbyConnection connection, LanDiscovery.Pc pc, Network network) {
                if (nearby == connection && foreground) {
                    select(pc, network, true);
                }
            }

            @Override
            public void cancelled(NearbyConnection connection) {
                if (nearby == connection) {
                    closeNearby();
                }
            }

            @Override
            public void lost(NearbyConnection connection) {
                if (nearby == connection) {
                    if (playing) {
                        activity.onNearbyNetworkLost();
                    } else {
                        showHome(R.string.nearby_wifi_lost, true);
                    }
                }
            }
        });
        nearby.show();
    }

    private void closeNearby() {
        if (nearby != null) {
            nearby.close();
            nearby = null;
        }
    }

    private void stopDiscovery() {
        home.removeCallbacks(refreshDiscovery);
        if (scan != null) {
            scan.close();
            scan = null;
        }
        refresh.setEnabled(true);
        vpnSearch.setEnabled(true);
    }

    private void renderList() {
        pcList.removeAllViews();
        List<RememberedPcStore.Entry> saved = remembered.entries();
        saved.sort(Comparator.comparing(entry -> entry.name));
        Map<String, LanDiscovery.Pc> unremembered = new LinkedHashMap<>();
        for (LanDiscovery.Pc pc : discovered) {
            unremembered.put(pc.endpoint.address(), pc);
        }
        for (RememberedPcStore.Entry entry : saved) {
            LanDiscovery.Pc response = unremembered.remove(entry.endpoint.address());
            addPc(new LanDiscovery.Pc(entry.endpoint, entry.name, response == null ? null : response.network), true);
        }
        for (LanDiscovery.Pc pc : unremembered.values()) {
            addPc(pc, false);
        }
    }

    private void addPc(LanDiscovery.Pc pc, boolean saved) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setOrientation(LinearLayout.HORIZONTAL);
        pcList.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout open = new LinearLayout(activity);
        open.setId(R.id.pc_item);
        open.setOrientation(LinearLayout.VERTICAL);
        open.setMinimumHeight(dp(56));
        open.setGravity(Gravity.CENTER_VERTICAL);
        open.setPadding(dp(8), dp(6), dp(8), dp(6));
        open.setFocusable(true);
        TypedValue touchBackground = new TypedValue();
        if (activity.getTheme().resolveAttribute(android.R.attr.selectableItemBackground, touchBackground, true)) {
            open.setBackgroundResource(touchBackground.resourceId);
        }
        open.setOnClickListener(view -> select(pc));
        row.addView(open, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        String title = saved ? activity.getString(R.string.pc_remembered, pc.name) : pc.name;
        TextView name = new TextView(activity);
        name.setId(R.id.pc_name);
        name.setText(title);
        name.setTextColor(activity.getColor(R.color.luma_text));
        name.setTextSize(16);
        name.setTypeface(name.getTypeface(), Typeface.BOLD);
        name.setMaxLines(2);
        name.setEllipsize(TextUtils.TruncateAt.END);
        open.addView(name);

        TextView endpoint = new TextView(activity);
        endpoint.setId(R.id.pc_address);
        endpoint.setText(pc.endpoint.address());
        endpoint.setTextColor(activity.getColor(R.color.luma_muted));
        endpoint.setTextSize(13);
        open.addView(endpoint);
        open.setContentDescription(title + " " + pc.endpoint.address());

        if (saved) {
            Button forget = new Button(activity, null, android.R.attr.borderlessButtonStyle);
            forget.setId(R.id.forget_pc_button);
            forget.setText(R.string.pc_forget);
            forget.setTextColor(activity.getColor(R.color.luma_accent));
            forget.setAllCaps(false);
            forget.setOnClickListener(view -> {
                remembered.forget(pc.endpoint);
                renderList();
                setStatus(homeStatus, R.string.pc_forget_done, false);
                homeStatus.setVisibility(View.VISIBLE);
            });
            row.addView(forget);
        }
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }

    private void selectAddress() {
        LanEndpoint endpoint;
        try {
            endpoint = LanEndpoint.parse(address.getText().toString());
        } catch (IllegalArgumentException e) {
            setStatus(homeStatus, R.string.invalid_address, true);
            homeStatus.setVisibility(View.VISIBLE);
            address.requestFocus();
            return;
        }
        select(new LanDiscovery.Pc(endpoint, activity.getString(R.string.pair_pc_default)));
    }

    private void select(LanDiscovery.Pc pc) {
        ConnectivityManager connectivity = activity.getSystemService(ConnectivityManager.class);
        Network route = null;
        if (connectivity != null) {
            try {
                if (pc.network == null) {
                    route = LanDiscovery.routeFor(connectivity, pc.endpoint);
                } else if (LanDiscovery.canUseDiscoveredRoute(connectivity, pc)) {
                    route = pc.network;
                }
            } catch (SecurityException ignored) {
                // The app cannot select a route when Android denies access to the network.
            }
        }
        if (route == null) {
            setStatus(homeStatus, R.string.connection_error, true);
            homeStatus.setVisibility(View.VISIBLE);
            return;
        }
        select(pc, route, false);
    }

    private void select(LanDiscovery.Pc pc, Network route, boolean direct) {
        stopDiscovery();
        InputMethodManager keyboard = activity.getSystemService(InputMethodManager.class);
        if (keyboard != null) {
            keyboard.hideSoftInputFromWindow(address.getWindowToken(), 0);
        }
        selected = pc;
        selectedNetwork = route;
        selectedNearby = direct;
        home.setVisibility(View.GONE);
        pairScreen.setVisibility(View.VISIBLE);
        pairScreen.scrollTo(0, 0);
        pairName.setText(pc.name);
        pairAddress.setText(pc.endpoint.address());
        options.check(R.id.pair_remember_always);
        // A direct network's private addresses can repeat across different PCs.
        RememberedPcStore.Entry record = direct ? null : remembered.find(pc.endpoint);
        if (record == null) {
            showOptions(direct ? R.string.nearby_pair : R.string.pair_choose, false);
        } else {
            pairName.setText(record.name);
            beginLogin(record);
        }
    }

    private PairingConnection begin(int message) {
        cancelAttempt();
        clearCode();
        options.setVisibility(View.GONE);
        startPairing.setVisibility(View.GONE);
        setStatus(pairStatus, message, false);
        activity.protectPairingForm(true);
        attempt = new PairingConnection(selected.endpoint, selectedNetwork);
        return attempt;
    }

    private void showOptions(int message, boolean error) {
        clearCode();
        options.setVisibility(selectedNearby ? View.GONE : View.VISIBLE);
        startPairing.setVisibility(View.VISIBLE);
        setStatus(pairStatus, message, error);
        activity.protectPairingForm(false);
    }

    private void startApproval() {
        if (selected == null || attempt != null) {
            return;
        }
        boolean save = !selectedNearby && options.getCheckedRadioButtonId() == R.id.pair_remember_device;
        LanEndpoint endpoint = selected.endpoint;
        if (!save && !selectedNearby) {
            remembered.forget(endpoint);
        }
        PairingConnection current = begin(R.string.connecting);
        String device = LanPairing.validName(Build.MODEL) ? Build.MODEL : "Android";
        request = network.schedule(() -> {
            try {
                PairingConnection.Pending pending = current.requestApproval(device, save);
                activity.runOnUiThread(() -> {
                    if (active(current)) {
                        showMatchingCode(current, pending);
                    }
                });
            } catch (IOException | JSONException | GeneralSecurityException error) {
                postFailure(current, connectionMessage(error, false));
            }
        }, 0, TimeUnit.MILLISECONDS);
    }

    private void showMatchingCode(PairingConnection current, PairingConnection.Pending pending) {
        awaitingConfirmation = pending;
        pairName.setText(pending.identity.name);
        pairCode.setText(pending.code);
        pairCode.setVisibility(View.VISIBLE);
        confirmCode.setVisibility(View.VISIBLE);
        setStatus(pairStatus, R.string.pair_compare, false);
        long remainingMs = Math.max(0, pending.deadline - SystemClock.elapsedRealtime());
        request = network.schedule(() -> postFailure(current, R.string.pairing_rejected), remainingMs, TimeUnit.MILLISECONDS);
    }

    private void confirmMatchingCode() {
        PairingConnection current = attempt;
        PairingConnection.Pending pending = awaitingConfirmation;
        if (current == null || pending == null || !active(current)) {
            return;
        }
        if (SystemClock.elapsedRealtime() >= pending.deadline) {
            cancelAttempt();
            showOptions(R.string.pairing_rejected, true);
            return;
        }
        request.cancel(false);
        awaitingConfirmation = null;
        confirmCode.setVisibility(View.GONE);
        setStatus(pairStatus, R.string.pair_approve, false);
        poll(current, selected.endpoint, pending);
    }

    private void beginLogin(RememberedPcStore.Entry record) {
        PairingConnection current = begin(R.string.remember_login);
        request = network.schedule(() -> {
            try {
                String token = remembered.token(record);
                LanClient media = current.login(record, token);
                activity.runOnUiThread(() -> waitForMedia(current, media, false));
            } catch (GeneralSecurityException error) {
                postFailure(current, R.string.remember_rejected);
            } catch (IOException | JSONException error) {
                postFailure(current, connectionMessage(error, true));
            }
        }, 0, TimeUnit.MILLISECONDS);
    }

    private void poll(PairingConnection current, LanEndpoint endpoint, PairingConnection.Pending pending) {
        request = network.schedule(() -> {
            try {
                PairingConnection.Approval approval = current.poll(pending);
                if (approval.state == PairingConnection.State.PENDING) {
                    activity.runOnUiThread(() -> {
                        if (active(current)) {
                            poll(current, endpoint, pending);
                        }
                    });
                } else if (approval.state == PairingConnection.State.DENIED) {
                    postFailure(current, R.string.pairing_rejected);
                } else {
                    LanClient media = current.playback(pending.identity.pin, approval.token);
                    RememberedPcStore.PreparedEntry encrypted = prepareApprovedPc(endpoint, pending, approval);
                    activity.runOnUiThread(() -> {
                        if (active(current)) {
                            boolean stored = encrypted != null && remembered.save(encrypted);
                            waitForMedia(current, media, pending.remember && !stored);
                        }
                    });
                }
            } catch (IOException | JSONException | GeneralSecurityException error) {
                postFailure(current, connectionMessage(error, false));
            }
        }, 1, TimeUnit.SECONDS);
    }

    private RememberedPcStore.PreparedEntry prepareApprovedPc(LanEndpoint endpoint, PairingConnection.Pending pending,
                                                               PairingConnection.Approval approval) {
        if (approval.rememberToken == null) {
            return null;
        }
        try {
            return remembered.prepare(endpoint, pending.identity.name, pending.identity.pin, pending.deviceId,
                    approval.rememberToken);
        } catch (GeneralSecurityException | IOException | JSONException e) {
            return null;
        }
    }

    private void waitForMedia(PairingConnection current, LanClient media, boolean saveFailed) {
        if (!active(current)) {
            return;
        }
        clearCode();
        pcControls.setVisibility(View.VISIBLE);
        pcControls.setOnClickListener(view -> showPcControls(media));
        activity.protectPairingForm(false);
        setStatus(pairStatus, R.string.connecting, false);
        startPlayback.setOnClickListener(view -> {
            if (!active(current)) {
                return;
            }
            if (request != null) {
                request.cancel(false);
            }
            startPlayback.setEnabled(false);
            int generation = ++mediaRequestGeneration;
            request = network.schedule(() -> {
                int message;
                try {
                    media.requestPlaybackStart();
                    message = R.string.pair_playback_pending;
                } catch (LanClient.HttpException error) {
                    message = error.status == 403 || error.status == 404
                            ? R.string.pair_playback_permission : connectionMessage(error, false);
                } catch (IOException error) {
                    message = connectionMessage(error, false);
                }
                int status = message;
                activity.runOnUiThread(() -> {
                    if (active(current) && mediaRequestGeneration == generation) {
                        setStatus(pairStatus, status, status != R.string.pair_playback_pending);
                        requestSession(current, media, saveFailed, 1500);
                    }
                });
            }, 0, TimeUnit.MILLISECONDS);
        });
        requestSession(current, media, saveFailed, 0);
    }

    void showPcControls(LanClient media) {
        controller.toggle(action -> sendPcAction(media, action));
        if (!playing && controller.isOverlayVisible()) startMenuPreview(media);
        else stopMenuPreview();
    }

    private void startMenuPreview(LanClient media) {
        stopMenuPreview();
        if (!activity.hasSecureDisplay()) return;
        menuClient = media;
        pollMenuPreview(media, menuGeneration, 0);
    }

    private void pollMenuPreview(LanClient media, int generation, long delayMs) {
        menuRequest = network.schedule(() -> {
            android.graphics.Bitmap frame;
            try {
                frame = DiscMenuView.decode(media.discMenuFrame());
            } catch (IOException e) {
                frame = null;
            }
            var result = frame;
            activity.runOnUiThread(() -> {
                if (menuClient != media || generation != menuGeneration || !foreground || playing ||
                        !controller.isOverlayVisible() || !activity.hasSecureDisplay()) {
                    if (generation == menuGeneration) stopMenuPreview();
                    return;
                }
                controller.updateMenu(result);
                pollMenuPreview(media, generation, 650);
            });
        }, delayMs, TimeUnit.MILLISECONDS);
    }

    private void stopMenuPreview() {
        ++menuGeneration;
        menuClient = null;
        if (menuRequest != null) menuRequest.cancel(false);
        menuRequest = null;
        controller.updateMenu(null);
    }

    private void sendPcAction(LanClient media, String action) {
        if ("file-picker".equals(action)) {
            chooseRemoteFile(media);
            return;
        }
        if (activity.applyStreamingControl(media, action)) return;
        if (controlInFlight) return;
        controlInFlight = true;
        network.execute(() -> {
            int message = 0;
            try {
                media.controlPlayer(action);
            } catch (LanClient.HttpException error) {
                message = error.status == 403 || error.status == 404 ? R.string.pc_control_permission : R.string.pc_control_failed;
            } catch (IOException error) {
                message = R.string.pc_control_failed;
            }
            int result = message;
            activity.runOnUiThread(() -> {
                controlInFlight = false;
                if (foreground && result != 0) Toast.makeText(activity, result, Toast.LENGTH_SHORT).show();
            });
        });
    }

    private void chooseRemoteFile(LanClient media) {
        if (controlInFlight) return;
        controlInFlight = true;
        network.execute(() -> {
            List<LanClient.RemoteFile> files;
            try {
                files = media.remoteFiles();
            } catch (IOException error) {
                files = null;
            }
            var entries = files;
            activity.runOnUiThread(() -> {
                controlInFlight = false;
                if (!foreground || activity.isFinishing()) return;
                if (entries == null || entries.isEmpty()) {
                    Toast.makeText(activity, entries == null ? R.string.pc_control_permission : R.string.pc_files_empty,
                            Toast.LENGTH_LONG).show();
                    return;
                }
                var labels = entries.stream().map(LanClient.RemoteFile::name).toArray(String[]::new);
                new AlertDialog.Builder(activity).setTitle(R.string.pc_files_title)
                        .setItems(labels, (dialog, index) -> openRemoteFile(media, entries.get(index).id()))
                        .setNegativeButton(android.R.string.cancel, null).show();
            });
        });
    }

    private void openRemoteFile(LanClient media, String id) {
        network.execute(() -> {
            try {
                media.openRemoteFile(id);
            } catch (IOException error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, R.string.pc_control_failed, Toast.LENGTH_SHORT).show());
            }
        });
    }

    boolean closePcControls() {
        if (!controller.dismissOverlay()) return false;
        stopMenuPreview();
        return true;
    }

    void updatePcControllerLayout(boolean videoOnly, boolean pip) {
        controller.setVideoOnly(videoOnly, pip);
    }

    void updatePcControllerPlayback(boolean playing) {
        controller.setPlaying(playing);
    }

    private void requestSession(PairingConnection current, LanClient media, boolean saveFailed, long delayMs) {
        int generation = ++mediaRequestGeneration;
        request = network.schedule(() -> {
            try {
                LanSession session = media.fetchSession();
                activity.runOnUiThread(() -> {
                    if (active(current) && mediaRequestGeneration == generation) {
                        if (saveFailed) {
                            Toast.makeText(activity, R.string.remember_not_saved, Toast.LENGTH_LONG).show();
                        }
                        activity.openPlayback(media, session);
                    }
                });
            } catch (LanClient.HttpException error) {
                if (error.status != 409) {
                    postMediaFailure(current, generation, error);
                    return;
                }
                activity.runOnUiThread(() -> {
                    if (active(current) && mediaRequestGeneration == generation) {
                        if (startPlayback.getVisibility() != View.VISIBLE) {
                            setStatus(pairStatus, saveFailed ? R.string.pair_wait_media_unsaved : R.string.pair_wait_media, saveFailed);
                        }
                        startPlayback.setVisibility(View.VISIBLE);
                        startPlayback.setEnabled(true);
                        requestSession(current, media, saveFailed, 2000);
                    }
                });
            } catch (IOException error) {
                postMediaFailure(current, generation, error);
            }
        }, delayMs, TimeUnit.MILLISECONDS);
    }

    private void postMediaFailure(PairingConnection current, int generation, IOException error) {
        activity.runOnUiThread(() -> {
            if (active(current) && mediaRequestGeneration == generation) {
                cancelAttempt();
                showOptions(connectionMessage(error, false), true);
            }
        });
    }

    private boolean active(PairingConnection current) {
        return foreground && attempt == current && !activity.isFinishing();
    }

    private void postFailure(PairingConnection current, int message) {
        activity.runOnUiThread(() -> {
            if (active(current)) {
                cancelAttempt();
                showOptions(message, true);
            }
        });
    }

    private void cancelAttempt() {
        pcControls.setVisibility(View.GONE);
        pcControls.setOnClickListener(null);
        startPlayback.setVisibility(View.GONE);
        startPlayback.setOnClickListener(null);
        if (request != null) {
            request.cancel(false);
            request = null;
        }
        if (attempt != null) {
            attempt.close();
            attempt = null;
        }
    }

    private void clearCode() {
        awaitingConfirmation = null;
        pairCode.setText(null);
        pairCode.setVisibility(View.GONE);
        confirmCode.setVisibility(View.GONE);
    }

    private void setStatus(TextView view, int message, boolean error) {
        CharSequence text = activity.getText(message);
        if (!TextUtils.equals(view.getText(), text)) {
            view.setText(text);
        }
        view.setTextColor(activity.getColor(error ? R.color.luma_error : R.color.luma_muted));
    }

    private static int connectionMessage(Exception error, boolean login) {
        if (error instanceof LanClient.HttpException http && (http.status == 401 || http.status == 403)) {
            return login ? R.string.remember_rejected : R.string.pairing_rejected;
        }
        boolean tls = false;
        for (Throwable cause = error; cause != null; cause = cause.getCause()) {
            if (login && cause instanceof LanClient.PinMismatch) {
                return R.string.remember_changed;
            }
            if (cause instanceof SSLException) {
                tls = true;
            } else if (!tls && (cause instanceof SocketException || cause instanceof InterruptedIOException)) {
                return R.string.connection_error;
            }
        }
        return tls ? R.string.tls_rejected : R.string.session_error;
    }
}
