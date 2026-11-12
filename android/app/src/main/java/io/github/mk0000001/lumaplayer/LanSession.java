package io.github.mk0000001.lumaplayer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class LanSession {
    private static final String DISC_MIME = "video/x-matroska";
    private static final Pattern FILE_MIME = Pattern.compile("(?:audio|video)/[a-z0-9!#$&^_.+-]{1,80}");

    final String title;
    final String mimeType;
    final String sourceKind;
    final long durationMs;
    final boolean seekable;
    final List<Subtitle> subtitles;

    private LanSession(String title, String mimeType, String sourceKind, long durationMs,
                       boolean seekable, List<Subtitle> subtitles) {
        this.title = title;
        this.mimeType = mimeType;
        this.sourceKind = sourceKind;
        this.durationMs = durationMs;
        this.seekable = seekable;
        this.subtitles = Collections.unmodifiableList(subtitles);
    }

    boolean isBluRay() {
        return "bluray".equals(sourceKind);
    }

    static LanSession parse(String json) throws IOException {
        try {
            JSONObject session = new JSONObject(json);
            if (!Integer.valueOf(1).equals(session.get("version"))) {
                throw new JSONException("Unsupported session version");
            }
            String title = string(session, "title", 512);
            String mimeType = string(session, "mimeType", 100).toLowerCase(Locale.ROOT);
            String sourceKind = string(session, "sourceKind", 16);
            Object seekableValue = session.get("seekable");
            Object durationValue = session.get("durationMs");
            if (!(seekableValue instanceof Boolean)
                    || (!(durationValue instanceof Integer) && !(durationValue instanceof Long))) {
                throw new JSONException("Invalid session playback properties");
            }
            boolean seekable = (Boolean) seekableValue;
            long durationMs = ((Number) durationValue).longValue();
            if (durationMs < -1 || !supportsSource(sourceKind, mimeType, seekable)) {
                throw new JSONException("Unsupported session source");
            }
            return new LanSession(title, mimeType, sourceKind, durationMs, seekable,
                    parseSubtitles(session.getJSONArray("subtitles")));
        } catch (JSONException | IllegalArgumentException e) {
            throw new IOException("Invalid LAN session metadata", e);
        }
    }

    private static boolean supportsSource(String sourceKind, String mimeType, boolean seekable) {
        if ("bluray".equals(sourceKind)) {
            return DISC_MIME.equals(mimeType) && !seekable;
        }
        return "file".equals(sourceKind)
                && (FILE_MIME.matcher(mimeType).matches() || "application/octet-stream".equals(mimeType));
    }

    private static List<Subtitle> parseSubtitles(JSONArray tracks) throws JSONException {
        if (tracks.length() > 64) {
            throw new JSONException("Too many subtitle tracks");
        }
        List<Subtitle> subtitles = new ArrayList<>();
        Set<String> ids = new HashSet<>();
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject track = tracks.getJSONObject(i);
            String id = string(track, "id", 64);
            String path = string(track, "path", 96);
            if (!ids.add(id) || !LanEndpoint.subtitlePath(id).equals(path)) {
                throw new JSONException("Invalid subtitle path or duplicate ID");
            }
            subtitles.add(new Subtitle(id, string(track, "label", 256), string(track, "language", 35), path));
        }
        return subtitles;
    }

    private static String string(JSONObject object, String name, int maxLength) throws JSONException {
        Object value = object.get(name);
        if (!(value instanceof String) || ((String) value).length() > maxLength) {
            throw new JSONException("Invalid session text field: " + name);
        }
        return (String) value;
    }

    static final class Subtitle {
        final String id;
        final String label;
        final String language;
        final String path;

        Subtitle(String id, String label, String language, String path) {
            this.id = id;
            this.label = label;
            this.language = language;
            this.path = path;
        }
    }
}
