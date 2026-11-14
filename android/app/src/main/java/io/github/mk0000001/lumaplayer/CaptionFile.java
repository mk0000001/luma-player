package io.github.mk0000001.lumaplayer;

import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.text.Subtitle;
import androidx.media3.extractor.text.SubtitleParser;
import androidx.media3.extractor.text.ssa.SsaParser;
import androidx.media3.extractor.text.subrip.SubripParser;
import androidx.media3.extractor.text.webvtt.WebvttParser;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

@UnstableApi
final class CaptionFile {
    private static final int MAX_FILE_BYTES = 4 * 1024 * 1024;

    private CaptionFile() {}

    static Subtitle read(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        for (int count; (count = input.read(buffer)) != -1; ) {
            if (output.size() + count > MAX_FILE_BYTES || Thread.currentThread().isInterrupted()) {
                throw new IOException("Caption file limit exceeded or read cancelled");
            }
            output.write(buffer, 0, count);
        }
        byte[] data = output.toByteArray();
        Charset encoding = data.length >= 2 && ((data[0] == (byte) 0xff && data[1] == (byte) 0xfe)
                || (data[0] == (byte) 0xfe && data[1] == (byte) 0xff))
                ? StandardCharsets.UTF_16 : StandardCharsets.UTF_8;
        String text = encoding.newDecoder().decode(ByteBuffer.wrap(data)).toString();
        if (text.startsWith("\ufeff")) text = text.substring(1);
        SubtitleParser parser;
        if (text.startsWith("WEBVTT")) {
            parser = new WebvttParser();
        } else if (text.startsWith("[Script Info]") || text.startsWith("[Events]")) {
            parser = new SsaParser();
        } else {
            parser = new SubripParser();
        }
        Subtitle cues = parse(parser, text);
        if (cues.getEventTimeCount() == 0) throw new IOException("No caption cues");
        return cues;
    }

    static Subtitle parseRemote(String vtt) throws IOException {
        return vtt.isEmpty() ? null : parse(new WebvttParser(), vtt);
    }

    private static Subtitle parse(SubtitleParser parser, String text) throws IOException {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_FILE_BYTES) throw new IOException("Caption text exceeds the size limit");
        try {
            return parser.parseToLegacySubtitle(bytes, 0, bytes.length);
        } catch (RuntimeException error) {
            throw new IOException("Invalid caption text", error);
        }
    }
}
