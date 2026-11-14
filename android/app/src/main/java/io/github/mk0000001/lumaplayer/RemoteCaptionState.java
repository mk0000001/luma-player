package io.github.mk0000001.lumaplayer;

import java.util.Set;
import org.json.JSONException;
import org.json.JSONObject;

record RemoteCaptionState(long revision, String state, boolean enabled, boolean canAI, boolean canControl,
                          boolean original, boolean reading, boolean translation, String translationState,
                          long aheadMs, String backend, String vtt) {
    private static final Set<String> STATES = Set.of("off", "waiting", "processing", "ready", "failed", "file", "prepared");
    private static final Set<String> TRANSLATION_STATES = Set.of("", "off", "waiting", "processing", "ready", "failed");

    static RemoteCaptionState parse(String value) throws JSONException {
        JSONObject json = LanJson.parseObject(value);
        String state = text(json, "state");
        String translationState = text(json, "translationState");
        long revision = integer(json, "revision");
        long ahead = integer(json, "aheadMs");
        String backend = text(json, "backend");
        String vtt = text(json, "vtt");
        if (revision < 0 || ahead < 0 || ahead > 120_000 || !STATES.contains(state)
                || !TRANSLATION_STATES.contains(translationState)
                || !Set.of("", "CPU", "GPU", "NPU").contains(backend)
                || (!vtt.isEmpty() && !validVttHeader(vtt))) {
            throw new JSONException("Unexpected caption state");
        }
        return new RemoteCaptionState(revision, state, flag(json, "enabled"), flag(json, "canAI"),
                flag(json, "canControl"), flag(json, "original"), flag(json, "reading"),
                flag(json, "translation"), translationState, ahead, backend, vtt);
    }

    private static boolean validVttHeader(String text) {
        int start = text.startsWith("\ufeff") ? 1 : 0;
        if (!text.startsWith("WEBVTT", start)) return false;
        int end = start + 6;
        return end == text.length() || " \t\r\n".indexOf(text.charAt(end)) >= 0;
    }

    private static String text(JSONObject json, String field) throws JSONException {
        if (!json.has(field)) return "";
        if (json.get(field) instanceof String value) return value;
        throw new JSONException("Invalid caption text field");
    }

    private static long integer(JSONObject json, String field) throws JSONException {
        Object value = json.get(field);
        if (value instanceof Integer || value instanceof Long) return ((Number) value).longValue();
        throw new JSONException("Invalid caption number field");
    }

    private static boolean flag(JSONObject json, String field) throws JSONException {
        if (json.get(field) instanceof Boolean value) return value;
        throw new JSONException("Invalid caption boolean field");
    }
}
