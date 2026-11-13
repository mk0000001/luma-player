package io.github.mk0000001.lumaplayer;

import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.Extractor;
import androidx.media3.extractor.ExtractorInput;
import androidx.media3.extractor.ExtractorOutput;
import androidx.media3.extractor.ForwardingExtractorOutput;
import androidx.media3.extractor.PositionHolder;
import androidx.media3.extractor.SeekMap;
import androidx.media3.extractor.SeekPoint;
import androidx.media3.extractor.mkv.MatroskaExtractor;
import androidx.media3.extractor.text.DefaultSubtitleParserFactory;
import java.io.IOException;

/** Gives Media3 the title duration so it can reuse its queued samples on a nearby seek. */
@UnstableApi
final class BufferedMatroskaExtractor implements Extractor {
    private final MatroskaExtractor delegate = new MatroskaExtractor(new DefaultSubtitleParserFactory(),
            MatroskaExtractor.FLAG_DISABLE_SEEK_FOR_CUES);
    private final long durationUs;

    BufferedMatroskaExtractor(long durationMs) {
        durationUs = durationMs * 1000;
    }

    @Override public boolean sniff(ExtractorInput input) throws IOException {
        return delegate.sniff(input);
    }

    @Override public void init(ExtractorOutput output) {
        delegate.init(new ForwardingExtractorOutput(output) {
            @Override public void seekMap(SeekMap map) {
                super.seekMap(new SeekMap() {
                    @Override public boolean isSeekable() { return true; }
                    @Override public long getDurationUs() { return durationUs; }
                    @Override public SeekPoints getSeekPoints(long timeUs) {
                        // A seek outside queued samples must fail the byte-range request.
                        // MainActivity then reopens the stream at the requested media time.
                        return new SeekPoints(new SeekPoint(timeUs, 1));
                    }
                });
            }
        });
    }

    @Override public int read(ExtractorInput input, PositionHolder seek) throws IOException {
        return delegate.read(input, seek);
    }

    @Override public void seek(long position, long timeUs) {
        delegate.seek(position, timeUs);
    }

    @Override public void release() {
        delegate.release();
    }
}
