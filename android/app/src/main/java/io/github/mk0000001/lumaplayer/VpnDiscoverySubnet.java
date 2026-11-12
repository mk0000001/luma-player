package io.github.mk0000001.lumaplayer;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class VpnDiscoverySubnet {
    private static final Pattern CIDR = Pattern.compile("([0-9.]{7,15})/(2[4-9]|3[0-2])");
    private final int firstHost;
    private final int hostCount;

    private VpnDiscoverySubnet(int firstHost, int hostCount) {
        this.firstHost = firstHost;
        this.hostCount = hostCount;
    }

    static VpnDiscoverySubnet parse(String input) {
        Matcher match = CIDR.matcher(input.trim());
        if (!match.matches()) {
            throw new IllegalArgumentException("Expected a private IPv4 /24 through /32 subnet");
        }
        InetAddress network = LanEndpoint.parse(match.group(1)).inetAddress();
        int address = ByteBuffer.wrap(network.getAddress()).getInt();
        int addressCount = 1 << (32 - Integer.parseInt(match.group(2)));
        if ((address & (addressCount - 1)) != 0) {
            throw new IllegalArgumentException("Host bits must be zero");
        }
        // /31 and /32 have no distinct network or broadcast destinations.
        return addressCount <= 2 ? new VpnDiscoverySubnet(address, addressCount)
                : new VpnDiscoverySubnet(address + 1, addressCount - 2);
    }

    int hostCount() {
        return hostCount;
    }

    InetAddress hostAt(int index) {
        if (index < 0 || index >= hostCount) {
            throw new IndexOutOfBoundsException("VPN discovery host index");
        }
        try {
            return InetAddress.getByAddress(ByteBuffer.allocate(4).putInt(firstHost + index).array());
        } catch (UnknownHostException impossible) {
            throw new AssertionError(impossible);
        }
    }
}
