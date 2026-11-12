package main

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

const (
	fileFixture = "synthetic-media-bytes"
	vttFixture  = "WEBVTT\n\n00:00:00.000 --> 00:00:01.000\n테스트\n"
)

func privateInterface(t *testing.T) (netip.Addr, netip.Prefix) {
	t.Helper()
	interfaces, err := net.Interfaces()
	if err != nil {
		t.Fatal(err)
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			subnet, err := netip.ParsePrefix(addr.String())
			if err == nil && subnet.Addr().Is4() && subnet.Addr().IsPrivate() {
				return subnet.Addr(), subnet.Masked()
			}
		}
	}
	t.Skip("integration test requires an assigned RFC1918 IPv4 address")
	return netip.Addr{}, netip.Prefix{}
}

func fileConfig(t *testing.T) config {
	t.Helper()
	ip, subnet := privateInterface(t)
	path := filepath.Join(t.TempDir(), "sample.mkv")
	if err := os.WriteFile(path, []byte(fileFixture), 0600); err != nil {
		t.Fatal(err)
	}
	return config{
		Listen: netip.AddrPortFrom(ip, 0).String(), Subnet: subnet.String(),
		SourceKind: "file", Source: path, AudioIndex: -1, AudioMode: "original",
		DurationMS: 7_200_000, Title: "합성 미디어",
		Subtitles: []subtitleConfig{{ID: "ko-1", Label: "한국어", Language: "ko", VTT: vttFixture}},
	}
}

type testHost struct {
	address    string
	serverName string
	pin        [32]byte
	token      string
	stop       func()
}

func startTestHost(t *testing.T, cfg config) testHost {
	t.Helper()
	input, send := io.Pipe()
	output, receive := io.Pipe()
	exitCode := make(chan int, 1)
	listen := listenLAN
	if os.Getenv("LUMA_TEST_DIRECT_LAN") != "1" {
		// Keep TLS and the stdin lifecycle on real TCP on Windows with
		// per-executable inbound filtering. The LAN policy is tested separately.
		listen = func(validatedConfig) (net.Listener, error) { return net.Listen("tcp4", "127.0.0.1:0") }
	}
	go func() {
		exitCode <- runWithListener(input, receive, listen)
		_ = receive.Close()
		_ = input.Close()
	}()
	t.Cleanup(func() {
		_ = output.Close()
		_ = send.Close()
	})
	if err := json.NewEncoder(send).Encode(cfg); err != nil {
		t.Fatal(err)
	}
	var ready struct {
		Status, Address, PairingCode, Message string
		Version                               int
	}
	decoder := json.NewDecoder(output)
	if err := decoder.Decode(&ready); err != nil {
		t.Fatal(err)
	}
	if ready.Status != "ready" || ready.Version != 1 || len(ready.PairingCode) != 64 {
		t.Fatalf("unexpected startup response: %s (version %d, %s)", ready.Status, ready.Version, ready.Message)
	}
	data, err := base64.RawURLEncoding.DecodeString(ready.PairingCode)
	if err != nil || len(data) != 48 {
		t.Fatal("invalid pairing payload")
	}
	if addr, err := netip.ParseAddrPort(ready.Address); err != nil || addr.Port() == 0 {
		t.Fatal("ready did not contain a bound address")
	}
	var stopOnce sync.Once
	host := testHost{
		address: ready.Address, serverName: netip.MustParseAddrPort(cfg.Listen).Addr().String(),
		pin:   [32]byte(data[:32]),
		token: base64.RawURLEncoding.EncodeToString(data[32:]),
		stop: func() {
			stopOnce.Do(func() {
				_ = send.Close()
				select {
				case code := <-exitCode:
					if code != 0 {
						t.Errorf("host exit code = %d", code)
					}
					if err := decoder.Decode(new(any)); err != io.EOF {
						t.Errorf("expected exactly one status line, got %v", err)
					}
				case <-time.After(5 * time.Second):
					t.Error("host did not stop on stdin EOF")
				}
			})
		},
	}
	t.Cleanup(host.stop)
	return host
}

func (host testHost) client(t *testing.T, pin [32]byte) *http.Client {
	t.Helper()
	transport := &http.Transport{TLSClientConfig: &tls.Config{
		InsecureSkipVerify: true, // This test verifies the session certificate pin.
		VerifyConnection: func(state tls.ConnectionState) error {
			if len(state.PeerCertificates) != 1 {
				return errors.New("expected one session certificate")
			}
			cert := state.PeerCertificates[0]
			if sha256.Sum256(cert.Raw) != pin {
				return errors.New("session certificate pin mismatch")
			}
			key, ok := cert.PublicKey.(*ecdsa.PublicKey)
			if !ok || key.Curve != elliptic.P256() || cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature) != nil {
				return errors.New("expected a self-signed P-256 certificate")
			}
			return cert.VerifyHostname(host.serverName)
		},
	}}
	t.Cleanup(transport.CloseIdleConnections)
	return &http.Client{Transport: transport, Timeout: 5 * time.Second}
}

func (host testHost) request(t *testing.T, client *http.Client, method, path, auth, span string) *http.Response {
	t.Helper()
	request, err := http.NewRequest(method, "https://"+host.address+path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if auth != "" {
		request.Header.Set("Authorization", auth)
	}
	if span != "" {
		request.Header.Set("Range", span)
	}
	response, err := client.Do(request)
	if err != nil {
		t.Fatal(err)
	}
	return response
}

func readResponse(t *testing.T, response *http.Response, code int) string {
	t.Helper()
	defer response.Body.Close()
	if response.StatusCode != code {
		t.Fatalf("status %d, want %d", response.StatusCode, code)
	}
	body, err := io.ReadAll(response.Body)
	if err != nil {
		t.Fatal(err)
	}
	return string(body)
}

func TestTLSAuthAndExactRoutes(t *testing.T) {
	cfg := fileConfig(t)
	host := startTestHost(t, cfg)
	defer host.stop()
	client := host.client(t, host.pin)
	badPin := host.pin
	badPin[0] ^= 1
	if response, err := host.client(t, badPin).Get("https://" + host.address + "/v1/session"); err == nil {
		response.Body.Close()
		t.Fatal("certificate mismatch did not fail TLS")
	}
	legacy := &http.Client{Transport: &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true, MaxVersion: tls.VersionTLS11},
	}, Timeout: 2 * time.Second}
	if response, err := legacy.Get("https://" + host.address + "/v1/session"); err == nil {
		response.Body.Close()
		t.Fatal("obsolete TLS version connected")
	}
	for _, path := range []string{"/v1/session", "/v1/media", "/v1/subtitle/ko-1.vtt"} {
		unauthenticated := host.request(t, client, http.MethodGet, path, "", "")
		if !unauthenticated.Close {
			t.Fatal("unauthenticated response left the connection open")
		}
		readResponse(t, unauthenticated, http.StatusUnauthorized)
		readResponse(t, host.request(t, client, http.MethodGet, path, "Bearer "+strings.Repeat("x", 22), ""), http.StatusUnauthorized)
	}
	auth := "Bearer " + host.token
	for _, path := range []string{
		"/v1/subtitle/../media", "/v1/%6dedia", "/v1/subtitle/missing.vtt", "/v1/media?file=sample.mkv",
	} {
		readResponse(t, host.request(t, client, http.MethodGet, path, auth, ""), http.StatusNotFound)
	}
	response := host.request(t, client, http.MethodGet, "/v1/session", auth, "")
	if response.Header.Get("Cache-Control") != "no-store" {
		t.Fatal("session response was cacheable")
	}
	body := readResponse(t, response, http.StatusOK)
	var meta struct {
		Version                     int
		Title, SourceKind, MimeType string
		DurationMS                  int64
		Seekable                    bool
		Subtitles                   []struct{ ID, Label, Language, Path string }
	}
	if err := json.Unmarshal([]byte(body), &meta); err != nil {
		t.Fatal(err)
	}
	if meta.Version != 1 || meta.Title != "합성 미디어" || meta.SourceKind != "file" ||
		meta.MimeType != "video/x-matroska" || meta.DurationMS != 7_200_000 || !meta.Seekable ||
		len(meta.Subtitles) != 1 || meta.Subtitles[0].ID != "ko-1" || meta.Subtitles[0].Label != "한국어" ||
		meta.Subtitles[0].Language != "ko" || meta.Subtitles[0].Path != "/v1/subtitle/ko-1.vtt" ||
		strings.Contains(body, filepath.Base(cfg.Source)) || strings.Contains(body, "WEBVTT") {
		t.Fatalf("invalid session metadata: %+v", meta)
	}
	response = host.request(t, client, http.MethodGet, "/v1/subtitle/ko-1.vtt", auth, "")
	if response.Header.Get("Content-Type") != "text/vtt; charset=utf-8" || response.Header.Get("Cache-Control") != "no-store" {
		t.Fatal("incorrect WebVTT headers")
	}
	if body := readResponse(t, response, http.StatusOK); body != vttFixture {
		t.Fatal("WebVTT data changed")
	}
}

func TestFileHeadAndRangePreserveOriginalBytes(t *testing.T) {
	host := startTestHost(t, fileConfig(t))
	defer host.stop()
	client := host.client(t, host.pin)
	auth := "Bearer " + host.token
	head := host.request(t, client, http.MethodHead, "/v1/media", auth, "")
	if head.ContentLength != int64(len(fileFixture)) || head.Header.Get("Accept-Ranges") != "bytes" {
		t.Fatal("HEAD did not advertise file length and byte ranges")
	}
	if body := readResponse(t, head, http.StatusOK); body != "" {
		t.Fatal("HEAD returned a response body")
	}
	for _, tc := range []struct {
		request, contentRange, body string
	}{
		{"bytes=2-5", "bytes 2-5/21", "nthe"},
		{"bytes=18-", "bytes 18-20/21", "tes"},
	} {
		response := host.request(t, client, http.MethodGet, "/v1/media", auth, tc.request)
		if response.Header.Get("Content-Range") != tc.contentRange {
			t.Fatalf("unexpected Content-Range: %q", response.Header.Get("Content-Range"))
		}
		if body := readResponse(t, response, http.StatusPartialContent); body != tc.body {
			t.Fatalf("unexpected range payload: %q", body)
		}
	}
	response := host.request(t, client, http.MethodGet, "/v1/media", auth, "")
	if body := readResponse(t, response, http.StatusOK); !bytes.Equal([]byte(body), []byte(fileFixture)) {
		t.Fatal("source bytes changed")
	}
}
