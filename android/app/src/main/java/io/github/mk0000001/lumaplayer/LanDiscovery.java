package io.github.mk0000001.lumaplayer;

import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.RouteInfo;
import android.os.Build;
import android.os.SystemClock;
import java.io.Closeable;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;
import org.json.JSONException;
import org.json.JSONObject;

final class LanDiscovery implements Closeable {
    private static final int MAX_REPLY = 1024;
    private static final int MAX_PCS = 32;
    private static final long SCAN_MS = 2400;
    private static final long PROBE_INTERVAL_MS = 600;
    private final ConcurrentLinkedQueue<DatagramSocket> sockets = new ConcurrentLinkedQueue<>();
    private volatile boolean closed;

    @SuppressWarnings("deprecation") // One foreground scan snapshots the available local network interfaces.
    List<Pc> find(ConnectivityManager manager) {
        return find(manager, Arrays.asList(manager.getAllNetworks()));
    }

    List<Pc> find(ConnectivityManager manager, Network selectedNetwork) {
        return find(manager, Collections.singletonList(selectedNetwork));
    }

    static boolean eligibleNetwork(ConnectivityManager manager, Network network) {
        NetworkCapabilities capabilities = network == null ? null : manager.getNetworkCapabilities(network);
        return capabilities != null && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
                && !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
                && !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
                && (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                || capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));
    }

    @SuppressWarnings("deprecation") // Snapshot routes when the user selects a manual or offline remembered address.
    static Network routeFor(ConnectivityManager manager, LanEndpoint endpoint) {
        String address = endpoint.address();
        InetAddress peer;
        try {
            // LanEndpoint already requires a canonical numeric IPv4 literal.
            peer = InetAddress.getByName(address.substring(0, address.indexOf(':')));
        } catch (UnknownHostException e) {
            return null;
        }

        Network active = manager.getActiveNetwork();
        Network direct = null;
        int longestPrefix = 0;
        for (Network network : manager.getAllNetworks()) {
            if (!eligibleNetwork(manager, network)) {
                continue;
            }
            LinkProperties properties = manager.getLinkProperties(network);
            if (properties == null) {
                continue;
            }
            for (LinkAddress link : properties.getLinkAddresses()) {
                int prefix = link.getPrefixLength();
                if ((prefix > longestPrefix || (prefix == longestPrefix && network.equals(active)))
                        && samePrivateSubnet(peer, link.getAddress(), prefix)) {
                    direct = network;
                    longestPrefix = prefix;
                }
            }
        }
        if (direct != null) {
            return direct;
        }
        // A bridged/emulated LAN may reach the address via the active network's gateway.
        if (!eligibleNetwork(manager, active)) {
            return null;
        }
        LinkProperties properties = manager.getLinkProperties(active);
        return properties != null && hasRoute(properties, peer) ? active : null;
    }

    private static boolean hasRoute(LinkProperties properties, InetAddress peer) {
        RouteInfo longest = null;
        for (RouteInfo route : properties.getRoutes()) {
            if (route.matches(peer) && (longest == null
                    || route.getDestination().getPrefixLength() > longest.getDestination().getPrefixLength())) {
                longest = route;
            }
        }
        return longest != null && (Build.VERSION.SDK_INT < 33 || longest.getType() == RouteInfo.RTN_UNICAST);
    }

    private List<Pc> find(ConnectivityManager manager, List<Network> networks) {
        String nonce = nonce();
        byte[] request = ("LUMA-DISCOVER-1 " + nonce).getBytes(StandardCharsets.US_ASCII);
        List<Probe> probes = new ArrayList<>();
        Map<String, Pc> found = new LinkedHashMap<>();
        try {
            for (Network network : networks) {
                if (closed || !eligibleNetwork(manager, network)) {
                    continue;
                }
                LinkProperties properties = manager.getLinkProperties(network);
                if (properties == null) {
                    continue;
                }
                for (LinkAddress link : properties.getLinkAddresses()) {
                    if (closed || probes.size() >= 8) {
                        break;
                    }
                    Probe probe = openProbe(network, link, request);
                    if (probe != null) {
                        probes.add(probe);
                    }
                }
            }

            long deadline = SystemClock.elapsedRealtime() + SCAN_MS;
            byte[] buffer = new byte[MAX_REPLY + 1];
            while (!closed && !probes.isEmpty() && found.size() < MAX_PCS
                    && SystemClock.elapsedRealtime() < deadline) {
                Iterator<Probe> iterator = probes.iterator();
                while (!closed && iterator.hasNext() && found.size() < MAX_PCS
                        && SystemClock.elapsedRealtime() < deadline) {
                    Probe probe = iterator.next();
                    DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    try {
                        probe.sendIfDue();
                        probe.socket.receive(packet);
                        Pc pc = parseReply(Arrays.copyOf(buffer, packet.getLength()), nonce, packet.getAddress(),
                                probe.local, probe.prefix);
                        if (pc != null) {
                            found.putIfAbsent(pc.endpoint.address(), new Pc(pc.endpoint, pc.name, probe.network));
                        }
                    } catch (SocketTimeoutException ignored) {
                        // Continue listening on the other eligible interfaces.
                    } catch (IOException e) {
                        probe.socket.close();
                        iterator.remove();
                    }
                }
            }
            return new ArrayList<>(found.values());
        } finally {
            close();
        }
    }

    private Probe openProbe(Network network, LinkAddress link, byte[] request) {
        InetAddress local = link.getAddress();
        int prefix = link.getPrefixLength();
        if (!LanEndpoint.isPrivate(local) || prefix < 1 || prefix > 30) {
            return null;
        }
        DatagramSocket socket = null;
        try {
            int broadcastBits = bits(local) | ~mask(prefix);
            InetAddress broadcast = InetAddress.getByAddress(ByteBuffer.allocate(4).putInt(broadcastBits).array());
            if (!LanEndpoint.isPrivate(broadcast)) {
                return null;
            }
            socket = new DatagramSocket(null);
            sockets.add(socket);
            if (closed) {
                socket.close();
                return null;
            }
            network.bindSocket(socket);
            socket.setBroadcast(true);
            socket.bind(new InetSocketAddress(local, 0));
            socket.setSoTimeout(140);
            Probe probe = new Probe(socket, network, local, prefix,
                    new DatagramPacket(request, request.length, broadcast, LanEndpoint.DEFAULT_PORT));
            probe.sendIfDue();
            return probe;
        } catch (IOException | SecurityException e) {
            if (socket != null) {
                socket.close();
            }
            return null;
        }
    }

    static Pc parseReply(byte[] reply, String nonce, InetAddress sender, InetAddress local, int prefix) {
        if (reply.length > MAX_REPLY || prefix > 30 || !samePrivateSubnet(sender, local, prefix)) {
            return null;
        }
        try {
            String utf8 = StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(reply)).toString();
            JSONObject message = new JSONObject(utf8);
            Object name = message.get("name");
            if (!"luma-discovery-1".equals(message.get("protocol")) || !nonce.equals(message.get("nonce"))
                    || !Integer.valueOf(LanEndpoint.DEFAULT_PORT).equals(message.get("port"))
                    || !(name instanceof String) || !LanPairing.validName((String) name)) {
                return null;
            }
            return new Pc(LanEndpoint.parse(sender.getHostAddress()), (String) name);
        } catch (JSONException | CharacterCodingException | IllegalArgumentException e) {
            return null;
        }
    }

    static boolean samePrivateSubnet(InetAddress peer, InetAddress local, int prefix) {
        return prefix >= 1 && prefix <= 32 && LanEndpoint.isPrivate(peer) && LanEndpoint.isPrivate(local)
                && (bits(peer) & mask(prefix)) == (bits(local) & mask(prefix));
    }

    private static int bits(InetAddress address) {
        return ByteBuffer.wrap(address.getAddress()).getInt();
    }

    private static int mask(int prefix) {
        return -1 << (32 - prefix);
    }

    private static String nonce() {
        StringBuilder value = new StringBuilder(32);
        for (byte octet : LanPairing.randomBytes(16)) {
            value.append(Character.forDigit((octet >> 4) & 0xf, 16));
            value.append(Character.forDigit(octet & 0xf, 16));
        }
        return value.toString();
    }

    @Override
    public void close() {
        closed = true;
        for (DatagramSocket socket : sockets) {
            socket.close();
        }
        sockets.clear();
    }

    static final class Pc {
        final LanEndpoint endpoint;
        final String name;
        final Network network;

        Pc(LanEndpoint endpoint, String name) {
            this(endpoint, name, null);
        }

        Pc(LanEndpoint endpoint, String name, Network network) {
            this.endpoint = endpoint;
            this.name = name;
            this.network = network;
        }
    }

    private static final class Probe {
        final DatagramSocket socket;
        final Network network;
        final InetAddress local;
        final int prefix;
        final DatagramPacket request;
        private long nextSendMs;

        Probe(DatagramSocket socket, Network network, InetAddress local, int prefix, DatagramPacket request) {
            this.socket = socket;
            this.network = network;
            this.local = local;
            this.prefix = prefix;
            this.request = request;
        }

        void sendIfDue() throws IOException {
            long now = SystemClock.elapsedRealtime();
            if (now >= nextSendMs) {
                socket.send(request);
                nextSendMs = now + PROBE_INTERVAL_MS;
            }
        }
    }
}
