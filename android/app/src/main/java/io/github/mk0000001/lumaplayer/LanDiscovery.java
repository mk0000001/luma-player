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
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import org.json.JSONException;
import org.json.JSONObject;

final class LanDiscovery implements Closeable {
    private static final int MAX_REPLY = 1024;
    private static final int MAX_PCS = 32;
    private static final long SCAN_MS = 2400;
    private static final long PROBE_INTERVAL_MS = 600;
    private static final int VPN_BATCH_SIZE = 16;
    private static final long VPN_BATCH_INTERVAL_MS = 50;
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
        InetAddress peer = endpoint.inetAddress();
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
        LinkProperties vpn = vpnProperties(manager, active);
        if (vpn != null && hasRoute(vpn, peer)) {
            return active;
        }
        // A bridged/emulated LAN may reach the address via the active network's gateway.
        if (!eligibleNetwork(manager, active)) {
            return null;
        }
        LinkProperties properties = manager.getLinkProperties(active);
        return properties != null && hasRoute(properties, peer) ? active : null;
    }

    static boolean canUseDiscoveredRoute(ConnectivityManager manager, Pc pc) {
        if (eligibleNetwork(manager, pc.network)) {
            return true;
        }
        LinkProperties vpn = vpnProperties(manager, pc.network);
        return vpn != null && hasRoute(vpn, pc.endpoint.inetAddress());
    }

    static boolean hasActiveVpn(ConnectivityManager manager) {
        return vpnProperties(manager, manager.getActiveNetwork()) != null;
    }

    private static LinkProperties vpnProperties(ConnectivityManager manager, Network network) {
        if (network == null || !network.equals(manager.getActiveNetwork())) {
            return null;
        }
        NetworkCapabilities capabilities = manager.getNetworkCapabilities(network);
        if (capabilities == null || !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) {
            return null;
        }
        LinkProperties properties = manager.getLinkProperties(network);
        return privateLinkAddress(properties) == null ? null : properties;
    }

    private static InetAddress privateLinkAddress(LinkProperties properties) {
        if (properties != null) {
            for (LinkAddress link : properties.getLinkAddresses()) {
                if (LanEndpoint.isPrivate(link.getAddress())) {
                    return link.getAddress();
                }
            }
        }
        return null;
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
        byte[] request = discoveryRequest(nonce);
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

    List<Pc> findVpn(ConnectivityManager manager, VpnDiscoverySubnet subnet) {
        Network network = manager.getActiveNetwork();
        LinkProperties properties = vpnProperties(manager, network);
        InetAddress local = privateLinkAddress(properties);
        if (closed || local == null) {
            return Collections.emptyList();
        }
        List<InetAddress> targets = new ArrayList<>();
        for (int index = 0; index < subnet.hostCount(); index++) {
            InetAddress target = subnet.hostAt(index);
            if (!target.equals(local) && hasRoute(properties, target)) {
                targets.add(target);
            }
        }
        if (targets.isEmpty()) {
            return Collections.emptyList();
        }

        Map<String, Pc> found = new LinkedHashMap<>();
        try (DatagramSocket socket = new DatagramSocket(null)) {
            sockets.add(socket);
            if (closed) {
                return Collections.emptyList();
            }
            network.bindSocket(socket);
            socket.bind(new InetSocketAddress(local, 0));
            socket.setSoTimeout((int) VPN_BATCH_INTERVAL_MS);
            collectVpnReplies(socket, network, targets, found);
        } catch (IOException | SecurityException ignored) {
            // The VPN can disappear at any point during a foreground search.
        } finally {
            close();
        }
        return new ArrayList<>(found.values());
    }

    private void collectVpnReplies(DatagramSocket socket, Network network, List<InetAddress> targets,
                                   Map<String, Pc> found) throws IOException {
        String nonce = nonce();
        byte[] request = discoveryRequest(nonce);
        Set<InetAddress> queried = new HashSet<>();
        byte[] buffer = new byte[MAX_REPLY + 1];
        DatagramPacket response = new DatagramPacket(buffer, buffer.length);
        long deadline = SystemClock.elapsedRealtime() + SCAN_MS;
        long nextSend = 0;
        int nextHost = 0;
        while (!closed && found.size() < MAX_PCS && SystemClock.elapsedRealtime() < deadline) {
            long now = SystemClock.elapsedRealtime();
            if (nextHost < targets.size() && now >= nextSend) {
                int end = Math.min(nextHost + VPN_BATCH_SIZE, targets.size());
                while (!closed && nextHost < end) {
                    InetAddress target = targets.get(nextHost++);
                    socket.send(new DatagramPacket(request, request.length, target, LanEndpoint.DEFAULT_PORT));
                    queried.add(target);
                }
                nextSend = now + VPN_BATCH_INTERVAL_MS;
            }
            try {
                response.setLength(buffer.length);
                socket.receive(response);
                Pc pc = parseVpnReply(Arrays.copyOf(buffer, response.getLength()), nonce,
                        response.getAddress(), response.getPort(), queried);
                if (pc != null) {
                    found.putIfAbsent(pc.endpoint.address(), new Pc(pc.endpoint, pc.name, network));
                }
            } catch (SocketTimeoutException ignored) {
                // Continue collecting replies until the same bounded deadline as LAN discovery.
            }
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
        if (prefix > 30 || !samePrivateSubnet(sender, local, prefix)) {
            return null;
        }
        return parseReply(reply, nonce, sender);
    }

    static Pc parseVpnReply(byte[] reply, String nonce, InetAddress sender, int port, Set<InetAddress> queried) {
        if (port != LanEndpoint.DEFAULT_PORT || !queried.contains(sender) || !LanEndpoint.isPrivate(sender)) {
            return null;
        }
        return parseReply(reply, nonce, sender);
    }

    private static Pc parseReply(byte[] reply, String nonce, InetAddress sender) {
        if (reply.length > MAX_REPLY) {
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

    private static byte[] discoveryRequest(String nonce) {
        return ("LUMA-DISCOVER-1 " + nonce).getBytes(StandardCharsets.US_ASCII);
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
