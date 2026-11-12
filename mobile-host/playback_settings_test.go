package main

import (
	"net/http"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func TestPlayerOriginalFallbackAndShiftedSubtitleRoutes(t *testing.T) {
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "pairing.dpapi"))
	paired := host.pair(t, false, "once")
	media := fileConfig(t)
	media.DurationMS = 4000
	media.ChaptersMS = []int64{0, 2000, 2000, 3000}
	media.FFmpeg = "invalid-relative-ffmpeg.exe"
	media.Subtitles[0].VTT = "\ufeffWEBVTT\r\n\r\n00:00.100 --> 00:00.900\r\nold\r\n\r\n" +
		"crossing\r\n00:00.800 --> 00:02.000 line:90%\r\ntext<00:01.200> now\r\n\r\n" +
		"00:02.500 --> 00:03.500\r\nlater\r\n"
	host.control(t, map[string]any{"command": "share", "media": media})
	host.event(t, "sharing")
	var meta sessionMetadata
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusOK, &meta)
	if !meta.Seekable || meta.Restartable || meta.VideoRange != "unknown" ||
		!slices.Equal(meta.Profiles, []string{"original"}) || !slices.Equal(meta.ChaptersMS, []int64{0, 2000, 3000}) {
		t.Fatalf("incorrect fallback metadata: %+v", meta)
	}
	if body := host.request(t, http.MethodGet, "/v1/media/variant/original/0", paired.Token, nil, http.StatusOK, nil); body != fileFixture {
		t.Fatal("the original variant changed file bytes")
	}
	host.request(t, http.MethodHead, "/v1/media/variant/720p/0", "", nil, http.StatusUnauthorized, nil)
	host.request(t, http.MethodHead, "/v1/media/variant/720p/0", paired.Token, nil, http.StatusNotFound, nil)
	host.request(t, http.MethodGet, "/v1/media/variant/original/1000", paired.Token, nil, http.StatusRequestedRangeNotSatisfiable, nil)
	for _, path := range []string{
		"/v1/media/variant/original/-1", "/v1/media/variant/original/01",
		"/v1/subtitle-at/-0/ko-1.vtt", "/v1/subtitle-at/+1/ko-1.vtt", "/v1/subtitle-at/1/no.vtt",
	} {
		host.request(t, http.MethodGet, path, paired.Token, nil, http.StatusNotFound, nil)
	}
	for _, path := range []string{"/v1/subtitle-at/-5001/ko-1.vtt", "/v1/subtitle-at/9001/ko-1.vtt"} {
		host.request(t, http.MethodGet, path, paired.Token, nil, http.StatusRequestedRangeNotSatisfiable, nil)
	}
	shifted := host.request(t, http.MethodGet, "/v1/subtitle-at/1000/ko-1.vtt", paired.Token, nil, http.StatusOK, nil)
	if strings.Contains(shifted, "old") ||
		!strings.Contains(shifted, "crossing\n00:00:00.000 --> 00:00:01.000 line:90%\ntext<00:00:00.200> now") ||
		!strings.Contains(shifted, "00:00:01.500 --> 00:00:02.500\nlater") {
		t.Fatal("crossing and later subtitle cues were not trimmed and rebased")
	}
	delayed := host.request(t, http.MethodGet, "/v1/subtitle-at/-500/ko-1.vtt", paired.Token, nil, http.StatusOK, nil)
	if !strings.Contains(delayed, "00:00:00.600 --> 00:00:01.400\nold") {
		t.Fatal("negative origin discarded an early subtitle instead of delaying it")
	}
	if original := host.request(t, http.MethodGet, "/v1/subtitle/ko-1.vtt", paired.Token, nil, http.StatusOK, nil); original != media.Subtitles[0].VTT {
		t.Fatal("shifting modified the original subtitle")
	}
}

func TestPlayerDeviceOptionsArePrivateRevisionedAndRevoked(t *testing.T) {
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "pairing.dpapi"))
	first := host.pair(t, true, "remember")
	const secondID = "EBESExQVFhcYGRobHB0eHw"
	second := host.pairDevice(t, secondID, false, "once")
	host.control(t, map[string]any{"command": "share", "media": fileConfig(t)})
	host.event(t, "sharing")
	host.control(t, map[string]any{
		"command": "setDeviceOptions", "id": testPlayerDeviceID,
		"options": map[string]any{"profile": "720p", "hdr": "sdr", "skipSeconds": 15, "subtitleDelayMs": -850, "fill": false},
	})
	event := host.event(t, "devices")
	if event.Devices == nil {
		t.Fatal("no device settings listing")
	}
	for _, entry := range *event.Devices {
		if (entry.ID == testPlayerDeviceID) != (entry.Options != nil) {
			t.Fatal("settings were attached to the wrong device")
		}
	}
	var meta sessionMetadata
	host.request(t, http.MethodGet, "/v1/session", first.Token, nil, http.StatusOK, &meta)
	opts := meta.Options
	if opts == nil || opts.Revision <= 0 || opts.Profile != "720p" || opts.HDR != "sdr" ||
		opts.SkipSeconds == nil || *opts.SkipSeconds != 15 || opts.SubtitleDelayMS == nil || *opts.SubtitleDelayMS != -850 ||
		opts.Fill == nil || *opts.Fill {
		t.Fatal("configured options were missing or changed")
	}
	meta = sessionMetadata{}
	host.request(t, http.MethodGet, "/v1/session", second.Token, nil, http.StatusOK, &meta)
	if meta.Options != nil {
		t.Fatal("another device received private options")
	}
	host.control(t, map[string]any{
		"command": "setDeviceOptions", "id": testPlayerDeviceID, "options": map[string]int{"skipSeconds": 12},
	})
	if event := host.event(t, "error"); event.Command != "setDeviceOptions" {
		t.Fatal("invalid options did not return the matching command error")
	}
	host.control(t, map[string]any{
		"command": "setDeviceOptions", "id": testPlayerDeviceID, "options": map[string]any{},
	})
	host.event(t, "devices")
	meta = sessionMetadata{}
	host.request(t, http.MethodGet, "/v1/session", first.Token, nil, http.StatusOK, &meta)
	if meta.Options == nil || meta.Options.Revision <= opts.Revision || meta.Options.Profile != "" || meta.Options.Fill != nil {
		t.Fatal("empty options did not clear the fields and advance revision")
	}
	host.control(t, map[string]string{"command": "disconnectDevice", "id": testPlayerDeviceID})
	host.event(t, "devices")
	host.request(t, http.MethodGet, "/v1/session", first.Token, nil, http.StatusUnauthorized, nil)
	var login struct{ Token string }
	host.request(t, http.MethodPost, "/v1/pair/login", first.RememberToken,
		map[string]string{"deviceId": testPlayerDeviceID}, http.StatusOK, &login)
	host.event(t, "deviceConnected")
	meta = sessionMetadata{}
	host.request(t, http.MethodGet, "/v1/session", login.Token, nil, http.StatusOK, &meta)
	if meta.Options != nil {
		t.Fatal("revocation did not clear device options")
	}
}
