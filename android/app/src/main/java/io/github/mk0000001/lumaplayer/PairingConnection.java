package io.github.mk0000001.lumaplayer;

import android.net.Network;
import android.os.SystemClock;
import androidx.media3.common.util.UnstableApi;
import java.io.Closeable;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.Objects;
import org.json.JSONException;
import org.json.JSONObject;

@UnstableApi
final class PairingConnection implements Closeable {
    private static final long APPROVAL_MS = 90_000;
    private final LanEndpoint endpoint;
    private final Network network;
    private LanClient client;
    private boolean closed;

    PairingConnection(LanEndpoint endpoint, Network network) {
        this.endpoint = endpoint;
        this.network = Objects.requireNonNull(network, "LAN network");
    }

    Pending requestApproval(String device, boolean remember) throws IOException, JSONException, GeneralSecurityException {
        LanClient identityClient = replaceClient(LanClient.forIdentity(endpoint, network));
        LanClient.Identity identity = identityClient.fetchIdentity();
        LanClient paired = replaceClient(LanClient.forPairing(endpoint, identity.pin, network));
        String deviceId = LanPairing.encode(LanPairing.randomBytes(LanPairing.ID_BYTES));
        String clientNonce = LanPairing.encode(LanPairing.randomBytes(LanPairing.NONCE_BYTES));
        String commitment = LanPairing.commitment(identity.pin, deviceId, clientNonce);
        long deadline = SystemClock.elapsedRealtime() + APPROVAL_MS;
        JSONObject start = paired.postPairing(LanEndpoint.PAIR_START_PATH,
                new JSONObject().put("deviceId", deviceId).put("device", device)
                        .put("commitment", commitment).put("remember", remember));
        String id = readBase64(start, "id", LanPairing.ID_BYTES);
        String serverNonce = readBase64(start, "serverNonce", LanPairing.NONCE_BYTES);
        JSONObject reveal = paired.postPairing(LanEndpoint.PAIR_REVEAL_PATH,
                new JSONObject().put("id", id).put("clientNonce", clientNonce));
        if (!"pending".equals(reveal.opt("state"))) {
            throw new IOException("Invalid LAN pairing state");
        }
        return new Pending(identity, deviceId, id, LanPairing.matchingCode(identity.pin, deviceId, clientNonce, serverNonce),
                remember, deadline);
    }

    Approval poll(Pending pending) throws IOException, JSONException {
        if (SystemClock.elapsedRealtime() >= pending.deadline) {
            return new Approval(State.DENIED, null, null);
        }
        JSONObject response = currentClient().postPairing(LanEndpoint.PAIR_POLL_PATH, new JSONObject().put("id", pending.id));
        Object state = response.opt("state");
        if ("pending".equals(state)) {
            return new Approval(State.PENDING, null, null);
        }
        if ("denied".equals(state)) {
            return new Approval(State.DENIED, null, null);
        }
        if (!"approved".equals(state)) {
            throw new IOException("Invalid LAN pairing state");
        }
        String token = readBase64(response, "token", LanPairing.PLAYBACK_TOKEN_BYTES);
        String remembered = pending.remember && response.has("rememberToken")
                ? readBase64(response, "rememberToken", LanPairing.REMEMBER_TOKEN_BYTES) : null;
        return new Approval(State.APPROVED, token, remembered);
    }

    LanClient login(RememberedPcStore.Entry remembered, String token)
            throws IOException, JSONException, GeneralSecurityException {
        LanClient login = replaceClient(LanClient.forRemembered(endpoint, remembered.pin, token, network));
        JSONObject response = login.postPairing(LanEndpoint.PAIR_LOGIN_PATH,
                new JSONObject().put("deviceId", remembered.deviceId));
        return playback(remembered.pin, readBase64(response, "token", LanPairing.PLAYBACK_TOKEN_BYTES));
    }

    LanClient playback(String pin, String token) throws GeneralSecurityException, IOException {
        return replaceClient(LanClient.forPlayback(endpoint, pin, token, network));
    }

    private synchronized LanClient replaceClient(LanClient next) throws IOException {
        if (closed) {
            next.close();
            throw new IOException("LAN connection is closed");
        }
        if (client != null) {
            client.close();
        }
        client = next;
        return next;
    }

    private synchronized LanClient currentClient() throws IOException {
        if (closed || client == null) {
            throw new IOException("LAN connection is closed");
        }
        return client;
    }

    @Override
    public synchronized void close() {
        closed = true;
        if (client != null) {
            client.close();
            client = null;
        }
    }

    synchronized void detachClientForPlayback() {
        closed = true;
        client = null;
    }

    private static String readBase64(JSONObject object, String field, int size) throws IOException {
        Object value = object.opt(field);
        if (!(value instanceof String) || !LanPairing.isBase64((String) value, size)) {
            throw new IOException("Invalid LAN pairing response field");
        }
        return (String) value;
    }

    enum State { PENDING, DENIED, APPROVED }

    static final class Pending {
        final LanClient.Identity identity;
        final String deviceId;
        final String id;
        final String code;
        final boolean remember;
        final long deadline;

        Pending(LanClient.Identity identity, String deviceId, String id, String code, boolean remember, long deadline) {
            this.identity = identity;
            this.deviceId = deviceId;
            this.id = id;
            this.code = code;
            this.remember = remember;
            this.deadline = deadline;
        }
    }

    static final class Approval {
        final State state;
        final String token;
        final String rememberToken;

        Approval(State state, String token, String rememberToken) {
            this.state = state;
            this.token = token;
            this.rememberToken = rememberToken;
        }
    }
}
