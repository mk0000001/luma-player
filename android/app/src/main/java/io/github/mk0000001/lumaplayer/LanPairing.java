package io.github.mk0000001.lumaplayer;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.Locale;

final class LanPairing {
    static final int ID_BYTES = 16;
    static final int NONCE_BYTES = 32;
    static final int PIN_BYTES = 32;
    static final int PLAYBACK_TOKEN_BYTES = 16;
    static final int REMEMBER_TOKEN_BYTES = 32;
    private static final SecureRandom RANDOM = new SecureRandom();

    private LanPairing() {}

    static byte[] randomBytes(int length) {
        byte[] bytes = new byte[length];
        RANDOM.nextBytes(bytes);
        return bytes;
    }

    static String encode(byte[] bytes) {
        return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
    }

    static byte[] decode(String value) {
        return Base64.getUrlDecoder().decode(value);
    }

    static boolean isBase64(String value, int bytes) {
        if (value == null || value.length() != (bytes * 8 + 5) / 6) {
            return false;
        }
        try {
            byte[] decoded = decode(value);
            return decoded.length == bytes && encode(decoded).equals(value);
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    static byte[] sha256(byte[] bytes) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(bytes);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is unavailable", e);
        }
    }

    static String commitment(String pin, String deviceId, String clientNonce) {
        String value = "luma-pair-commit-v1\n" + pin + "\n" + deviceId + "\n" + clientNonce;
        return encode(sha256(value.getBytes(StandardCharsets.UTF_8)));
    }

    static String matchingCode(String pin, String deviceId, String clientNonce, String serverNonce) {
        String value = "luma-pair-code-v1\n" + pin + "\n" + deviceId + "\n" + clientNonce + "\n" + serverNonce;
        long firstFour = Integer.toUnsignedLong(ByteBuffer.wrap(sha256(value.getBytes(StandardCharsets.UTF_8))).getInt());
        return String.format(Locale.ROOT, "%06d", firstFour % 1_000_000);
    }

    static boolean validName(String name) {
        return name != null && name.length() <= 128 && !name.trim().isEmpty()
                && name.getBytes(StandardCharsets.UTF_8).length <= 128
                && name.codePoints().noneMatch(codePoint -> Character.isISOControl(codePoint)
                        || Character.getType(codePoint) == Character.SURROGATE);
    }
}
