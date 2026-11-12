package io.github.mk0000001.lumaplayer;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
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
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import org.junit.Test;

public final class MediaRequestsTest {
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
            try (LanClient.MediaRequests media = new LanClient.MediaRequests(client)) {
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
