package io.github.mk0000001.lumaplayer;

import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

final class NearbyCredentials {
    private static final String DESCRIPTOR_PREFIX = "LUMA-DIRECT-1\n";
    private static final Pattern DIRECT_SSID = Pattern.compile("DIRECT-[A-Za-z0-9]{1,25}");

    private NearbyCredentials() {}

    static String advertisedSsid(byte[] descriptor) {
        if (descriptor == null || descriptor.length > DESCRIPTOR_PREFIX.length() + 32) {
            return null;
        }
        String text = new String(descriptor, StandardCharsets.US_ASCII);
        if (!text.startsWith(DESCRIPTOR_PREFIX)) {
            return null;
        }
        String ssid = text.substring(DESCRIPTOR_PREFIX.length());
        return validSsid(ssid) ? ssid : null;
    }

    static boolean validSsid(String ssid) {
        return DIRECT_SSID.matcher(ssid).matches();
    }

    static boolean validPassphrase(CharSequence passphrase) {
        if (passphrase.length() < 8 || passphrase.length() > 63) {
            return false;
        }
        for (int index = 0; index < passphrase.length(); index++) {
            char character = passphrase.charAt(index);
            if (character < ' ' || character > '~') {
                return false;
            }
        }
        return true;
    }
}
