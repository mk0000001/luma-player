package main

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestLiveCaptionSnapshotsFollowOnlyTheCurrentAuthorizedShare(t *testing.T) {
	var events []playerStatus
	p := newPlayer("Fixture", "", nil, nil, func(event playerStatus) { events = append(events, event) })
	peer := netip.MustParseAddr("192.168.4.2")
	token, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice("fixture", "Tablet", peer, token)
	p.media = &sharedMedia{revision: "current", session: &mediaSession{
		config: validatedConfig{config: config{LiveCaptions: true, DurationMS: 20_000}},
	}}
	const text = "WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nOwned fixture\n\n"
	p.setCaptionSnapshot("previous", &captionState{State: "ready", VTT: text})
	p.setCaptionSnapshot("current", &captionState{State: "ready", VTT: text})

	post := func(path, bearer, body string, want int) *httptest.ResponseRecorder {
		t.Helper()
		req := httptest.NewRequest(http.MethodPost, path, strings.NewReader(body))
		req.RemoteAddr = netip.AddrPortFrom(peer, 50000).String()
		req.Header.Set("Authorization", "Bearer "+bearer)
		response := httptest.NewRecorder()
		p.serveHTTP(response, req)
		if response.Code != want {
			t.Fatalf("%s: got %d, want %d", path, response.Code, want)
		}
		return response
	}
	const poll = `{"revision":"current","positionMs":1000,"captionRevision":0}`
	post(liveCaptionPath, "", poll, http.StatusUnauthorized)
	post(liveCaptionPath, token, `{"revision":"previous","positionMs":1000}`, http.StatusConflict)
	post(liveCaptionPath, token, `{"revision":"current","positionMs":20001}`, http.StatusBadRequest)
	post(remoteControlPath, token, `{"revision":"current","action":"caption-ai-on"}`, http.StatusForbidden)
	var reply struct {
		captionState
		Revision   int64 `json:"revision"`
		CanControl bool  `json:"canControl"`
	}
	response := post(liveCaptionPath, token, poll, http.StatusOK)
	if err := json.Unmarshal(response.Body.Bytes(), &reply); err != nil || reply.VTT != text ||
		reply.Revision != 1 || reply.CanControl || response.Header().Get("Cache-Control") != "no-store" || len(events) != 0 {
		t.Fatal("expected a visible snapshot without playback control")
	}
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteControl", Grant: "0123456789abcdef0123456789abcdef"})
	response = post(liveCaptionPath, token, `{"revision":"current","positionMs":1000,"captionRevision":1}`, http.StatusOK)
	reply.VTT = ""
	if err := json.Unmarshal(response.Body.Bytes(), &reply); err != nil || reply.VTT != "" || !reply.CanControl ||
		len(events) != 1 || events[0].Status != "captionClock" || events[0].Position != 1_000 {
		t.Fatal("unchanged cues were sent or authorized clock dispatch failed")
	}
	post(remoteControlPath, token, `{"revision":"previous","action":"caption-translation"}`, http.StatusConflict)
	post(remoteControlPath, token, `{"action":"caption-ai-on"}`, http.StatusConflict)
	post(remoteControlPath, token, `{"revision":"current","action":"caption-ai-on"}`, http.StatusAccepted)
	if len(events) != 2 || events[1].Revision != "current" || events[1].Action != "caption-ai-on" {
		t.Fatal("authorized action lost its media revision")
	}
	p.setCaptionSnapshot("current", &captionState{State: "off"})
	response = post(liveCaptionPath, token, `{"revision":"current","positionMs":1000,"captionRevision":1}`, http.StatusOK)
	if err := json.Unmarshal(response.Body.Bytes(), &reply); err != nil || reply.VTT != "" || reply.Revision != 2 {
		t.Fatal("disabling captions did not invalidate the receiver timeline")
	}
}

func TestCaptionAudioRingWrapsAndRejectsUnbufferedReads(t *testing.T) {
	var audio captionAudioBuffer
	audio.reset(10_000)
	data := make([]byte, captionAudioCapacity+64)
	for i := range data {
		data[i] = byte(i % 251)
	}
	if _, err := audio.Write(data); err != nil {
		t.Fatal(err)
	}
	got, status := audio.read(129_999, 3)
	if status != "ready" || !bytes.Equal(got, data[captionAudioCapacity-32:]) {
		t.Fatal("cross-ring PCM read is misaligned")
	}
	if _, status := audio.read(10_001, 1); status != "expired" {
		t.Fatal("overwritten audio should no longer be available")
	}
	if _, status := audio.read(130_002, 1); status != "waiting" {
		t.Fatal("audio beyond the write cursor should wait")
	}
	audio.finish()
	if _, status := audio.read(130_002, 1); status != "ended" {
		t.Fatal("audio after successful EOF should no longer wait")
	}
}

func TestBlurayRemuxCanFeedAudioFromItsExistingInput(t *testing.T) {
	fixture := os.Getenv("LUMA_TEST_BLURAY_FIXTURE")
	if fixture == "" {
		t.Skip("set LUMA_TEST_BLURAY_FIXTURE to an owned Blu-ray folder with an audio track")
	}
	ffmpeg := variantTestFFmpeg(t)
	cfg := validatedConfig{config: config{Source: fixture, SourceKind: "bluray",
		Playlist: "00000", AudioIndex: 0, AudioMode: "compatible"}}
	for _, start := range []int64{0, 64_000} {
		audio := new(captionAudioBuffer)
		audio.reset(start)
		args := variantArguments(cfg, originalProfile, start, false, blurayChapterOffset(cfg, start))
		args = append(args[:len(args)-1], "-t", "1", args[len(args)-1])
		args = append(args, captionAudioOutput(cfg, "-t", "1")...)
		ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		command := exec.CommandContext(ctx, ffmpeg, args...)
		command.Dir = filepath.Dir(ffmpeg)
		command.Stderr = audio
		hideProcess(command)
		video, err := command.Output()
		cancel()
		if err != nil {
			t.Fatalf("FFmpeg caption output: %v", err)
		}
		pcm, status := audio.read(start+250, 500)
		if len(video) == 0 || status != "ready" || len(pcm) != 500*captionPCMBytesPerMS {
			t.Fatalf("start %d: remux output bytes %d, audio bytes %d, status %s", start, len(video), len(pcm), status)
		}
	}
}
