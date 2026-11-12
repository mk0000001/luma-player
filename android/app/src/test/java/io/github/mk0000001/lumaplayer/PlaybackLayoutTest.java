package io.github.mk0000001.lumaplayer;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class PlaybackLayoutTest {
    @Test
    public void keepsChaptersBelowVideoInPortraitAndNearSquareWindows() {
        assertEquals(PlaybackLayout.STACKED, PlaybackLayout.forWindow(360, 760, false));
        assertEquals(PlaybackLayout.STACKED, PlaybackLayout.forWindow(800, 768, false));
    }

    @Test
    public void givesWideTabletsASidePaneAndKeepsCompactLandscapePlayable() {
        assertEquals(PlaybackLayout.SIDE_BY_SIDE, PlaybackLayout.forWindow(1024, 680, false));
        assertEquals(PlaybackLayout.SIDE_BY_SIDE, PlaybackLayout.forWindow(1280, 720, false));
        assertEquals(PlaybackLayout.COMPACT_LANDSCAPE, PlaybackLayout.forWindow(740, 360, false));
    }

    @Test
    public void fullscreenAndPipKeepOnlyVideo() {
        assertEquals(PlaybackLayout.VIDEO_ONLY, PlaybackLayout.forWindow(1024, 680, true));
        assertEquals(PlaybackLayout.VIDEO_ONLY, PlaybackLayout.forWindow(360, 760, true));
    }
}
