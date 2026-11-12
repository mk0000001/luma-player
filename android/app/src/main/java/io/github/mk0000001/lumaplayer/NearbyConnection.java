package io.github.mk0000001.lumaplayer;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Build;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;

final class NearbyConnection implements AutoCloseable {
    private static final int BLUETOOTH_PERMISSION = 0x4c51;
    private static final String[] LOCATION = {Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.ACCESS_FINE_LOCATION};

    interface Listener {
        void selected(NearbyConnection connection, LanDiscovery.Pc pc, Network network);
        void cancelled(NearbyConnection connection);
        void lost(NearbyConnection connection);
    }

    private final Activity activity;
    private final ConnectivityManager connectivity;
    private final ExecutorService discoveryExecutor;
    private final Listener listener;
    private final AlertDialog dialog;
    private final TextView status;
    private final LinearLayout choices;
    private final View credentials;
    private final EditText ssid;
    private final EditText password;
    private final Set<String> advertised = new HashSet<>();
    private NearbyPcScanner bluetooth;
    private NearbyWifiRequest wifi;
    private LanDiscovery discovery;
    private Runnable deferred;
    private int permissionRequest;
    private boolean foreground = true;
    private boolean handedOff;
    private boolean closed;

    static boolean supported(Activity activity) {
        return Build.VERSION.SDK_INT >= 29 && activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_WIFI);
    }

    NearbyConnection(Activity activity, ConnectivityManager connectivity, ExecutorService discoveryExecutor,
                     Listener listener) {
        this.activity = activity;
        this.connectivity = connectivity;
        this.discoveryExecutor = discoveryExecutor;
        this.listener = listener;
        View content = activity.getLayoutInflater().inflate(R.layout.nearby_connection_dialog, null);
        status = content.findViewById(R.id.nearby_status);
        choices = content.findViewById(R.id.nearby_choices);
        credentials = content.findViewById(R.id.nearby_credentials);
        ssid = content.findViewById(R.id.nearby_ssid);
        password = content.findViewById(R.id.nearby_password);
        dialog = new AlertDialog.Builder(activity).setTitle(R.string.nearby_action).setView(content)
                .setPositiveButton(R.string.nearby_join, null).setNegativeButton(android.R.string.cancel, null).create();
        dialog.setOnDismissListener(ignored -> listener.cancelled(this));
    }

    void show() {
        dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> join());
        password.setOnEditorActionListener((view, action, event) -> {
            if (action == EditorInfo.IME_ACTION_GO) {
                join();
                return true;
            }
            return false;
        });
        if (!activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            status.setText(R.string.nearby_manual);
        } else if (hasPermissions(bluetoothPermissions())) {
            scanBluetooth();
        } else {
            requestPermissions(bluetoothPermissions(), BLUETOOTH_PERMISSION);
        }
    }

    void setForeground(boolean visible) {
        foreground = visible;
        if (foreground && deferred != null) {
            Runnable action = deferred;
            deferred = null;
            action.run();
        }
    }

    boolean awaitingSystemUi() {
        return !closed && (permissionRequest != 0 || (wifi != null && wifi.awaitingNetwork()));
    }

    void onRequestPermissionsResult(int requestCode) {
        if (closed || permissionRequest != requestCode || requestCode != BLUETOOTH_PERMISSION) {
            return;
        }
        permissionRequest = 0;
        whenForeground(() -> {
            if (hasPermissions(bluetoothPermissions())) {
                scanBluetooth();
            } else {
                status.setText(R.string.nearby_manual);
            }
        });
    }

    private void whenForeground(Runnable action) {
        if (foreground) {
            action.run();
        } else {
            deferred = action;
        }
    }

    private void scanBluetooth() {
        if (Build.VERSION.SDK_INT < 31 && !locationEnabled()) {
            status.setText(R.string.nearby_manual);
            return;
        }
        status.setText(R.string.nearby_scan);
        bluetooth = new NearbyPcScanner(activity, this::showSsid, this::bluetoothFinished);
        if (!bluetooth.start()) {
            bluetoothFinished();
        }
    }

    private void showSsid(String found) {
        if (advertised.add(found)) {
            addChoice(found, () -> {
                ssid.setText(found);
                password.getText().clear();
                password.requestFocus();
            });
        }
    }

    private void bluetoothFinished() {
        stopBluetooth();
        status.setText(advertised.isEmpty() ? R.string.nearby_manual : R.string.nearby_select_pc);
    }

    private void addChoice(CharSequence title, Runnable select) {
        Button button = new Button(activity, null, 0, R.style.LumaTextButton);
        button.setText(title);
        button.setOnClickListener(view -> select.run());
        choices.addView(button);
    }

    private void join() {
        if (closed || !foreground || wifi != null || permissionRequest != 0) {
            return;
        }
        String name = ssid.getText().toString().trim();
        if (!NearbyCredentials.validSsid(name)) {
            status.setText(R.string.nearby_invalid_ssid);
            ssid.requestFocus();
            return;
        }
        if (!NearbyCredentials.validPassphrase(password.getText())) {
            status.setText(R.string.nearby_invalid_password);
            password.requestFocus();
            return;
        }
        stopBluetooth();
        String passphrase = password.getText().toString();
        password.getText().clear();
        InputMethodManager keyboard = activity.getSystemService(InputMethodManager.class);
        if (keyboard != null) {
            keyboard.hideSoftInputFromWindow(password.getWindowToken(), 0);
        }
        choices.removeAllViews();
        credentials.setVisibility(View.GONE);
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(false);
        status.setText(R.string.nearby_joining);
        wifi = new NearbyWifiRequest(connectivity, new NearbyWifiRequest.Listener() {
            @Override
            public void available(Network network) {
                whenForeground(() -> discover(network));
            }

            @Override
            public void unavailable() {
                retry(R.string.nearby_wifi_unavailable);
            }

            @Override
            public void lost() {
                if (handedOff) {
                    listener.lost(NearbyConnection.this);
                } else {
                    retry(R.string.nearby_wifi_lost);
                }
            }
        });
        if (!wifi.start(name, passphrase)) {
            retry(R.string.nearby_wifi_unavailable);
        }
    }

    private void discover(Network network) {
        if (closed || wifi == null) {
            return;
        }
        status.setText(R.string.nearby_find_pc);
        LanDiscovery current = new LanDiscovery();
        discovery = current;
        discoveryExecutor.execute(() -> {
            List<LanDiscovery.Pc> result;
            try {
                result = current.find(connectivity, network);
            } catch (SecurityException e) {
                result = Collections.emptyList();
            }
            List<LanDiscovery.Pc> found = result;
            activity.runOnUiThread(() -> {
                if (discovery == current) {
                    discovery = null;
                    whenForeground(() -> showPcs(found, network));
                }
            });
        });
    }

    private void showPcs(List<LanDiscovery.Pc> found, Network network) {
        if (found.isEmpty()) {
            retry(R.string.nearby_pc_unavailable);
        } else if (found.size() == 1) {
            choose(found.get(0), network);
        } else {
            status.setText(R.string.nearby_select_pc);
            for (LanDiscovery.Pc pc : found) {
                addChoice(activity.getString(R.string.nearby_pc_choice, pc.name, pc.endpoint.address()),
                        () -> choose(pc, network));
            }
        }
    }

    private void choose(LanDiscovery.Pc pc, Network network) {
        if (!closed && !handedOff && wifi != null) {
            handedOff = true;
            dialog.setOnDismissListener(null);
            dialog.dismiss();
            listener.selected(this, pc, network);
        }
    }

    private void retry(int message) {
        deferred = null;
        closeNetwork();
        choices.removeAllViews();
        credentials.setVisibility(View.VISIBLE);
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(true);
        status.setText(message);
    }

    private String[] bluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= 31) {
            return new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
        }
        return LOCATION;
    }

    private boolean hasPermissions(String[] permissions) {
        for (String permission : permissions) {
            if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    private boolean locationEnabled() {
        LocationManager location = activity.getSystemService(LocationManager.class);
        return location != null && location.isLocationEnabled();
    }

    private void requestPermissions(String[] permissions, int requestCode) {
        permissionRequest = requestCode;
        activity.requestPermissions(permissions, requestCode);
    }

    private void stopBluetooth() {
        if (bluetooth != null) {
            bluetooth.close();
            bluetooth = null;
        }
    }

    private void closeNetwork() {
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        if (wifi != null) {
            wifi.close();
            wifi = null;
        }
    }

    @Override
    public void close() {
        closed = true;
        deferred = null;
        stopBluetooth();
        closeNetwork();
        password.getText().clear();
        dialog.setOnDismissListener(null);
        dialog.dismiss();
    }
}
