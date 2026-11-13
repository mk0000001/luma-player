package io.github.mk0000001.lumaplayer;

import android.annotation.SuppressLint;
import android.net.Network;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.datasource.okhttp.OkHttpDataSource;
import java.io.Closeable;
import java.io.IOException;
import java.net.Proxy;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import okhttp3.Call;
import okhttp3.CookieJar;
import okhttp3.EventListener;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.ResponseBody;
import okio.Buffer;
import okio.BufferedSource;
import okio.ForwardingSource;
import okio.Okio;
import org.json.JSONException;
import org.json.JSONObject;

@UnstableApi
final class LanClient implements Closeable {
    private static final long MAX_SESSION_BYTES = 64 * 1024;
    private static final long MAX_PAIR_BYTES = 4 * 1024;
    private static final long MAX_SUBTITLE_BYTES = 4L * 1024 * 1024;
    private static final MediaType JSON = MediaType.get("application/json; charset=utf-8");

    private final LanEndpoint endpoint;
    private final OkHttpClient client;
    private final RequestScope metadataRequests;
    private final ServerTrustManager trust;
    private final AtomicBoolean closed = new AtomicBoolean();

    static LanClient forIdentity(LanEndpoint endpoint) throws GeneralSecurityException {
        return forIdentity(endpoint, null);
    }

    static LanClient forIdentity(LanEndpoint endpoint, Network network) throws GeneralSecurityException {
        return new LanClient(endpoint, Access.IDENTITY, null, null, network);
    }

    static LanClient forPairing(LanEndpoint endpoint, String pin) throws GeneralSecurityException {
        return forPairing(endpoint, pin, null);
    }

    static LanClient forPairing(LanEndpoint endpoint, String pin, Network network) throws GeneralSecurityException {
        return new LanClient(endpoint, Access.PAIRING, pin, null, network);
    }

    static LanClient forRemembered(LanEndpoint endpoint, String pin, String token) throws GeneralSecurityException {
        return forRemembered(endpoint, pin, token, null);
    }

    static LanClient forRemembered(LanEndpoint endpoint, String pin, String token, Network network)
            throws GeneralSecurityException {
        return new LanClient(endpoint, Access.REMEMBERED, pin, token, network);
    }

    static LanClient forPlayback(LanEndpoint endpoint, String pin, String token) throws GeneralSecurityException {
        return forPlayback(endpoint, pin, token, null);
    }

    static LanClient forPlayback(LanEndpoint endpoint, String pin, String token, Network network)
            throws GeneralSecurityException {
        return new LanClient(endpoint, Access.PLAYBACK, pin, token, network);
    }

    private LanClient(LanEndpoint endpoint, Access access, String pin, String token, Network network)
            throws GeneralSecurityException {
        if ((access != Access.IDENTITY && !LanPairing.isBase64(pin, LanPairing.PIN_BYTES))
                || (access == Access.REMEMBERED && !LanPairing.isBase64(token, LanPairing.REMEMBER_TOKEN_BYTES))
                || (access == Access.PLAYBACK && !LanPairing.isBase64(token, LanPairing.PLAYBACK_TOKEN_BYTES))) {
            throw new GeneralSecurityException("Invalid LAN trust material");
        }
        this.endpoint = endpoint;
        trust = new ServerTrustManager(pin == null ? null : LanPairing.decode(pin));
        SSLContext tls = SSLContext.getInstance("TLS");
        tls.init(null, new TrustManager[]{trust}, null);
        String authorization = token == null ? null : "Bearer " + token;
        // OkHttp keeps its default HTTPS hostname verifier, including IP subject alternative names.
        OkHttpClient.Builder builder = new OkHttpClient.Builder()
                .sslSocketFactory(tls.getSocketFactory(), trust)
                .proxy(Proxy.NO_PROXY)
                .followRedirects(false)
                .followSslRedirects(false)
                .retryOnConnectionFailure(false)
                .cookieJar(CookieJar.NO_COOKIES)
                .connectTimeout(8, TimeUnit.SECONDS)
                .readTimeout(135, TimeUnit.SECONDS)
                .addInterceptor(chain -> {
                    Request request = chain.request();
                    if (closed.get()) {
                        throw new IOException("LAN session is closed");
                    }
                    if (!endpoint.permits(request.url()) || !access.permits(request.method(), request.url().encodedPath())) {
                        throw new IOException("Request is outside the paired LAN session");
                    }
                    Request.Builder scopedRequest = request.newBuilder().removeHeader("Authorization")
                            .header("Cache-Control", "no-store");
                    if (authorization != null) {
                        scopedRequest.header("Authorization", authorization);
                    }
                    Response response = chain.proceed(scopedRequest.build());
                    if (!request.url().equals(response.request().url())
                            || (response.code() >= 300 && response.code() < 400)) {
                        response.close();
                        throw new IOException("LAN session redirects are disabled");
                    }
                    if (access == Access.PLAYBACK) {
                        return checkedPlaybackResponse(response);
                    }
                    return response;
                });
        if (network != null) {
            builder.socketFactory(network.getSocketFactory());
        }
        client = builder.build();
        metadataRequests = new RequestScope(client);
    }

    HttpUrl url(String path) {
        return endpoint.url(path);
    }

    RequestScope mediaRequests() {
        return new RequestScope(client);
    }

    static final class RequestScope implements Call.Factory, Closeable {
        private final OkHttpClient client;
        private final Set<Call> open = new HashSet<>();
        private boolean closed;

        RequestScope(OkHttpClient base) {
            client = base.newBuilder().eventListener(new EventListener() {
                @Override public void callEnd(Call call) { remove(call); }
                @Override public void callFailed(Call call, IOException error) { remove(call); }
            }).build();
        }

        private synchronized void remove(Call call) {
            open.remove(call);
        }

        @Override
        public synchronized Call newCall(Request request) {
            Call call = client.newCall(request);
            if (closed) {
                call.cancel();
            } else {
                open.add(call);
            }
            return call;
        }

        OkHttpDataSource.Factory dataSourceFactory() {
            return new OkHttpDataSource.Factory(this);
        }

        @Override
        public void close() {
            List<Call> pending;
            synchronized (this) {
                closed = true;
                pending = new ArrayList<>(open);
                open.clear();
            }
            // Synchronous calls leave the OkHttp dispatcher when headers arrive,
            // even if their response bodies are still being read.
            pending.forEach(Call::cancel);
        }
    }

    Identity fetchIdentity() throws IOException, JSONException {
        JSONObject result = LanJson.parseObject(readJson(LanEndpoint.IDENTITY_PATH, null, 8, MAX_PAIR_BYTES));
        Object name = result.get("name");
        String pin = trust.observedPin;
        if (!(name instanceof String) || !LanPairing.validName((String) name)
                || !LanPairing.isBase64(pin, LanPairing.PIN_BYTES)) {
            throw new IOException("Invalid LAN identity");
        }
        return new Identity((String) name, pin);
    }

    JSONObject postPairing(String path, JSONObject body) throws IOException, JSONException {
        return LanJson.parseObject(readJson(path, body, 5, MAX_PAIR_BYTES));
    }

    LanSession fetchSession() throws IOException {
        return fetchSession(12);
    }

    LanSession checkSession() throws IOException {
        return fetchSession(4);
    }

    void requestPlaybackStart() throws IOException {
        sendPlayerCommand(LanEndpoint.PLAYBACK_START_PATH, RequestBody.create(new byte[0], null));
    }

    void controlPlayer(String action) throws IOException {
        sendPlayerCommand(LanEndpoint.PLAYER_CONTROL_PATH,
                RequestBody.create("{\"action\":" + JSONObject.quote(action) + "}", JSON));
    }

    private void sendPlayerCommand(String path, RequestBody body) throws IOException {
        Request request = new Request.Builder().url(url(path)).post(body).build();
        Call call = metadataRequests.newCall(request);
        call.timeout().timeout(5, TimeUnit.SECONDS);
        try (Response response = call.execute()) {
            if (response.code() != 202) {
                throw new HttpException(response.code());
            }
        }
    }

    private LanSession fetchSession(long timeoutSeconds) throws IOException {
        return LanSession.parse(readJson(LanEndpoint.SESSION_PATH, null, timeoutSeconds, MAX_SESSION_BYTES));
    }

    private String readJson(String path, JSONObject body, long timeoutSeconds, long maxBytes) throws IOException {
        Request.Builder request = new Request.Builder().url(url(path)).header("Accept", "application/json");
        if (body != null) {
            request.post(RequestBody.create(body.toString(), JSON));
        }
        Call call = metadataRequests.newCall(request.build());
        call.timeout().timeout(timeoutSeconds, TimeUnit.SECONDS);
        try (Response response = call.execute()) {
            if (response.code() != 200) {
                throw new HttpException(response.code());
            }
            ResponseBody content = response.body();
            MediaType type = content == null ? null : content.contentType();
            if (type == null || !"application".equals(type.type()) || !"json".equals(type.subtype())) {
                throw new IOException("Invalid LAN session response type");
            }
            if (content.contentLength() > maxBytes || content.source().request(maxBytes + 1)) {
                throw new IOException("LAN session metadata exceeds the size limit");
            }
            return StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(content.source().readByteArray())).toString();
        }
    }

    static Response checkedPlaybackResponse(Response response) throws IOException {
        String path = response.request().url().encodedPath();
        if (!LanEndpoint.isPlaybackStreamPath(path)) {
            return response;
        }
        ResponseBody body = response.body();
        if (!response.isSuccessful()) {
            // Media3 materializes error bodies. Keep status and range/retry headers only.
            if (body != null) {
                body.close();
            }
            return response.newBuilder().removeHeader("Content-Encoding").removeHeader("Transfer-Encoding")
                    .header("Content-Length", "0").body(ResponseBody.create(new byte[0], null)).build();
        }
        if (!LanEndpoint.isSubtitlePath(path)) {
            MediaType type = body == null ? null : body.contentType();
            String mimeType = type == null ? "" : type.type() + "/" + type.subtype();
            boolean original = LanEndpoint.MEDIA_PATH.equals(path)
                    || LanEndpoint.mediaVariantPath("original", 0).equals(path);
            boolean supported = original
                    ? LanSession.supportsFileMime(mimeType) : LanSession.DISC_MIME.equals(mimeType);
            if (!supported) {
                if (body != null) {
                    body.close();
                }
                throw new IOException("Unsupported LAN media response type");
            }
            return response;
        }
        try {
            if (response.code() != 200) {
                throw new IOException("LAN subtitle must be a complete response");
            }
            ResponseBody checked = boundedSubtitle(body);
            // Media3 uses the raw headers as an allocation hint before reading the source.
            Response.Builder result = response.newBuilder().removeHeader("Content-Range")
                    .removeHeader("Content-Length").body(checked);
            if (checked.contentLength() >= 0) {
                result.header("Content-Length", Long.toString(checked.contentLength()));
            }
            return result.build();
        } catch (IOException error) {
            if (body != null) {
                body.close();
            }
            throw error;
        }
    }

    static ResponseBody boundedSubtitle(ResponseBody body) throws IOException {
        MediaType type = body == null ? null : body.contentType();
        if (type == null || !"text".equals(type.type()) || !"vtt".equals(type.subtype())) {
            throw new IOException("LAN subtitle must be WebVTT");
        }
        long length = body.contentLength();
        if (length > MAX_SUBTITLE_BYTES) {
            throw new IOException("LAN subtitle exceeds its size limit");
        }
        BufferedSource bounded = Okio.buffer(new ForwardingSource(body.source()) {
            private long remaining = MAX_SUBTITLE_BYTES;

            @Override public long read(Buffer sink, long byteCount) throws IOException {
                if (byteCount == 0) {
                    return 0;
                }
                if (remaining == 0) {
                    if (super.read(new Buffer(), 1) == -1) {
                        return -1;
                    }
                    throw new IOException("LAN subtitle exceeds its size limit");
                }
                long count = super.read(sink, Math.min(byteCount, remaining));
                if (count > 0) {
                    remaining -= count;
                }
                return count;
            }
        });
        return ResponseBody.create(bounded, type, length);
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        // Closing a pooled TLS connection can perform network I/O.
        client.dispatcher().executorService().execute(() -> {
            metadataRequests.close();
            client.dispatcher().cancelAll();
            client.connectionPool().evictAll();
        });
    }

    static final class Identity {
        final String name;
        final String pin;

        Identity(String name, String pin) {
            this.name = name;
            this.pin = pin;
        }
    }

    static final class HttpException extends IOException {
        final int status;

        HttpException(int status) {
            super("LAN session HTTP error " + status);
            this.status = status;
        }
    }

    static final class PinMismatch extends CertificateException {
        PinMismatch() {
            super("PC certificate does not match the pinned public key");
        }
    }

    private enum Access {
        IDENTITY, PAIRING, REMEMBERED, PLAYBACK;

        boolean permits(String method, String path) {
            return switch (this) {
                case IDENTITY -> "GET".equals(method) && LanEndpoint.IDENTITY_PATH.equals(path);
                case PAIRING -> "POST".equals(method) && (LanEndpoint.PAIR_START_PATH.equals(path)
                        || LanEndpoint.PAIR_REVEAL_PATH.equals(path) || LanEndpoint.PAIR_POLL_PATH.equals(path));
                case REMEMBERED -> "POST".equals(method) && LanEndpoint.PAIR_LOGIN_PATH.equals(path);
                case PLAYBACK -> ("GET".equals(method) && LanEndpoint.isMediaPath(path))
                        || ("POST".equals(method) && (LanEndpoint.PLAYBACK_START_PATH.equals(path)
                                || LanEndpoint.PLAYER_CONTROL_PATH.equals(path)));
            };
        }
    }

    // Unpinned trust is confined to the anonymous identity GET. Every subsequent request pins the SPKI.
    @SuppressLint("CustomX509TrustManager")
    private static final class ServerTrustManager implements X509TrustManager {
        private final byte[] expectedSpki;
        private volatile String observedPin;

        ServerTrustManager(byte[] expectedSpki) {
            this.expectedSpki = expectedSpki;
        }

        @Override
        public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            if (chain == null || chain.length == 0) {
                throw new CertificateException("Missing server certificate");
            }
            X509Certificate leaf = chain[0];
            leaf.checkValidity();
            byte[] actual = LanPairing.sha256(leaf.getPublicKey().getEncoded());
            if (expectedSpki != null && !MessageDigest.isEqual(expectedSpki, actual)) {
                throw new PinMismatch();
            }
            observedPin = LanPairing.encode(actual);
        }

        @Override
        public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            throw new CertificateException("Unexpected client certificate validation");
        }

        @Override
        public X509Certificate[] getAcceptedIssuers() {
            return new X509Certificate[0];
        }
    }
}
