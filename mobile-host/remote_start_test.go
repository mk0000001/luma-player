package main

import (
	"net/http"
	"net/http/httptest"
	"net/netip"
	"testing"
)

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
