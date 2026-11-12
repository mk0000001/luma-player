package io.github.mk0000001.lumaplayer;

import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

final class NearbyWifiRequest implements AutoCloseable {
    private static final int TIMEOUT_MS = 30_000;

    interface Listener {
        void available(Network network);
        void unavailable();
        void lost();
    }

    private final ConnectivityManager connectivity;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable timeout = this::unavailable;
    private boolean requested;
    private boolean closed;
    private Network selected;

    NearbyWifiRequest(ConnectivityManager connectivity, Listener listener) {
        this.connectivity = connectivity;
        this.listener = listener;
    }

    boolean start(String ssid, String passphrase) {
        if (Build.VERSION.SDK_INT < 29 || closed || requested) {
            return false;
        }
        try {
            WifiNetworkSpecifier specifier = new WifiNetworkSpecifier.Builder()
                    .setSsid(ssid).setWpa2Passphrase(passphrase).build();
            NetworkRequest request = new NetworkRequest.Builder()
                    .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                    .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .setNetworkSpecifier(specifier).build();
            connectivity.requestNetwork(request, callback, handler, TIMEOUT_MS);
            requested = true;
            // The system timeout can stop at onAvailable before the interface has IPv4.
            handler.postDelayed(timeout, TIMEOUT_MS);
            return true;
        } catch (RuntimeException e) {
            close();
            return false;
        }
    }

    boolean awaitingNetwork() {
        return requested && !closed && selected == null;
    }

    private final ConnectivityManager.NetworkCallback callback = new ConnectivityManager.NetworkCallback() {
        @Override
        public void onLinkPropertiesChanged(Network network, LinkProperties properties) {
            if (closed || (selected != null && !selected.equals(network))) {
                return;
            }
            boolean privateIpv4 = false;
            for (LinkAddress address : properties.getLinkAddresses()) {
                if (LanEndpoint.isPrivate(address.getAddress()) && address.getPrefixLength() >= 1
                        && address.getPrefixLength() <= 30) {
                    privateIpv4 = true;
                    break;
                }
            }
            if (selected == null && privateIpv4) {
                selected = network;
                handler.removeCallbacks(timeout);
                listener.available(network);
            } else if (selected != null && !privateIpv4) {
                onLost(network);
            }
        }

        @Override
        public void onUnavailable() {
            unavailable();
        }

        @Override
        public void onLost(Network network) {
            if (!closed && network.equals(selected)) {
                close();
                listener.lost();
            }
        }
    };

    private void unavailable() {
        if (!closed && selected == null) {
            close();
            listener.unavailable();
        }
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        selected = null;
        handler.removeCallbacks(timeout);
        if (requested) {
            try {
                connectivity.unregisterNetworkCallback(callback);
            } catch (IllegalArgumentException ignored) {
                // Android can release a timed-out request before onUnavailable.
            }
        }
    }
}
