package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"testing"
	"time"
)

// The fixture contains an EBML header, an open Segment, Info and an open Cluster.
var streamFixture = []byte{
	0x1a, 0x45, 0xdf, 0xa3, 0x80,
	0x18, 0x53, 0x80, 0x67, 0xff,
	0x15, 0x49, 0xa9, 0x66, 0x81, 0x00,
	0x1f, 0x43, 0xb6, 0x75, 0xff,
}

type workerObservation struct {
	PID  int
	Args []string
}

func TestMain(m *testing.M) {
	statePath := os.Getenv("LUMA_TEST_FFMPEG_STATE")
	if statePath != "" && len(os.Args) > 1 && os.Args[1] == "-hide_banner" {
		if slices.Contains(os.Args, "-c:a") && !slices.Contains(os.Args, "aac") && !slices.Contains(os.Args, "pcm_s24le") {
			_, _ = os.Stdout.Write(streamFixture[:16])
			fmt.Fprintln(os.Stderr, "[matroska] No wav codec tag found for codec pcm_bluray")
			os.Exit(1)
		}
		data, err := json.Marshal(workerObservation{PID: os.Getpid(), Args: os.Args[1:]})
		if err != nil || os.WriteFile(statePath, data, 0600) != nil {
			os.Exit(2)
		}
		if _, err := os.Stdout.Write(streamFixture); err != nil {
			os.Exit(2)
		}
		for {
			time.Sleep(time.Hour)
		}
	}
	os.Exit(m.Run())
}

func discConfig(t *testing.T) (config, string) {
	t.Helper()
	cfg := fileConfig(t)
	cfg.SourceKind = "bluray"
	cfg.Source = t.TempDir()
	cfg.Playlist = "00200"
	pid := 4352
	cfg.AudioTrackID = &pid
	playlistDir := filepath.Join(cfg.Source, "BDMV", "PLAYLIST")
	if err := os.MkdirAll(playlistDir, 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(playlistDir, cfg.Playlist+".mpls"), []byte("synthetic playlist"), 0600); err != nil {
		t.Fatal(err)
	}
	self, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	cfg.FFmpeg = self
	statePath := filepath.Join(t.TempDir(), "worker.json")
	t.Setenv("LUMA_TEST_FFMPEG_STATE", statePath)
	return cfg, statePath
}

func observeWorker(t *testing.T, statePath string) workerObservation {
	t.Helper()
	data, err := os.ReadFile(statePath)
	if err != nil {
		t.Fatal(err)
	}
	var worker workerObservation
	if err := json.Unmarshal(data, &worker); err != nil {
		t.Fatal(err)
	}
	return worker
}

func TestDiscOriginalRemuxRetriesPCMAndJoinsWorkerOnEOF(t *testing.T) {
	cfg, statePath := discConfig(t)
	discPath, err := filepath.EvalSymlinks(cfg.Source)
	if err != nil {
		t.Fatal(err)
	}
	host := startTestHost(t, cfg)
	client := host.client(t, host.pin)
	auth := "Bearer " + host.token
	head := host.request(t, client, http.MethodHead, "/v1/media", auth, "")
	if head.Header.Get("Accept-Ranges") != "none" || head.Header.Get("Content-Type") != "video/x-matroska" {
		t.Fatal("HEAD did not report a progressive Matroska stream")
	}
	readResponse(t, head, http.StatusOK)
	if _, err := os.Stat(statePath); !os.IsNotExist(err) {
		t.Fatal("HEAD started an FFmpeg worker")
	}
	response := host.request(t, client, http.MethodGet, "/v1/session", auth, "")
	var meta struct {
		MimeType, SourceKind string
		Seekable             bool
	}
	if err := json.Unmarshal([]byte(readResponse(t, response, http.StatusOK)), &meta); err != nil {
		t.Fatal(err)
	}
	if meta.SourceKind != "bluray" || meta.MimeType != "video/x-matroska" || meta.Seekable {
		t.Fatal("incorrect Blu-ray session metadata")
	}
	response = host.request(t, client, http.MethodGet, "/v1/media", auth, "")
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("stream start status = %d", response.StatusCode)
	}
	data := make([]byte, len(streamFixture))
	if _, err := io.ReadFull(response.Body, data); err != nil || !bytes.Equal(data, streamFixture) {
		t.Fatal("partial failed Matroska header reached the stream")
	}
	worker := observeWorker(t, statePath)
	args := strings.Join(worker.Args, " ")
	for _, want := range []string{
		"-playlist 200", "-i bluray:" + filepath.ToSlash(discPath),
		"-map 0:v:0 -map 0:a:i:4352", "-c:v copy -c:a pcm_s24le", "-sn -dn", "-f matroska -live 1",
	} {
		if !strings.Contains(args, want) {
			t.Fatalf("worker arguments lack %q: %v", want, worker.Args)
		}
	}
	process, err := os.FindProcess(worker.PID)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = process.Kill()
		_ = process.Release()
	})
	host.stop()
	if runtime.GOOS == "windows" {
		exited := make(chan error, 1)
		go func() {
			_, err := process.Wait()
			exited <- err
		}()
		select {
		case err := <-exited:
			if err != nil {
				t.Fatal(err)
			}
		case <-time.After(time.Second):
			t.Fatal("FFmpeg process was still alive after host exit")
		}
	}
}

func TestDiscCompatibleAudioReopensAfterDisconnect(t *testing.T) {
	cfg, statePath := discConfig(t)
	cfg.AudioMode = "compatible"
	cfg.AudioTrackID = nil
	cfg.AudioIndex = 1
	host := startTestHost(t, cfg)
	defer host.stop()
	client := host.client(t, host.pin)
	auth := "Bearer " + host.token

	response := host.request(t, client, http.MethodGet, "/v1/media", auth, "")
	if response.StatusCode != http.StatusOK {
		response.Body.Close()
		t.Fatalf("stream start status = %d", response.StatusCode)
	}
	first := observeWorker(t, statePath)
	args := strings.Join(first.Args, " ")
	if !strings.Contains(args, "-map 0:v:0 -map 0:a:1") || !strings.Contains(args, "-c:v copy -c:a aac -ac 2") {
		t.Fatalf("compatible remux args are incorrect: %v", first.Args)
	}
	busy := host.request(t, client, http.MethodGet, "/v1/media", auth, "")
	readResponse(t, busy, http.StatusServiceUnavailable)
	if current := observeWorker(t, statePath); first.PID != current.PID {
		t.Fatal("a concurrent request replaced the active worker")
	}
	_ = response.Body.Close()

	response = host.request(t, client, http.MethodGet, "/v1/media", auth, "")
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("replacement stream status = %d", response.StatusCode)
	}
	if second := observeWorker(t, statePath); first.PID == second.PID {
		t.Fatal("new connection did not open a new worker")
	}
}

type fullStreamReader struct{}

func (fullStreamReader) Read(data []byte) (int, error) {
	return len(data), nil
}

func BenchmarkStreamingRead(b *testing.B) {
	reader := &idleMediaReader{source: fullStreamReader{}, onIdle: func() {}}
	buffer := make([]byte, 32*1024)
	b.ReportAllocs()
	for b.Loop() {
		if _, err := reader.Read(buffer); err != nil {
			b.Fatal(err)
		}
	}
}
