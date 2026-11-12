package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestReceiverRejectsUnassignedOrNonPrivateLocalBindings(t *testing.T) {
	for _, local := range []string{"8.8.8.8", "127.0.0.1", "fd00::1", "0.0.0.0", "10.0.0.1:48762"} {
		var output bytes.Buffer
		cfg := initialConfig{Endpoint: "192.168.7.9:48762", LocalAddress: local,
			Name: "Luma fixture", Store: filepath.Join(t.TempDir(), "receiver.dpapi")}
		code := runReceiver(bufio.NewReader(strings.NewReader("")), &output, cfg, nil)
		var status receiverStatus
		if code != 1 || json.Unmarshal(output.Bytes(), &status) != nil ||
			status.Status != "error" || status.Message != errInvalidLAN.Error() {
			t.Fatalf("unexpected result for local binding %q: %d %s", local, code, output.String())
		}
	}
}

type receiverTestHost struct {
	commands *json.Encoder
	events   <-chan receiverStatus
	stop     func()
}

func startReceiverTestHost(t *testing.T, remote playerTestHost, store string, remember, fresh bool) receiverTestHost {
	t.Helper()
	input, commands := io.Pipe()
	events, output := io.Pipe()
	statuses := make(chan receiverStatus, 8)
	exitCode := make(chan int, 1)
	dialer := &net.Dialer{Timeout: 2 * time.Second}
	go func() {
		exitCode <- runWithServices(input, output, playerServices{
			receiverDial: func(ctx context.Context, network, address string) (net.Conn, error) {
				if network != "tcp4" || address != remote.endpoint {
					return nil, errPeerResponse
				}
				return dialer.DialContext(ctx, network, strings.TrimPrefix(remote.url, "https://"))
			},
		})
		_ = output.Close()
		_ = input.Close()
	}()
	go func() {
		defer close(statuses)
		decoder := json.NewDecoder(events)
		for {
			var status receiverStatus
			if decoder.Decode(&status) != nil {
				return
			}
			statuses <- status
		}
	}()
	var once sync.Once
	host := receiverTestHost{
		commands: json.NewEncoder(commands), events: statuses,
		stop: func() {
			once.Do(func() {
				_ = commands.Close()
				select {
				case code := <-exitCode:
					if code != 0 {
						t.Errorf("receiver exit code = %d", code)
					}
				case <-time.After(5 * time.Second):
					t.Error("receiver did not stop on stdin EOF")
				}
				_ = events.Close()
			})
		},
	}
	t.Cleanup(host.stop)
	if err := host.commands.Encode(map[string]any{
		"mode": "receiver", "endpoint": remote.endpoint, "name": "Receiver PC",
		"store": filepath.ToSlash(store), "remember": remember, "fresh": fresh,
	}); err != nil {
		t.Fatal(err)
	}
	return host
}

func (host receiverTestHost) event(t *testing.T, expected string) receiverStatus {
	t.Helper()
	select {
	case status := <-host.events:
		if status.Status != expected {
			t.Fatalf("expected receiver status %q, got %q (%s)", expected, status.Status, status.Message)
		}
		return status
	case <-time.After(7 * time.Second):
		t.Fatalf("timed out waiting for receiver status %q", expected)
		return receiverStatus{}
	}
}

func TestReceiverConfirmsBeforeSavingAndPlaysThroughExactLoopbackRoutes(t *testing.T) {
	remote := startPlayerTestHost(t, filepath.Join(t.TempDir(), "player.dpapi"))
	cfg := fileConfig(t)
	cfg.Title = "합성\n미디어"
	remote.control(t, map[string]any{"command": "share", "media": cfg})
	remote.event(t, "sharing")

	store := filepath.Join(t.TempDir(), "receiver.dpapi")
	receiver := startReceiverTestHost(t, remote, store, true, false)
	code := receiver.event(t, "pairCode")
	prompt := remote.event(t, "pairRequest")
	if code.Code != prompt.Code || len(code.Code) != 6 || code.Name != "테스트 PC" {
		t.Fatal("PCs did not show the same pairing code")
	}
	remote.control(t, map[string]string{"command": "decide", "id": prompt.ID, "decision": "remember"})
	connected := remote.event(t, "deviceConnected")
	select {
	case event := <-receiver.events:
		t.Fatalf("receiver advanced before local confirmation: %s", event.Status)
	case <-time.After(time.Second):
	}
	if _, err := os.Stat(store); !os.IsNotExist(err) {
		t.Fatal("receiver saved credentials before local confirmation")
	}
	if err := receiver.commands.Encode(map[string]string{"command": "confirm"}); err != nil {
		t.Fatal(err)
	}
	receiver.event(t, "connected")
	playback := receiver.event(t, "playback")
	media, err := url.Parse(playback.URL)
	if err != nil || media.Scheme != "http" || media.Hostname() != "127.0.0.1" || media.Port() == "" ||
		playback.Title != "합성미디어" || playback.MIMEType != "video/x-matroska" ||
		playback.Seekable == nil || !*playback.Seekable || playback.Subtitles == nil || len(*playback.Subtitles) != 1 {
		t.Fatal("invalid receiver playback status")
	}
	route := strings.Split(media.Path, "/")
	if len(route) != 3 || !validPairValue(route[1], 16) || route[2] != "media" {
		t.Fatal("receiver used an invalid playback route")
	}
	if _, ok, err := rememberedPC(store, remote.endpoint); err != nil || !ok {
		t.Fatal("confirmed remembered credentials were not saved")
	}
	client := &http.Client{Timeout: 5 * time.Second}
	response, err := client.Head(playback.URL)
	if err != nil {
		t.Fatal(err)
	}
	if response.ContentLength != int64(len(fileFixture)) || response.Header.Get("Accept-Ranges") != "bytes" {
		t.Fatal("bridge did not preserve HEAD and range metadata")
	}
	readResponse(t, response, http.StatusOK)
	req, _ := http.NewRequest(http.MethodGet, playback.URL, nil)
	req.Header.Set("Range", "bytes=2-5")
	req.Header.Set("Authorization", "Bearer ignored-local-header")
	response, err = client.Do(req)
	if err != nil || response.Header.Get("Content-Range") != "bytes 2-5/21" {
		t.Fatal("bridge did not forward the single byte range")
	}
	if body := readResponse(t, response, http.StatusPartialContent); body != "nthe" {
		t.Fatal("bridge changed media bytes")
	}
	subtitle := (*playback.Subtitles)[0]
	response, err = client.Get(subtitle.URL)
	if err != nil || subtitle.Label != "한국어" || readResponse(t, response, http.StatusOK) != vttFixture {
		t.Fatal("bridge did not expose the approved subtitle")
	}
	req, _ = http.NewRequest(http.MethodGet, subtitle.URL, nil)
	req.Header.Set("Range", "bytes=0-")
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	if response.Header.Get("Content-Range") != "" || readResponse(t, response, http.StatusOK) != vttFixture {
		t.Fatal("the initial subtitle range request did not return the complete VTT")
	}
	req, _ = http.NewRequest(http.MethodGet, subtitle.URL, nil)
	req.Header.Set("Range", "bytes=1-")
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	readResponse(t, response, http.StatusRequestedRangeNotSatisfiable)
	req, _ = http.NewRequest(http.MethodGet, playback.URL, nil)
	req.Header.Set("Range", "bytes=0-1,4-5")
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	readResponse(t, response, http.StatusRequestedRangeNotSatisfiable)
	req, _ = http.NewRequest(http.MethodGet, playback.URL, nil)
	req.Host = "unrelated.invalid"
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	readResponse(t, response, http.StatusNotFound)
	for _, bad := range []string{playback.URL + "?file=test.mkv", strings.TrimSuffix(playback.URL, "/media") + "/subtitle/no.vtt"} {
		response, err := client.Get(bad)
		if err != nil {
			t.Fatal(err)
		}
		readResponse(t, response, http.StatusNotFound)
	}

	disc, _ := discConfig(t)
	disc.AudioMode = "compatible"
	remote.control(t, map[string]any{"command": "share", "media": disc})
	remote.event(t, "sharing")
	progressive := receiver.event(t, "playback")
	if progressive.URL == playback.URL || progressive.Seekable == nil || *progressive.Seekable {
		t.Fatal("receiver kept the old URL or marked the progressive source seekable")
	}
	response, err = client.Get(playback.URL)
	if err != nil {
		t.Fatal(err)
	}
	readResponse(t, response, http.StatusNotFound)
	req, _ = http.NewRequest(http.MethodGet, progressive.URL, nil)
	req.Header.Set("Range", "bytes=1-")
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	readResponse(t, response, http.StatusRequestedRangeNotSatisfiable)
	req, _ = http.NewRequest(http.MethodGet, progressive.URL, nil)
	req.Header.Set("Range", "bytes=0-")
	response, err = client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	received := make([]byte, len(streamFixture))
	_, readErr := io.ReadFull(response.Body, received)
	response.Body.Close()
	if response.StatusCode != http.StatusOK || response.Header.Get("Accept-Ranges") != "none" ||
		readErr != nil || string(received) != string(streamFixture) {
		t.Fatalf("progressive Matroska response status=%d accept-ranges=%q read=%v",
			response.StatusCode, response.Header.Get("Accept-Ranges"), readErr)
	}

	receiver.stop()
	if conn, err := net.DialTimeout("tcp4", media.Host, time.Second); err == nil {
		conn.Close()
		t.Fatal("receiver EOF left the loopback listener open")
	}
	remote.control(t, map[string]string{"command": "disconnectDevice", "id": connected.DeviceID})
	remote.event(t, "devices")
	reconnected := startReceiverTestHost(t, remote, store, true, false)
	reconnected.event(t, "connected")
	remote.event(t, "deviceConnected")
	reconnected.event(t, "playback")
	remote.control(t, map[string]string{"command": "forgetDevice", "id": connected.DeviceID})
	remote.event(t, "devices")
	reconnected.event(t, "ended")
	reconnected.stop()
	rejected := startReceiverTestHost(t, remote, store, true, false)
	rejected.event(t, "pairingRequired")
}

func TestReceiverRejectsChangedRememberedKeyBeforeLogin(t *testing.T) {
	old := startPlayerTestHost(t, filepath.Join(t.TempDir(), "original.dpapi"))
	store := filepath.Join(t.TempDir(), "receiver.dpapi")
	secret, err := pairRandom(32)
	if err != nil {
		t.Fatal(err)
	}
	if err := rememberPC(store, old.endpoint, outboundPeer{
		Pin: old.pin, DeviceID: testPlayerDeviceID, Token: secret,
	}); err != nil {
		t.Fatal(err)
	}
	old.stop()
	replacement := startPlayerTestHost(t, filepath.Join(t.TempDir(), "replacement.dpapi"))
	replacement.endpoint = old.endpoint
	receiver := startReceiverTestHost(t, replacement, store, true, false)
	receiver.event(t, "pairingRequired")
	select {
	case event := <-replacement.events:
		t.Fatalf("key mismatch caused an unexpected player event %s", event.Status)
	default:
	}
}

func TestReceiverNeverFollowsAnUpstreamRedirect(t *testing.T) {
	ip := netip.MustParseAddr("192.168.7.25")
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := createCertificate(key, ip, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	var redirected atomic.Bool
	upstream := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == pairLoginPath {
			http.Redirect(w, r, "/unexpected", http.StatusTemporaryRedirect)
			return
		}
		redirected.Store(true)
		w.WriteHeader(http.StatusOK)
	}))
	upstream.TLS = &tls.Config{Certificates: []tls.Certificate{cert}}
	upstream.StartTLS()
	defer upstream.Close()
	peer := newReceiverPeer(netip.AddrPortFrom(ip, playerPort), "", func(ctx context.Context, network, _ string) (net.Conn, error) {
		return (&net.Dialer{}).DialContext(ctx, network, upstream.Listener.Addr().String())
	})
	defer peer.transport.CloseIdleConnections()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	result, err := peer.json(ctx, http.MethodPost, pairLoginPath, "test-bearer",
		map[string]string{"deviceId": testPlayerDeviceID}, new(any), maxPairBody)
	if err != nil || result.status != http.StatusTemporaryRedirect || redirected.Load() {
		t.Fatal("receiver followed the remote redirect")
	}
}
