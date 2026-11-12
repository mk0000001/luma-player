package io.github.mk0000001.lumaplayer;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.Key;
import java.security.KeyStore;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import org.json.JSONException;
import org.json.JSONObject;

final class RememberedPcStore {
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String KEY_ALIAS = "lumaplayer-remembered-pc-v1";
    private static final String ENTRY_PREFIX = "pc:";
    private static final int IV_BYTES = 12;
    private static final int AUTH_TAG_BITS = 128;
    private static final int ENCRYPTED_BYTES = 43 + AUTH_TAG_BITS / 8;
    private static final int MAX_PCS = 32;

    private final SharedPreferences preferences;

    RememberedPcStore(Context context) {
        preferences = context.getSharedPreferences("remembered-pcs", Context.MODE_PRIVATE);
    }

    List<Entry> entries() {
        List<Entry> records = new ArrayList<>();
        for (Map.Entry<String, ?> preference : preferences.getAll().entrySet()) {
            if (records.size() >= MAX_PCS) {
                break;
            }
            if (preference.getKey().startsWith(ENTRY_PREFIX) && preference.getValue() instanceof String) {
                Entry entry = parse(preference.getKey(), (String) preference.getValue());
                if (entry != null) {
                    records.add(entry);
                }
            }
        }
        return records;
    }

    Entry find(LanEndpoint endpoint) {
        String key = ENTRY_PREFIX + endpoint.address();
        String data = preferences.getString(key, null);
        return data == null ? null : parse(key, data);
    }

    void forget(LanEndpoint endpoint) {
        preferences.edit().remove(ENTRY_PREFIX + endpoint.address()).apply();
    }

    PreparedEntry prepare(LanEndpoint endpoint, String name, String pin, String deviceId, String token)
            throws GeneralSecurityException, IOException, JSONException {
        String key = ENTRY_PREFIX + endpoint.address();
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, secretKey(true));
        cipher.updateAAD(associatedData(endpoint, name, pin, deviceId));
        byte[] tokenBytes = token.getBytes(StandardCharsets.US_ASCII);
        byte[] ciphertext;
        try {
            ciphertext = cipher.doFinal(tokenBytes);
        } finally {
            Arrays.fill(tokenBytes, (byte) 0);
        }
        JSONObject value = new JSONObject().put("name", name).put("pin", pin).put("deviceId", deviceId)
                .put("iv", LanPairing.encode(cipher.getIV())).put("ciphertext", LanPairing.encode(ciphertext));
        return new PreparedEntry(key, value.toString());
    }

    boolean save(PreparedEntry entry) {
        if (!preferences.contains(entry.key) && entries().size() >= MAX_PCS) {
            return false;
        }
        preferences.edit().putString(entry.key, entry.value).apply();
        return true;
    }

    String token(Entry entry) throws GeneralSecurityException, IOException {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, secretKey(false), new GCMParameterSpec(AUTH_TAG_BITS, entry.iv));
        cipher.updateAAD(associatedData(entry.endpoint, entry.name, entry.pin, entry.deviceId));
        byte[] clear = cipher.doFinal(entry.ciphertext);
        try {
            String token = new String(clear, StandardCharsets.US_ASCII);
            if (!LanPairing.isBase64(token, LanPairing.REMEMBER_TOKEN_BYTES)) {
                throw new GeneralSecurityException("Invalid remembered PC credential");
            }
            return token;
        } finally {
            Arrays.fill(clear, (byte) 0);
        }
    }

    private static SecretKey secretKey(boolean create) throws GeneralSecurityException, IOException {
        KeyStore keystore = KeyStore.getInstance(KEYSTORE);
        keystore.load(null);
        Key existing = keystore.getKey(KEY_ALIAS, null);
        if (existing instanceof SecretKey) {
            return (SecretKey) existing;
        }
        if (!create) {
            throw new GeneralSecurityException("The remembered PC key is unavailable");
        }
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        generator.init(new KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(256)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build());
        return generator.generateKey();
    }

    private static byte[] associatedData(LanEndpoint endpoint, String name, String pin, String deviceId) {
        String value = "luma-remembered-pc-v1\n" + endpoint.address() + "\n" + pin + "\n" + deviceId + "\n" + name;
        return value.getBytes(StandardCharsets.UTF_8);
    }

    private static Entry parse(String key, String data) {
        if (data.length() > 1024) {
            return null;
        }
        try {
            LanEndpoint endpoint = LanEndpoint.parse(key.substring(ENTRY_PREFIX.length()));
            JSONObject value = new JSONObject(data);
            String name = value.getString("name");
            String pin = value.getString("pin");
            String deviceId = value.getString("deviceId");
            String iv = value.getString("iv");
            String ciphertext = value.getString("ciphertext");
            if (!key.equals(ENTRY_PREFIX + endpoint.address()) || !LanPairing.validName(name)
                    || !LanPairing.isBase64(pin, LanPairing.PIN_BYTES) || !LanPairing.isBase64(deviceId, LanPairing.ID_BYTES)
                    || !LanPairing.isBase64(iv, IV_BYTES) || !LanPairing.isBase64(ciphertext, ENCRYPTED_BYTES)) {
                return null;
            }
            return new Entry(endpoint, name, pin, deviceId, LanPairing.decode(iv), LanPairing.decode(ciphertext));
        } catch (JSONException | IllegalArgumentException e) {
            return null;
        }
    }

    static final class PreparedEntry {
        final String key;
        final String value;

        PreparedEntry(String key, String value) {
            this.key = key;
            this.value = value;
        }
    }

    static final class Entry {
        final LanEndpoint endpoint;
        final String name;
        final String pin;
        final String deviceId;
        final byte[] iv;
        final byte[] ciphertext;

        Entry(LanEndpoint endpoint, String name, String pin, String deviceId, byte[] iv, byte[] ciphertext) {
            this.endpoint = endpoint;
            this.name = name;
            this.pin = pin;
            this.deviceId = deviceId;
            this.iv = iv;
            this.ciphertext = ciphertext;
        }
    }
}
