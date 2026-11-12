package main

import (
	"bytes"
	"crypto/sha256"
	"crypto/tls"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"testing"
	"time"
)

const testPlayerDeviceID = "AQIDBAUGBwgJCgsMDQ4PEA"

type playerTestHost struct {
	url      string
	endpoint string
	client   *http.Client
	pin      string
	leaf     [32]byte
	commands *json.Encoder
	events   <-chan playerStatus
	stop     func()
}

func startPlayerTestHost(t *testing.T, store string) playerTestHost {
	t.Helper()
	if runtime.GOOS != "windows" {
		t.Skip("player integration uses Windows DPAPI")
	}
	ip, subnet := privateInterface(t)
	input, commands := io.Pipe()
	events, output := io.Pipe()
	statuses := make(chan playerStatus, 16)
	exitCode := make(chan int, 1)
	go func() {
		exitCode <- runWithServices(input, output, playerServices{
			listen: func(validatedConfig) (net.Listener, error) { return net.Listen("tcp4", "127.0.0.1:0") },
			discovery: func(netip.AddrPort, netip.Prefix, string) (io.Closer, error) {
				return io.NopCloser(bytes.NewReader(nil)), nil
			},
		})
		_ = output.Close()
		_ = input.Close()
	}()
	go func() {
		defer close(statuses)
		decoder := json.NewDecoder(events)
		for {
			var event playerStatus
			if decoder.Decode(&event) != nil {
				return
			}
			statuses <- event
		}
	}()
	var once sync.Once
	host := playerTestHost{
		commands: json.NewEncoder(commands), events: statuses,
		endpoint: netip.AddrPortFrom(ip, 48762).String(),
		stop: func() {
			once.Do(func() {
				_ = commands.Close()
				select {
				case code := <-exitCode:
					if code != 0 {
						t.Errorf("player exit code = %d", code)
					}
				case <-time.After(5 * time.Second):
					t.Error("player did not stop on stdin EOF")
				}
				_ = events.Close()
			})
		},
	}
	t.Cleanup(host.stop)
	host.control(t, map[string]string{
		"mode": "player", "listen": host.endpoint, "subnet": subnet.String(),
		"name": "테스트 PC", "store": filepath.ToSlash(store),
	})
	ready := host.event(t, "ready")
	if ready.Version != 1 {
		t.Fatalf("unexpected protocol version %d", ready.Version)
	}
	host.url = "https://" + ready.Address
	first := &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
	defer first.CloseIdleConnections()
	response, err := (&http.Client{Transport: first, Timeout: 5 * time.Second}).Get(host.url + "/v1/pair/identity")
	if err != nil {
		t.Fatal(err)
	}
	cert := response.TLS.PeerCertificates[0]
	if err := cert.VerifyHostname(ip.String()); err != nil {
		t.Fatalf("PC identity certificate did not contain the selected IP: %v", err)
	}
	host.leaf = sha256.Sum256(cert.Raw)
	pin := sha256.Sum256(cert.RawSubjectPublicKeyInfo)
	host.pin = base64.RawURLEncoding.EncodeToString(pin[:])
	var identity struct{ Name string }
	if err := json.Unmarshal([]byte(readResponse(t, response, http.StatusOK)), &identity); err != nil || identity.Name != "테스트 PC" {
		t.Fatalf("invalid first-contact identity %q", identity.Name)
	}
	pinned := &http.Transport{TLSClientConfig: &tls.Config{
		InsecureSkipVerify: true,
		VerifyConnection: func(state tls.ConnectionState) error {
			if sha256.Sum256(state.PeerCertificates[0].RawSubjectPublicKeyInfo) != pin {
				return fmt.Errorf("PC public key changed")
			}
			return state.PeerCertificates[0].VerifyHostname(ip.String())
		},
	}}
	t.Cleanup(pinned.CloseIdleConnections)
	host.client = &http.Client{Transport: pinned, Timeout: 5 * time.Second}
	return host
}

func (host playerTestHost) event(t *testing.T, expected string) playerStatus {
	t.Helper()
	select {
	case status := <-host.events:
		if status.Status != expected {
			t.Fatalf("expected %q status, got %+v", expected, status)
		}
		return status
	case <-time.After(5 * time.Second):
		t.Fatalf("timed out waiting for %q status", expected)
		return playerStatus{}
	}
}

func (host playerTestHost) request(t *testing.T, method, path, token string, input any, expected int, output any) string {
	t.Helper()
	var reader io.Reader
	if input != nil {
		data, err := json.Marshal(input)
		if err != nil {
			t.Fatal(err)
		}
		reader = bytes.NewReader(data)
	}
	req, err := http.NewRequest(method, host.url+path, reader)
	if err != nil {
		t.Fatal(err)
	}
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := host.client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	body := readResponse(t, response, expected)
	if output != nil {
		if err := json.Unmarshal([]byte(body), output); err != nil {
			t.Fatal(err)
		}
	}
	return body
}

func (host playerTestHost) control(t *testing.T, command any) {
	t.Helper()
	if err := host.commands.Encode(command); err != nil {
		t.Fatal(err)
	}
}

func (host playerTestHost) pair(t *testing.T, remember bool, decision string) pairPollResponse {
	t.Helper()
	return host.pairDevice(t, testPlayerDeviceID, remember, decision)
}

func (host playerTestHost) pairDevice(t *testing.T, deviceID string, remember bool, decision string) pairPollResponse {
	t.Helper()
	clientNonce := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{0x6c}, 32))
	digest := sha256.Sum256([]byte("luma-pair-commit-v1\n" + host.pin + "\n" + deviceID + "\n" + clientNonce))
	var started pairStartResponse
	host.request(t, http.MethodPost, "/v1/pair/start", "", map[string]any{
		"deviceId": deviceID, "device": "테스트 휴대폰", "remember": remember,
		"commitment": base64.RawURLEncoding.EncodeToString(digest[:]),
	}, http.StatusOK, &started)
	if len(started.ID) != 22 || len(started.ServerNonce) != 43 {
		t.Fatal("incorrect nonce or attempt ID length")
	}
	var polled pairPollResponse
	host.request(t, http.MethodPost, "/v1/pair/poll", "", map[string]string{"id": started.ID}, http.StatusOK, &polled)
	if polled.State != "pending" || polled.Token != "" {
		t.Fatal("pairing yielded a token before reveal")
	}
	select {
	case event := <-host.events:
		t.Fatalf("PC was prompted before reveal: %+v", event)
	default:
	}
	host.request(t, http.MethodPost, "/v1/pair/reveal", "", map[string]string{"id": started.ID, "clientNonce": clientNonce}, http.StatusOK, nil)
	prompt := host.event(t, "pairRequest")
	sum := sha256.Sum256([]byte("luma-pair-code-v1\n" + host.pin + "\n" + deviceID + "\n" + clientNonce + "\n" + started.ServerNonce))
	wantCode := fmt.Sprintf("%06d", binary.BigEndian.Uint32(sum[:4])%1_000_000)
	if prompt.ID != started.ID || prompt.Code != wantCode || prompt.Device != "테스트 휴대폰" || prompt.Remember == nil || *prompt.Remember != remember {
		t.Fatalf("invalid approval prompt: %+v", prompt)
	}
	host.request(t, http.MethodPost, "/v1/pair/poll", "", map[string]string{"id": started.ID}, http.StatusOK, &polled)
	if polled.State != "pending" || polled.Token != "" {
		t.Fatal("pairing yielded a token before approval")
	}
	host.control(t, map[string]string{"command": "decide", "id": started.ID, "decision": decision})
	if decision != "deny" {
		if status := host.event(t, "deviceConnected"); status.DeviceID != deviceID {
			t.Fatal("incorrect approved device")
		}
	}
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		host.request(t, http.MethodPost, "/v1/pair/poll", "", map[string]string{"id": started.ID}, http.StatusOK, &polled)
		if polled.State != "pending" {
			return polled
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("pairing decision never became available to the client")
	return pairPollResponse{}
}

func TestPlayerApprovalSharesStopsAndRevokes(t *testing.T) {
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "nested", "pairing.dpapi"))
	host.request(t, http.MethodGet, "/v1/session", "", nil, http.StatusUnauthorized, nil)
	paired := host.pair(t, false, "once")
	if paired.State != "approved" || len(paired.Token) != 22 || paired.RememberToken != "" {
		t.Fatal("incorrect once approval")
	}
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusConflict, nil)
	media := fileConfig(t)
	media.Listen, media.Subnet = "", ""
	host.control(t, map[string]any{"command": "share", "media": media})
	if event := host.event(t, "sharing"); event.Title != media.Title {
		t.Fatal("share did not acknowledge the selected title")
	}
	if body := host.request(t, http.MethodGet, "/v1/media", paired.Token, nil, http.StatusOK, nil); body != fileFixture {
		t.Fatal("player changed the file bytes")
	}
	host.control(t, map[string]string{"command": "stop"})
	if status := host.event(t, "stopped"); status.Command != "stop" {
		t.Fatal("stop acknowledgement was ambiguous")
	}
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusUnauthorized, nil)
	host.control(t, map[string]string{"command": "forget"})
	if status := host.event(t, "stopped"); status.Command != "forget" {
		t.Fatal("forget acknowledgement was ambiguous")
	}
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusUnauthorized, nil)
}

func TestPlayerDevicesDisconnectPreservesRememberAndForgetRevokes(t *testing.T) {
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "pairing.dpapi"))
	paired := host.pair(t, true, "remember")
	host.control(t, map[string]string{"command": "listDevices"})
	list := host.event(t, "devices")
	if list.Devices == nil || len(*list.Devices) != 1 ||
		(*list.Devices)[0] != (deviceInfo{ID: testPlayerDeviceID, Name: "테스트 휴대폰",
			Address: "127.0.0.1", Active: true, Remembered: true}) {
		t.Fatal("the approved remembered device was missing")
	}
	host.control(t, map[string]string{"command": "disconnectDevice", "id": testPlayerDeviceID})
	list = host.event(t, "devices")
	if list.Devices == nil || len(*list.Devices) != 1 || (*list.Devices)[0].Active || !(*list.Devices)[0].Remembered {
		t.Fatal("ordinary disconnect discarded the remembered device")
	}
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusUnauthorized, nil)
	var login struct{ Token string }
	host.request(t, http.MethodPost, "/v1/pair/login", paired.RememberToken,
		map[string]string{"deviceId": testPlayerDeviceID}, http.StatusOK, &login)
	host.event(t, "deviceConnected")
	host.control(t, map[string]string{"command": "forgetDevice", "id": testPlayerDeviceID})
	list = host.event(t, "devices")
	if list.Devices == nil || len(*list.Devices) != 0 {
		t.Fatal("forgotten device remained listed")
	}
	host.request(t, http.MethodGet, "/v1/session", login.Token, nil, http.StatusUnauthorized, nil)
	host.request(t, http.MethodPost, "/v1/pair/login", paired.RememberToken,
		map[string]string{"deviceId": testPlayerDeviceID}, http.StatusUnauthorized, nil)
}

func TestPlayerRememberedLoginSurvivesRestartAndForget(t *testing.T) {
	store := filepath.Join(t.TempDir(), "pairing.dpapi")
	first := startPlayerTestHost(t, store)
	paired := first.pair(t, true, "remember")
	if paired.State != "approved" || len(paired.RememberToken) != 43 {
		t.Fatal("remembered grant did not return a persistent token")
	}
	first.stop()
	second := startPlayerTestHost(t, store)
	if second.pin != first.pin || second.leaf == first.leaf {
		t.Fatal("the stable public key was lost or the leaf certificate was reused")
	}
	second.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusUnauthorized, nil)
	var login struct{ Token string }
	second.request(t, http.MethodPost, "/v1/pair/login", paired.RememberToken, map[string]string{"deviceId": testPlayerDeviceID}, http.StatusOK, &login)
	second.event(t, "deviceConnected")
	if len(login.Token) != 22 || login.Token == paired.Token {
		t.Fatal("remembered login did not issue a fresh playback token")
	}
	second.request(t, http.MethodGet, "/v1/session", login.Token, nil, http.StatusConflict, nil)
	second.control(t, map[string]string{"command": "forget"})
	second.event(t, "stopped")
	second.stop()
	third := startPlayerTestHost(t, store)
	if third.pin != first.pin {
		t.Fatal("forget unexpectedly rotated the PC identity")
	}
	third.request(t, http.MethodPost, "/v1/pair/login", paired.RememberToken, map[string]string{"deviceId": testPlayerDeviceID}, http.StatusUnauthorized, nil)
}

func TestPlayerStopAcknowledgesAfterWorkerExit(t *testing.T) {
	testPlayerWorkerTeardown(t, "stop", "stopped")
}

func TestPlayerDisconnectAcknowledgesAfterWorkerExit(t *testing.T) {
	testPlayerWorkerTeardown(t, "disconnectDevice", "devices")
}

func testPlayerWorkerTeardown(t *testing.T, command, expected string) {
	t.Helper()
	cfg, statePath := discConfig(t)
	cfg.AudioMode = "compatible"
	host := startPlayerTestHost(t, filepath.Join(t.TempDir(), "pairing.dpapi"))
	paired := host.pair(t, false, "once")
	host.control(t, map[string]any{"command": "share", "media": cfg})
	host.event(t, "sharing")
	request, err := http.NewRequest(http.MethodGet, host.url+"/v1/media", nil)
	if err != nil {
		t.Fatal(err)
	}
	request.Header.Set("Authorization", "Bearer "+paired.Token)
	response, err := host.client.Do(request)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("synthetic worker did not start, status %d", response.StatusCode)
	}
	worker := observeWorker(t, statePath)
	process, err := os.FindProcess(worker.PID)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = process.Kill()
		_ = process.Release()
	})
	host.control(t, map[string]string{"command": command, "id": testPlayerDeviceID})
	host.event(t, expected)
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
		t.Fatal("stopped was emitted while FFmpeg was still running")
	}
	host.request(t, http.MethodGet, "/v1/session", paired.Token, nil, http.StatusUnauthorized, nil)
}
