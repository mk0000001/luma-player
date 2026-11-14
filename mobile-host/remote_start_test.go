package main

import (
	"bytes"
	"image"
	"image/png"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strings"
	"testing"
	"time"
)

func TestDiscMenuPreviewRequiresCurrentGrantAndExpires(t *testing.T) {
	var events []playerStatus
	p := newPlayer("Fixture", "", nil, nil, func(event playerStatus) { events = append(events, event) })
	now := time.Now()
	p.now = func() time.Time { return now }
	peer := netip.MustParseAddr("192.168.4.2")
	token, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice("fixture", "Tablet", peer, token)
	check := func(address, bearer string, status int) *httptest.ResponseRecorder {
		t.Helper()
		request := httptest.NewRequest(http.MethodGet, discMenuPath, nil)
		request.RemoteAddr = address + ":50000"
		request.Header.Set("Authorization", "Bearer "+bearer)
		response := httptest.NewRecorder()
		p.serveHTTP(response, request)
		if response.Code != status {
			t.Fatalf("got %d, want %d", response.Code, status)
		}
		return response
	}
	check(peer.String(), token, http.StatusForbidden)
	const grant = "0123456789abcdef0123456789abcdef"
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteControl", Grant: grant})
	check("192.168.4.3", token, http.StatusUnauthorized)
	check(peer.String(), token, http.StatusNoContent)
	check(peer.String(), token, http.StatusNoContent)
	if len(events) != 1 || events[0].Status != "discMenuFrameRequested" || events[0].ID != grant {
		t.Fatalf("unexpected requests: %+v", events)
	}
	var frame bytes.Buffer
	if err := png.Encode(&frame, image.NewRGBA(image.Rect(0, 0, 2, 2))); err != nil {
		t.Fatal(err)
	}
	p.handleCommand(config{}, playerCommand{Command: "discMenuFrame", Grant: grant, Frame: frame.Bytes()})
	response := check(peer.String(), token, http.StatusOK)
	if !bytes.Equal(response.Body.Bytes(), frame.Bytes()) || response.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("unexpected menu response")
	}
	now = now.Add(3 * time.Second)
	check(peer.String(), token, http.StatusNoContent)
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteControl"})
	check(peer.String(), token, http.StatusForbidden)
}

func TestRemoteStartRequiresPairingAndHostPermission(t *testing.T) {
	var requested int
	p := newPlayer("Fixture", "", nil, nil, func(status playerStatus) {
		if status.Status == "remoteStart" {
			requested++
		}
	})
	peer := netip.MustParseAddr("192.168.4.2")
	token, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice("fixture", "Tablet", peer, token)
	check := func(method, address, credential string, want int) {
		t.Helper()
		req := httptest.NewRequest(method, "/v1/playback/start", nil)
		req.RemoteAddr = address
		if credential != "" {
			req.Header.Set("Authorization", "Bearer "+credential)
		}
		response := httptest.NewRecorder()
		p.serveHTTP(response, req)
		if response.Code != want {
			t.Fatalf("status %d, want %d", response.Code, want)
		}
	}
	check(http.MethodPost, "192.168.4.2:50000", "", http.StatusUnauthorized)
	check(http.MethodPost, "192.168.4.3:50000", token, http.StatusUnauthorized)
	check(http.MethodPost, "192.168.4.2:50000", token, http.StatusForbidden)
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteStart", Grant: "0123456789abcdef0123456789abcdef"})
	check(http.MethodGet, "192.168.4.2:50000", token, http.StatusMethodNotAllowed)
	check(http.MethodPost, "192.168.4.2:50000", token, http.StatusAccepted)
	check(http.MethodPost, "192.168.4.2:50000", token, http.StatusTooManyRequests)
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteStart"})
	check(http.MethodPost, "192.168.4.2:50000", token, http.StatusForbidden)
	if requested != 1 {
		t.Fatalf("dispatched %d remote start requests, want one", requested)
	}
}

func TestRemoteControlRequiresHostGrantAndOnlyAcceptsKnownActions(t *testing.T) {
	var received []playerStatus
	p := newPlayer("Fixture", "", nil, nil, func(status playerStatus) {
		received = append(received, status)
	})
	token, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice("fixture", "Tablet", netip.MustParseAddr("192.168.4.2"), token)
	check := func(path, address, credential, body string, want int) {
		t.Helper()
		req := httptest.NewRequest(http.MethodPost, path, strings.NewReader(body))
		req.RemoteAddr = address
		if credential != "" {
			req.Header.Set("Authorization", "Bearer "+credential)
		}
		response := httptest.NewRecorder()
		p.serveHTTP(response, req)
		if response.Code != want {
			t.Fatalf("status %d, want %d", response.Code, want)
		}
	}
	const valid = `{"action":"disc-confirm"}`
	const origin = "192.168.4.2:50000"
	check(remoteControlPath, origin, "", valid, http.StatusUnauthorized)
	check(remoteControlPath, origin, token, valid, http.StatusForbidden)
	const grant = "0123456789abcdef0123456789abcdef"
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteControl", Grant: grant})
	check(remoteControlPath, "192.168.4.3:50000", token, valid, http.StatusUnauthorized)
	check(remoteControlPath, origin, token, `{"action":"open-path"}`, http.StatusBadRequest)
	check(remoteControlPath, origin, token, `{"action":"disc-confirm","path":"fixture"}`, http.StatusBadRequest)
	check(remoteControlPath, origin, token, valid, http.StatusAccepted)
	check(remoteControlPath, origin, token, valid, http.StatusTooManyRequests)
	check(remoteStartPath, origin, token, "", http.StatusAccepted)
	otherToken, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice("new-device", "Second tablet", netip.MustParseAddr("192.168.4.3"), otherToken)
	check(remoteControlPath, "192.168.4.3:50000", otherToken, valid, http.StatusForbidden)
	check(remoteStartPath, "192.168.4.3:50000", otherToken, "", http.StatusForbidden)
	p.handleCommand(config{}, playerCommand{Command: "allowRemoteControl"})
	check(remoteControlPath, origin, token, valid, http.StatusForbidden)
	check(remoteStartPath, origin, token, "", http.StatusForbidden)
	if len(received) != 2 || received[0].Action != "disc-confirm" || received[0].ID != grant ||
		received[1].Status != "remoteStart" || received[1].ID != grant {
		t.Fatalf("unexpected dispatched events: %+v", received)
	}
}
