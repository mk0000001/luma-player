package io.github.mk0000001.lumaplayer;

enum PlaybackLayout {
    VIDEO_ONLY, STACKED, SIDE_BY_SIDE, COMPACT_LANDSCAPE;

    static PlaybackLayout forWindow(int widthDp, int heightDp, boolean videoOnly) {
        if (videoOnly) {
            return VIDEO_ONLY;
        }
        boolean nearSquare = widthDp >= 600 && widthDp * 5L < heightDp * 6L;
        if (widthDp <= heightDp || nearSquare) {
            return STACKED;
        }
        return widthDp >= 720 && heightDp >= 420 ? SIDE_BY_SIDE : COMPACT_LANDSCAPE;
    }
}
