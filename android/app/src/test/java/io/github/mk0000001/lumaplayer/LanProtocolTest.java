package io.github.mk0000001.lumaplayer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.IOException;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import okhttp3.HttpUrl;
import org.junit.Test;

public final class LanProtocolTest {
    @Test
    public void acceptsOnlyCanonicalRfc1918AddressesWithOptionalPort() {
        assertEquals("https://10.1.2.3:48762/v1/media",
                LanEndpoint.parse(" 10.1.2.3 ").url("/v1/media").toString());
        assertEquals("https://172.31.8.7:9443/v1/session",
                LanEndpoint.parse("172.31.8.7:9443").url("/v1/session").toString());
        assertEquals("https://192.168.0.9/v1/media",
                LanEndpoint.parse("192.168.0.9:443").url("/v1/media").toString());

        for (String value : new String[]{"127.0.0.1", "172.15.0.1", "172.32.0.1", "169.254.0.1",
                "100.64.0.1", "192.168.0.256", "192.168.01.1", "192.168.0.1:65536",
                "192.168.0.1:0", "https://192.168.0.1", "192.168.0.1@10.1.2.3", "10.0.0.1/",
                "[::ffff:192.168.0.1]", "192.168.0.1.example.org", "192.168.0.1:۴۴۳"}) {
            assertThrows(value, IllegalArgumentException.class, () -> LanEndpoint.parse(value));
        }
    }

    @Test
    public void credentialsStayWithinTheirOriginalHttpsOriginAndPaths() {
        LanEndpoint endpoint = LanEndpoint.parse("192.168.0.12");
        assertTrue(endpoint.permits(HttpUrl.get("https://192.168.0.12:48762/v1/subtitle/ko_1.vtt")));
        for (String value : new String[]{"http://192.168.0.12:48762/v1/media",
                "https://192.168.0.13:48762/v1/media", "https://192.168.0.12:443/v1/media",
                "https://192.168.0.12:48762/v1/media?x=1", "https://192.168.0.12:48762/v1/media#x",
                "https://a@192.168.0.12:48762/v1/media", "https://192.168.0.12:48762/v1/other",
                "https://192.168.0.12:48762/v1/subtitle/a%2fb.vtt"}) {
            assertFalse(value, endpoint.permits(HttpUrl.get(value)));
        }
        assertThrows(IllegalArgumentException.class,
                () -> endpoint.url("/v1/subtitle/../media"));
    }

    @Test
    public void pairingCommitmentAndCodeUseTheCanonicalWireValues() {
        String pin = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
        String device = "ICEiIyQlJicoKSorLC0uLw";
        String client = "MDEyMzQ1Njc4OTo7PD0-P0BBQkNERUZHSElKS0xNTk8";
        String server = "UFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm8";
        assertEquals("6xtjJxqTMnR9s8svgvqwSXUEisYZg8qkMB_HOSUGPLw", LanPairing.commitment(pin, device, client));
        assertEquals("619510", LanPairing.matchingCode(pin, device, client, server));
        assertTrue(LanPairing.isBase64(device, 16));
        assertFalse(LanPairing.isBase64("ICEiIyQlJicoKSorLC0uLx", 16));
        assertFalse(LanPairing.isBase64(device + "=", 16));
        assertFalse(LanPairing.isBase64(device, 32));
    }

    @Test
    public void discoveryRequiresTheCurrentNonceAndLocalSubnet() throws Exception {
        String nonce = "00112233445566778899aabbccddeeff";
        String reply = "{\"protocol\":\"luma-discovery-1\",\"nonce\":\"" + nonce
                + "\",\"port\":48762,\"name\":\"편집 PC\"}";
        InetAddress local = InetAddress.getByName("192.168.5.11");
        InetAddress sender = InetAddress.getByName("192.168.5.25");
        LanDiscovery.Pc pc = LanDiscovery.parseReply(reply.getBytes(StandardCharsets.UTF_8), nonce, sender, local, 24);
        assertEquals("192.168.5.25:48762", pc.endpoint.address());
        assertEquals("편집 PC", pc.name);
        assertNull(LanDiscovery.parseReply(reply.getBytes(StandardCharsets.UTF_8), "different", sender, local, 24));
        assertNull(LanDiscovery.parseReply(reply.getBytes(StandardCharsets.UTF_8), nonce,
                InetAddress.getByName("192.168.6.25"), local, 24));
        assertNull(LanDiscovery.parseReply(reply.getBytes(StandardCharsets.UTF_8), nonce,
                InetAddress.getByName("192.0.2.3"), local, 1));
        assertNull(LanDiscovery.parseReply(reply.replace("48762", "80").getBytes(StandardCharsets.UTF_8),
                nonce, sender, local, 24));
        assertNull(LanDiscovery.parseReply(reply.replace("편집 PC", "\\u0009bad").getBytes(StandardCharsets.UTF_8),
                nonce, sender, local, 24));
    }

    @Test
    public void directRoutesMatchPrivateIpv4PrefixesIncludingHostRoutes() throws Exception {
        InetAddress local = InetAddress.getByName("10.0.2.14");
        InetAddress peer = InetAddress.getByName("10.0.2.15");
        assertTrue(LanDiscovery.samePrivateSubnet(peer, local, 31));
        assertTrue(LanDiscovery.samePrivateSubnet(local, local, 32));
        assertFalse(LanDiscovery.samePrivateSubnet(peer, local, 32));
        assertFalse(LanDiscovery.samePrivateSubnet(peer, local, 0));
        assertFalse(LanDiscovery.samePrivateSubnet(InetAddress.getByName("192.168.5.25"), local, 24));
        assertFalse(LanDiscovery.samePrivateSubnet(InetAddress.getByName("192.0.2.3"), local, 1));
        assertFalse(LanDiscovery.samePrivateSubnet(InetAddress.getByName("fd00::1"), local, 24));
    }

    @Test
    public void pairingRequiresASelectedNetwork() {
        assertThrows(NullPointerException.class, () -> new PairingConnection(LanEndpoint.parse("192.168.5.25"), null));
    }

    @Test
    public void validatesSidecarPathsBeforeTheyCanBePassedToThePlayer() throws IOException {
        String json = """
                {"version":1,"title":"Synthetic","mimeType":"video/mp4","sourceKind":"file",
                "durationMs":5000,"seekable":true,"subtitles":[{"id":"ko","label":"Korean",
                "language":"ko","path":"/v1/subtitle/ko.vtt"}]}
                """;
        LanSession session = LanSession.parse(json);
        assertTrue(session.seekable);
        assertEquals("/v1/subtitle/ko.vtt", session.subtitles.get(0).path);
        assertThrows(IOException.class, () -> LanSession.parse(json.replace("/v1/subtitle/ko.vtt",
                "https://192.168.0.50:48762/v1/subtitle/ko.vtt")));
        assertThrows(IOException.class, () -> LanSession.parse(json.replace("/v1/subtitle/ko.vtt",
                "/v1/subtitle/other.vtt")));
        assertThrows(IOException.class, () -> LanSession.parse(json.replace("\"version\":1",
                "\"version\":2")));
    }

    @Test
    public void preservesLiveDiscSemanticsAndRejectsPlaylistSources() throws IOException {
        String disc = """
                {"version":1,"title":"Disc","mimeType":"video/x-matroska","sourceKind":"bluray",
                "durationMs":0,"seekable":false,"subtitles":[]}
                """;
        LanSession session = LanSession.parse(disc);
        assertTrue(session.isBluRay());
        assertFalse(session.seekable);
        assertThrows(IOException.class, () -> LanSession.parse(disc.replace("false", "true")));
        assertThrows(IOException.class, () -> LanSession.parse(disc.replace("video/x-matroska",
                "application/vnd.apple.mpegurl")));
    }
}
