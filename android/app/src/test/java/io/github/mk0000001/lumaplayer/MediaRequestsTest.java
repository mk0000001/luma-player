package io.github.mk0000001.lumaplayer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import okhttp3.Call;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Protocol;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;
import okio.Buffer;
import org.junit.Test;

public final class MediaRequestsTest {
    @Test
    public void playerErrorsKeepTheStatusWithoutForwardingTheResponseBody() throws Exception {
        for (String path : new String[]{"/v1/media", "/v1/media/variant/720p/0", LanEndpoint.subtitlePath("fixture")}) {
            Request request = new Request.Builder().url("https://192.168.0.2:48762" + path).build();
            try (Response response = responseFor(request, "text/plain").newBuilder().code(416)
                         .header("Content-Range", "bytes */16").build();
                 Response checked = LanClient.checkedPlaybackResponse(response)) {
                assertEquals(416, checked.code());
                assertEquals("bytes */16", checked.header("Content-Range"));
                assertEquals(0, checked.body().bytes().length);
            }
        }
    }

    @Test
    public void bothSubtitleRoutesUseTheResponseGateWithoutChangingMedia() throws Exception {
        for (String path : new String[]{LanEndpoint.subtitlePath("fixture"), LanEndpoint.subtitleAtPath("fixture", 1000)}) {
            Request request = new Request.Builder().url("https://192.168.0.2:48762" + path).build();
            try (Response response = responseFor(request, "text/plain")) {
                assertThrows(IOException.class, () -> LanClient.checkedPlaybackResponse(response));
            }
            try (Response response = responseFor(request, "text/vtt");
                 Response checked = LanClient.checkedPlaybackResponse(response)) {
                assertEquals("fixture", checked.body().string());
            }
            try (Response response = responseFor(request, "text/vtt").newBuilder()
                         .header("Content-Range", "bytes 0-15/16").header("Content-Length", "16").build();
                 Response checked = LanClient.checkedPlaybackResponse(response)) {
                assertEquals(null, checked.header("Content-Range"));
                assertEquals("7", checked.header("Content-Length"));
                assertEquals("fixture", checked.body().string());
            }
        }
        Request media = new Request.Builder().url("https://192.168.0.2:48762/v1/media").build();
        try (Response response = responseFor(media, "video/mp4")) {
            assertTrue(response == LanClient.checkedPlaybackResponse(response));
        }
    }

    private static Response responseFor(Request request, String type) {
        return new Response.Builder().request(request).protocol(Protocol.HTTP_1_1).code(200)
                .message("OK").body(ResponseBody.create("fixture", MediaType.get(type))).build();
    }

    @Test
    public void mediaResponsesAcceptOnlyProgressivePlaybackTypes() throws Exception {
        Request original = new Request.Builder().url("https://192.168.0.2:48762/v1/media").build();
        Request variant = new Request.Builder().url("https://192.168.0.2:48762/v1/media/variant/720p/0").build();
        Request originalAlias = new Request.Builder()
                .url("https://192.168.0.2:48762" + LanEndpoint.mediaVariantPath("original", 0)).build();
        for (String type : new String[]{"video/mp4", "audio/flac", "application/octet-stream"}) {
            try (Response response = responseFor(original, type)) {
                assertTrue(response == LanClient.checkedPlaybackResponse(response));
            }
        }
        for (String type : new String[]{"text/html", "application/vnd.apple.mpegurl"}) {
            try (Response response = responseFor(original, type)) {
                assertThrows(IOException.class, () -> LanClient.checkedPlaybackResponse(response));
            }
        }
        try (Response response = responseFor(variant, "video/x-matroska")) {
            assertTrue(response == LanClient.checkedPlaybackResponse(response));
        }
        try (Response response = responseFor(variant, "video/mp4")) {
            assertThrows(IOException.class, () -> LanClient.checkedPlaybackResponse(response));
        }
        try (Response response = responseFor(originalAlias, "video/mp4")) {
            assertTrue(response == LanClient.checkedPlaybackResponse(response));
        }
    }

    @Test
    public void subtitleResponsesPreserveTextAndRejectUnsupportedContent() throws Exception {
        String cue = "WEBVTT\n\n00:00:00.000 --> 00:00:01.000\nSynthetic cue\n";
        try (ResponseBody input = ResponseBody.create(cue, MediaType.get("text/vtt; charset=utf-8"));
             ResponseBody subtitle = LanClient.boundedSubtitle(input)) {
            assertEquals(cue, subtitle.string());
        }
        try (ResponseBody input = ResponseBody.create(cue, MediaType.get("text/plain"))) {
            assertThrows(IOException.class, () -> LanClient.boundedSubtitle(input));
        }
    }

    @Test
    public void subtitleResponsesEnforceTheHostBudgetWithAndWithoutContentLength() throws Exception {
        int maxBytes = 4 * 1024 * 1024;
        MediaType vtt = MediaType.get("text/vtt");
        try (ResponseBody declared = ResponseBody.create(new Buffer(), vtt, maxBytes + 1L)) {
            assertThrows(IOException.class, () -> LanClient.boundedSubtitle(declared));
        }
        try (ResponseBody chunked = ResponseBody.create(new Buffer().write(new byte[maxBytes + 1]), vtt, -1);
             ResponseBody subtitle = LanClient.boundedSubtitle(chunked)) {
            subtitle.source().skip(maxBytes);
            assertThrows(IOException.class, () -> subtitle.source().exhausted());
        }
    }

    @Test
    public void replacingMediaCancelsTheBodyAfterResponseHeadersHaveArrived() throws Exception {
        CountDownLatch releaseBody = new CountDownLatch(1);
        OkHttpClient client = new OkHttpClient.Builder().readTimeout(2, TimeUnit.SECONDS).build();
        try (ServerSocket server = new ServerSocket()) {
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0));
            server.setSoTimeout(3000);
            FutureTask<Void> responseWriter = new FutureTask<>(() -> {
                try (Socket peer = server.accept()) {
                    peer.setSoTimeout(2000);
                    BufferedReader headers = new BufferedReader(new InputStreamReader(
                            peer.getInputStream(), StandardCharsets.US_ASCII));
                    for (String header; (header = headers.readLine()) != null && !header.isEmpty();) {
                        // Wait until the complete synthetic request is available.
                    }
                    peer.getOutputStream().write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n"
                            .getBytes(StandardCharsets.US_ASCII));
                    peer.getOutputStream().write(1);
                    peer.getOutputStream().flush();
                    releaseBody.await(5, TimeUnit.SECONDS);
                }
                return null;
            });
            Thread serverThread = new Thread(responseWriter, "synthetic-media-response");
            serverThread.setDaemon(true);
            serverThread.start();
            Request request = new Request.Builder().url("http://127.0.0.1:" + server.getLocalPort() + "/media").build();
            Call sessionCall = client.newCall(request);
            try (LanClient.RequestScope media = new LanClient.RequestScope(client)) {
                Call old = media.newCall(request);
                try (Response response = old.execute()) {
                    assertTrue(response.isSuccessful());
                    assertTrue(response.body().source().readByte() == 1);
                    media.close();
                    assertTrue(old.isCanceled());
                    assertTrue(media.newCall(request).isCanceled());
                    assertFalse(sessionCall.isCanceled());
                }
            }
            releaseBody.countDown();
            responseWriter.get(2, TimeUnit.SECONDS);
        } finally {
            releaseBody.countDown();
            client.connectionPool().evictAll();
        }
    }
}
