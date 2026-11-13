package io.github.mk0000001.lumaplayer;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import okhttp3.HttpUrl;

final class LanEndpoint {
    static final String SESSION_PATH = "/v1/session";
    static final String MEDIA_PATH = "/v1/media";
    static final String PLAYBACK_START_PATH = "/v1/playback/start";
    static final String PLAYER_CONTROL_PATH = "/v1/player/control";
    static final String IDENTITY_PATH = "/v1/pair/identity";
    static final String PAIR_START_PATH = "/v1/pair/start";
    static final String PAIR_REVEAL_PATH = "/v1/pair/reveal";
    static final String PAIR_POLL_PATH = "/v1/pair/poll";
    static final String PAIR_LOGIN_PATH = "/v1/pair/login";
    static final int DEFAULT_PORT = 48762;
    private static final Pattern ADDRESS = Pattern.compile(
            "([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})(?::([0-9]{1,5}))?");
    private static final String SUBTITLE_PREFIX = "/v1/subtitle/";
    private static final String SUBTITLE_ID = "[A-Za-z0-9_-]{1,64}";
    private static final Pattern SUBTITLE_PATH = Pattern.compile(SUBTITLE_PREFIX + SUBTITLE_ID + "\\.vtt");
    private static final String OFFSET = "(?:0|[1-9][0-9]{0,8})";
    private static final Pattern VARIANT_PATH = Pattern.compile(
            "/v1/media/variant/(?:original|1080p|720p|480p)/" + OFFSET);
    private static final Pattern SUBTITLE_AT_PATH = Pattern.compile(
            "/v1/subtitle-at/(?:" + OFFSET + "|-[1-9][0-9]{0,3})/" + SUBTITLE_ID + "\\.vtt");
    private static final Pattern VALID_SUBTITLE_ID = Pattern.compile(SUBTITLE_ID);

    private final String host;
    private final int port;

    private LanEndpoint(String host, int port) {
        this.host = host;
        this.port = port;
    }

    static LanEndpoint parse(String input) {
        Matcher match = ADDRESS.matcher(input.trim());
        if (!match.matches()) {
            throw new IllegalArgumentException("Invalid LAN address");
        }

        int[] address = new int[4];
        for (int i = 0; i < address.length; i++) {
            String octet = match.group(i + 1);
            address[i] = Integer.parseInt(octet);
            if (address[i] > 255 || (octet.length() > 1 && octet.charAt(0) == '0')) {
                throw new IllegalArgumentException("Invalid IPv4 octet");
            }
        }
        if (!isPrivate(address[0], address[1])) {
            throw new IllegalArgumentException("Address is outside RFC 1918");
        }

        int port = match.group(5) == null ? DEFAULT_PORT : Integer.parseInt(match.group(5));
        if (port < 1 || port > 65535) {
            throw new IllegalArgumentException("Invalid LAN port");
        }
        String host = address[0] + "." + address[1] + "." + address[2] + "." + address[3];
        return new LanEndpoint(host, port);
    }

    String address() {
        return host + ":" + port;
    }

    InetAddress inetAddress() {
        try {
            // The stored host is already a canonical numeric IPv4 literal.
            return InetAddress.getByName(host);
        } catch (UnknownHostException impossible) {
            throw new AssertionError(impossible);
        }
    }

    static boolean isPrivate(InetAddress address) {
        if (!(address instanceof Inet4Address)) {
            return false;
        }
        byte[] bytes = address.getAddress();
        return isPrivate(bytes[0] & 0xff, bytes[1] & 0xff);
    }

    private static boolean isPrivate(int first, int second) {
        return first == 10 || (first == 172 && second >= 16 && second <= 31)
                || (first == 192 && second == 168);
    }

    static String subtitlePath(String id) {
        if (!VALID_SUBTITLE_ID.matcher(id).matches()) {
            throw new IllegalArgumentException("Invalid subtitle ID");
        }
        return SUBTITLE_PREFIX + id + ".vtt";
    }

    static String mediaVariantPath(String profile, long startMs) {
        String path = "/v1/media/variant/" + profile + "/" + startMs;
        if (!VARIANT_PATH.matcher(path).matches()) {
            throw new IllegalArgumentException("Invalid media variant");
        }
        return path;
    }

    static String subtitleAtPath(String id, long originMs) {
        String path = "/v1/subtitle-at/" + originMs + "/" + id + ".vtt";
        if (!SUBTITLE_AT_PATH.matcher(path).matches() || originMs < -5000) {
            throw new IllegalArgumentException("Invalid subtitle origin");
        }
        return path;
    }

    HttpUrl url(String path) {
        if (!permitsPath(path)) {
            throw new IllegalArgumentException("Unsupported session path");
        }
        return new HttpUrl.Builder().scheme("https").host(host).port(port).encodedPath(path).build();
    }

    boolean permits(HttpUrl url) {
        return url.isHttps() && url.host().equals(host) && url.port() == port
                && url.encodedUsername().isEmpty() && url.encodedPassword().isEmpty()
                && url.encodedQuery() == null && url.encodedFragment() == null
                && permitsPath(url.encodedPath());
    }

    private static boolean permitsPath(String path) {
        return isMediaPath(path) || PLAYER_CONTROL_PATH.equals(path) || PLAYBACK_START_PATH.equals(path) || IDENTITY_PATH.equals(path) || PAIR_START_PATH.equals(path)
                || PAIR_REVEAL_PATH.equals(path) || PAIR_POLL_PATH.equals(path) || PAIR_LOGIN_PATH.equals(path);
    }

    static boolean isMediaPath(String path) {
        return SESSION_PATH.equals(path) || isPlaybackStreamPath(path);
    }

    static boolean isPlaybackStreamPath(String path) {
        return MEDIA_PATH.equals(path) || VARIANT_PATH.matcher(path).matches() || isSubtitlePath(path);
    }

    static boolean isSubtitlePath(String path) {
        return SUBTITLE_PATH.matcher(path).matches() || SUBTITLE_AT_PATH.matcher(path).matches();
    }
}
