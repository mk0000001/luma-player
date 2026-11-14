package main

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"
)

func TestBlurayChapterStreamsFirstFrameAtRequestedPosition(t *testing.T) {
	fixture := os.Getenv("LUMA_TEST_BLURAY_FIXTURE")
	if fixture == "" {
		t.Skip("set LUMA_TEST_BLURAY_FIXTURE to a synthetic red, green, blue Blu-ray folder")
	}
	ffmpeg := variantTestFFmpeg(t)
	cfg := validatedConfig{config: config{Source: fixture, SourceKind: "bluray", Playlist: "00000",
		AudioMode: "compatible", ChaptersMS: []int64{0, 64000, 128000}}}
	for _, tc := range []struct {
		start    int64
		dominant int
	}{{0, 0}, {64000, 1}, {128000, 2}} {
		args := variantArguments(cfg, originalProfile, tc.start, false)
		args = append(args[:len(args)-1], "-t", "0.75", args[len(args)-1])
		stream := runFixtureFFmpeg(t, ffmpeg, nil, args...)
		pixel := runFixtureFFmpeg(t, ffmpeg, bytes.NewReader(stream), "-i", "pipe:0",
			"-frames:v", "1", "-vf", "scale=2:2,format=rgb24", "-f", "rawvideo", "pipe:1")
		if len(pixel) != 12 || pixel[tc.dominant] < 100 || pixel[(tc.dominant+1)%3] > 12 || pixel[(tc.dominant+2)%3] > 12 {
			t.Fatalf("start %d: got RGB %v, expected channel %d", tc.start, pixel, tc.dominant)
		}
	}
}

func variantTestFFmpeg(t *testing.T) string {
	t.Helper()
	if path := os.Getenv("LUMA_TEST_VARIANT_FFMPEG"); path != "" {
		resolved, err := validateFFmpeg(path)
		if err != nil {
			t.Fatal("invalid test FFmpeg path")
		}
		return resolved
	}
	t.Skip("set LUMA_TEST_VARIANT_FFMPEG to run generated FFmpeg fixtures")
	return ""
}

func runFixtureFFmpeg(t *testing.T, ffmpeg string, input io.Reader, args ...string) []byte {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	command := exec.CommandContext(ctx, ffmpeg, append([]string{"-hide_banner", "-nostdin", "-loglevel", "error"}, args...)...)
	command.Dir = filepath.Dir(ffmpeg)
	command.Stdin = input
	hideProcess(command)
	data, err := command.Output()
	if err != nil {
		if exit, ok := err.(*exec.ExitError); ok {
			t.Fatalf("synthetic FFmpeg failed: %v, %s", err, exit.Stderr)
		}
		t.Fatal(err)
	}
	return data
}

func TestVariantColorChainsUseBundledFFmpeg(t *testing.T) {
	ffmpeg := variantTestFFmpeg(t)
	for _, videoRange := range []string{"sdr", "pq", "hlg"} {
		t.Run(videoRange, func(t *testing.T) {
			if !supportsQualityEncoding(validatedConfig{config: config{FFmpeg: ffmpeg, VideoRange: videoRange}}) {
				t.Fatalf("bundled FFmpeg could not encode the generated %s fixture", videoRange)
			}
		})
	}
}

func TestHDRVariantProducesEightBitSDR(t *testing.T) {
	ffmpeg := variantTestFFmpeg(t)
	for _, tc := range []struct {
		videoRange string
		transfer   string
		sourceSize string
		outputSize string
	}{
		{"pq", "smpte2084", "2560x1440", "1920x1080"},
		{"hlg", "arib-std-b67", "1280x720", "1280x720"},
	} {
		t.Run(tc.videoRange, func(t *testing.T) {
			source := filepath.Join(t.TempDir(), "synthetic-hdr.mkv")
			input := "color=gray:s=" + tc.sourceSize + ":r=24,format=yuv420p10le,setparams=range=limited:" +
				"color_primaries=bt2020:colorspace=bt2020nc:color_trc=" + tc.transfer
			runFixtureFFmpeg(t, ffmpeg, nil, "-f", "lavfi", "-i", input, "-frames:v", "3",
				"-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p10le", source)
			cfg := validatedConfig{config: config{SourceKind: "file", Source: source, VideoRange: tc.videoRange, AudioIndex: -1}, mimeType: matroskaMIME}
			inputFile, err := os.Open(source)
			if err != nil {
				t.Fatal(err)
			}
			defer inputFile.Close()
			encoded := runFixtureFFmpeg(t, ffmpeg, inputFile, variantArguments(cfg, "1080p", 0, false)...)
			output := filepath.Join(t.TempDir(), "synthetic-sdr.mkv")
			if err := os.WriteFile(output, encoded, 0600); err != nil {
				t.Fatal(err)
			}
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			command := exec.CommandContext(ctx, ffmpeg, "-hide_banner", "-nostdin", "-i", output, "-frames:v", "1", "-f", "null", "-")
			hideProcess(command)
			diagnostics, err := command.CombinedOutput()
			if err != nil {
				t.Fatal(err)
			}
			for _, line := range strings.Split(string(diagnostics), "\n") {
				if strings.Contains(line, "Stream #0:0") && strings.Contains(line, "Video: h264") &&
					strings.Contains(line, "yuv420p(tv, bt709") && !strings.Contains(line, tc.transfer) &&
					strings.Contains(line, ", "+tc.outputSize+" ") {
					return
				}
			}
			t.Fatalf("converted fixture did not report %s H.264 8-bit BT.709: %s", tc.outputSize, diagnostics)
		})
	}
}

func TestFileVariantAccurateTranscodeAndKeyframeCopy(t *testing.T) {
	ffmpeg := variantTestFFmpeg(t)
	const width, height, rate, frames = 64, 64, 10, 50
	const lumaSize = width * height
	var sourceFrames []byte
	for frame := 0; frame < frames; frame++ {
		sourceFrames = append(sourceFrames, bytes.Repeat([]byte{byte(16 + frame*3)}, lumaSize)...)
		sourceFrames = append(sourceFrames, bytes.Repeat([]byte{128}, lumaSize/2)...)
	}
	source := filepath.Join(t.TempDir(), "synthetic-position.mkv")
	runFixtureFFmpeg(t, ffmpeg, bytes.NewReader(sourceFrames),
		"-f", "rawvideo", "-pixel_format", "yuv420p", "-video_size", "64x64", "-framerate", "10", "-i", "pipe:0",
		"-c:v", "libx264", "-g", "20", "-keyint_min", "20", "-sc_threshold", "0", "-bf", "0", "-crf", "10", source)
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "pairing.dpapi"))
	paired := host.pair(t, false, "once")
	media := fileConfig(t)
	media.Source = source
	media.FFmpeg = ffmpeg
	media.VideoRange = "sdr"
	media.DurationMS = frames * 1000 / rate
	host.control(t, map[string]any{"command": "share", "media": media})
	host.event(t, "sharing")
	var meta sessionMetadata
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusOK, &meta)
	if !meta.Seekable || !meta.Restartable || meta.VideoRange != "sdr" ||
		!slices.Equal(meta.Profiles, []string{"original", "1080p", "720p", "480p"}) {
		t.Fatal("generated file did not advertise working conversion profiles")
	}
	host.request(t, http.MethodHead, "/v1/media/variant/480p/2300", paired.Token, nil, http.StatusOK, nil)
	host.request(t, http.MethodGet, "/v1/media/variant/480p/5000", paired.Token, nil, http.StatusRequestedRangeNotSatisfiable, nil)

	for _, tc := range []struct {
		profile string
		frame   int
	}{
		{"480p", 23},
		{"original", 20},
	} {
		t.Run(tc.profile, func(t *testing.T) {
			body := host.request(t, http.MethodGet, "/v1/media/variant/"+tc.profile+"/2300", paired.Token, nil, http.StatusOK, nil)
			output := filepath.Join(t.TempDir(), "variant.mkv")
			if err := os.WriteFile(output, []byte(body), 0600); err != nil {
				t.Fatal(err)
			}
			first := runFixtureFFmpeg(t, ffmpeg, nil, "-i", output, "-map", "0:v:0", "-frames:v", "1",
				"-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:1")
			if len(first) != lumaSize*3/2 {
				t.Fatalf("wrong decoded fixture frame length %d", len(first))
			}
			want := 16 + tc.frame*3
			if actual := int(first[lumaSize/2]); actual < want-1 || actual > want+1 {
				t.Fatalf("first frame luma %d, expected %d (source frame %d)", actual, want, tc.frame)
			}
		})
	}
}
