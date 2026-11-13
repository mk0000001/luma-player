package io.github.mk0000001.lumaplayer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class LanSession {
    static final String DISC_MIME = "video/x-matroska";
    private static final long MAX_RESTARTABLE_DURATION_MS = 999_990_000;

    final String title;
    final String mediaRevision;
    final String mimeType;
    final String sourceKind;
    final long durationMs;
    final boolean seekable;
    final List<Subtitle> subtitles;
    final boolean restartable;
    final boolean supportsCaptionOffset;
    final List<String> profiles;
    final List<Long> chaptersMs;
    final List<String> chapterTitles;
    final String videoRange;
    final RemotePlaybackOptions options;

    private LanSession(String title, String mimeType, String sourceKind, long durationMs,
                       boolean seekable, List<Subtitle> subtitles, boolean restartable, boolean supportsCaptionOffset,
                       List<String> profiles, List<Long> chaptersMs, String videoRange,
                       RemotePlaybackOptions options, String mediaRevision, List<String> chapterTitles) {
        this.title = title;
        this.mediaRevision = mediaRevision;
        this.mimeType = mimeType;
        this.sourceKind = sourceKind;
        this.durationMs = durationMs;
        this.seekable = seekable;
        this.subtitles = Collections.unmodifiableList(subtitles);
        this.restartable = restartable;
        this.supportsCaptionOffset = supportsCaptionOffset;
        this.profiles = Collections.unmodifiableList(profiles);
        this.chaptersMs = Collections.unmodifiableList(chaptersMs);
        this.chapterTitles = Collections.unmodifiableList(chapterTitles);
        this.videoRange = videoRange;
        this.options = options;
    }

    boolean isBluRay() {
        return "bluray".equals(sourceKind);
    }

    static LanSession parse(String json) throws IOException {
        try {
            JSONObject session = LanJson.parseObject(json);
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
            if (durationMs < -1 || durationMs > Long.MAX_VALUE / 1000 || !supportsSource(sourceKind, mimeType, seekable)) {
                throw new JSONException("Unsupported session source");
            }
            boolean restartable = session.has("restartable") && booleanField(session, "restartable");
            if (restartable && (durationMs <= 0 || durationMs >= MAX_RESTARTABLE_DURATION_MS)) {
                throw new JSONException("Invalid restartable timeline");
            }
            List<String> profiles = parseProfiles(session);
            if (!restartable && profiles.size() > 1) {
                throw new JSONException("Alternate stream profiles require a restartable timeline");
            }
            String videoRange = session.has("videoRange") ? string(session, "videoRange", 8) : "unknown";
            if (!Arrays.asList("sdr", "pq", "hlg", "unknown").contains(videoRange)) {
                throw new JSONException("Invalid video range");
            }
            var chapters = parseChapters(session, durationMs);
            return new LanSession(title, mimeType, sourceKind, durationMs, seekable,
                    parseSubtitles(session.getJSONArray("subtitles")), restartable, session.has("profiles"),
                    profiles, chapters, videoRange,
                    session.has("options") ? RemotePlaybackOptions.parse(session.getJSONObject("options")) : null,
                    session.has("mediaRevision") ? string(session, "mediaRevision", 64) : "",
                    parseChapterTitles(session, chapters.size()));
        } catch (JSONException | IllegalArgumentException e) {
            throw new IOException("Invalid LAN session metadata", e);
        }
    }

    private static List<String> parseChapterTitles(JSONObject session, int count) throws JSONException {
        if (!session.has("chapterTitles")) return Collections.nCopies(count, "");
        var labels = session.getJSONArray("chapterTitles");
        if (labels.length() != count) throw new JSONException("Invalid chapter title count");
        List<String> result = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            if (!(labels.get(i) instanceof String title) || title.length() > 160)
                throw new JSONException("Invalid chapter title");
            result.add(title);
        }
        return result;
    }

    private static boolean booleanField(JSONObject object, String name) throws JSONException {
        Object value = object.get(name);
        if (!(value instanceof Boolean)) {
            throw new JSONException("Invalid boolean field: " + name);
        }
        return (Boolean) value;
    }

    private static List<String> parseProfiles(JSONObject session) throws JSONException {
        if (!session.has("profiles")) {
            return Collections.singletonList("original");
        }
        JSONArray values = session.getJSONArray("profiles");
        if (values.length() == 0 || values.length() > 4) {
            throw new JSONException("Invalid stream profiles");
        }
        List<String> result = new ArrayList<>();
        for (int i = 0; i < values.length(); i++) {
            Object value = values.get(i);
            if (!(value instanceof String) || !RemotePlaybackOptions.isProfile((String) value)
                    || result.contains(value)) {
                throw new JSONException("Invalid stream profile");
            }
            result.add((String) value);
        }
        if (!result.contains("original")) {
            throw new JSONException("Missing original stream profile");
        }
        return result;
    }

    private static List<Long> parseChapters(JSONObject session, long durationMs) throws JSONException {
        if (!session.has("chaptersMs")) {
            return Collections.emptyList();
        }
        JSONArray values = session.getJSONArray("chaptersMs");
        if (values.length() > 512) {
            throw new JSONException("Too many chapters");
        }
        List<Long> result = new ArrayList<>();
        long previous = -1;
        for (int i = 0; i < values.length(); i++) {
            Object value = values.get(i);
            if (!(value instanceof Integer) && !(value instanceof Long)) {
                throw new JSONException("Invalid chapter timestamp");
            }
            long time = ((Number) value).longValue();
            if (time <= previous || time >= durationMs) {
                throw new JSONException("Chapter outside the timeline");
            }
            result.add(time);
            previous = time;
        }
        return result;
    }

    private static boolean supportsSource(String sourceKind, String mimeType, boolean seekable) {
        if ("bluray".equals(sourceKind)) {
            return DISC_MIME.equals(mimeType) && !seekable;
        }
        return "file".equals(sourceKind) && supportsFileMime(mimeType);
    }

    static boolean supportsFileMime(String mimeType) {
        return switch (mimeType) {
            case DISC_MIME, "video/mp4", "video/webm", "video/quicktime", "video/x-msvideo",
                    "video/mp2t", "audio/mp4", "audio/flac", "audio/mpeg", "audio/wav",
                    "application/octet-stream" -> true;
            default -> false;
        };
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
