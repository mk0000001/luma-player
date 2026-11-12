package io.github.mk0000001.lumaplayer;

import java.util.Arrays;
import org.json.JSONException;
import org.json.JSONObject;

final class RemotePlaybackOptions {
    final long revision;
    final String profile;
    final String hdr;
    final Integer skipSeconds;
    final Integer subtitleDelayMs;
    final Boolean fill;

    private RemotePlaybackOptions(long revision, String profile, String hdr, Integer skipSeconds,
                                  Integer subtitleDelayMs, Boolean fill) {
        this.revision = revision;
        this.profile = profile;
        this.hdr = hdr;
        this.skipSeconds = skipSeconds;
        this.subtitleDelayMs = subtitleDelayMs;
        this.fill = fill;
    }

    static boolean isProfile(String value) {
        return Arrays.asList("original", "1080p", "720p", "480p").contains(value);
    }

    static RemotePlaybackOptions parse(JSONObject object) throws JSONException {
        Object revisionValue = object.get("revision");
        if (!(revisionValue instanceof Integer) && !(revisionValue instanceof Long)) {
            throw new JSONException("Invalid playback options revision");
        }
        long revision = ((Number) revisionValue).longValue();
        Object profile = object.opt("profile");
        Object hdr = object.opt("hdr");
        Object fill = object.opt("fill");
        Integer skip = optionalInt(object, "skipSeconds");
        Integer delay = optionalInt(object, "subtitleDelayMs");
        if (revision < 0 || (profile != null && (!(profile instanceof String)
                || (!"auto".equals(profile) && !isProfile((String) profile))))
                || (hdr != null && (!(hdr instanceof String) || !Arrays.asList("auto", "original", "sdr").contains(hdr)))
                || (skip != null && !Arrays.asList(5, 10, 15, 30).contains(skip))
                || (delay != null && (delay < -5000 || delay > 5000))
                || (fill != null && !(fill instanceof Boolean))) {
            throw new JSONException("Invalid remote playback options");
        }
        return new RemotePlaybackOptions(revision, (String) profile, (String) hdr, skip, delay, (Boolean) fill);
    }

    private static Integer optionalInt(JSONObject object, String name) throws JSONException {
        Object value = object.opt(name);
        if (value != null && !(value instanceof Integer)) {
            throw new JSONException("Invalid playback option: " + name);
        }
        return (Integer) value;
    }
}
