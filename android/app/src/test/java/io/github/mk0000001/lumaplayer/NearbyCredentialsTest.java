package io.github.mk0000001.lumaplayer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.nio.charset.StandardCharsets;
import org.junit.Test;

public final class NearbyCredentialsTest {
    @Test
    public void acceptsOnlyTheExactPublicDescriptor() {
        String longest = "DIRECT-" + "A1".repeat(12) + "Z";
        assertEquals(longest, parse("LUMA-DIRECT-1\n" + longest));
        assertEquals("DIRECT-Ab123", parse("LUMA-DIRECT-1\nDIRECT-Ab123"));
        for (String value : new String[]{"LUMA-DIRECT-1\nDIRECT-", "LUMA-DIRECT-1\n" + longest + "1",
                "LUMA-DIRECT-1\r\nDIRECT-Ab123", "LUMA-DIRECT-2\nDIRECT-Ab123", "DIRECT-Ab123",
                "LUMA-DIRECT-1\nDIRECT-Ab123\npassword", "LUMA-DIRECT-1\nDIRECT-Ab123\n",
                "LUMA-DIRECT-1\nDIRECT-Ab-123", "LUMA-DIRECT-1\nDIRECT-한글"}) {
            assertNull(value, parse(value));
        }
    }

    @Test
    public void validatesWpa2PassphraseWithoutRewritingIt() {
        assertTrue(NearbyCredentials.validPassphrase(" Ab1234 "));
        assertTrue(NearbyCredentials.validPassphrase("a".repeat(63)));
        assertFalse(NearbyCredentials.validPassphrase("a".repeat(64)));
        assertFalse(NearbyCredentials.validPassphrase("1234567"));
        assertFalse(NearbyCredentials.validPassphrase("1234567\n"));
        assertFalse(NearbyCredentials.validPassphrase("1234567한"));
    }

    private static String parse(String value) {
        return NearbyCredentials.advertisedSsid(value.getBytes(StandardCharsets.UTF_8));
    }
}
