package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/netip"
	"strings"
	"unicode/utf8"
)

const maxPairBody = 1024

func (p *player) serveHTTP(original http.ResponseWriter, r *http.Request) {
	w := newResponseWriter(original, r)
	if !validRequestTarget(r) {
		http.NotFound(w, r)
		return
	}
	remote, err := netip.ParseAddrPort(r.RemoteAddr)
	if err != nil || !remote.Addr().Unmap().Is4() {
		w.Header().Set("Connection", "close")
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	peer := remote.Addr().Unmap()
	switch r.URL.Path {
	case pairIdentityPath:
		if r.Method != http.MethodGet {
			methodNotAllowed(w, "GET")
			return
		}
		if !rejectRequestBody(w, r) {
			writePairJSON(w, struct {
				Name string `json:"name"`
			}{Name: p.name})
		}
	case pairStartPath, pairRevealPath, pairPollPath, pairLoginPath:
		if r.Method != http.MethodPost {
			methodNotAllowed(w, "POST")
			return
		}
		p.servePairing(w, r, peer)
	default:
		p.servePlayerMedia(w, r, peer)
	}
}

func (p *player) servePairing(w *responseWriter, r *http.Request, peer netip.Addr) {
	switch r.URL.Path {
	case pairStartPath:
		var request pairStartRequest
		if !readPairJSON(w, r, &request) {
			return
		}
		started, status := p.beginPairing(peer, request)
		if status != http.StatusOK {
			http.Error(w, http.StatusText(status), status)
			return
		}
		writePairJSON(w, started)
	case pairRevealPath:
		var request pairRevealRequest
		if !readPairJSON(w, r, &request) {
			return
		}
		status := p.revealPairing(peer, request)
		if status != http.StatusOK {
			http.Error(w, http.StatusText(status), status)
			return
		}
		writePairJSON(w, pairPollResponse{State: "pending"})
	case pairPollPath:
		var request struct {
			ID string `json:"id"`
		}
		if !readPairJSON(w, r, &request) {
			return
		}
		if !validPairValue(request.ID, 16) {
			http.Error(w, "invalid pairing request", http.StatusBadRequest)
			return
		}
		writePairJSON(w, p.pollPairing(peer, request.ID))
	case pairLoginPath:
		secret := requestBearer(r)
		if !validPairValue(secret, 32) {
			unauthorized(w)
			return
		}
		var request struct {
			DeviceID string `json:"deviceId"`
		}
		if !readPairJSON(w, r, &request) {
			return
		}
		token, status := p.login(peer, request.DeviceID, secret)
		if status == http.StatusUnauthorized {
			unauthorized(w)
			return
		}
		if status != http.StatusOK {
			http.Error(w, http.StatusText(status), status)
			return
		}
		writePairJSON(w, struct {
			Token string `json:"token"`
		}{Token: token})
	}
}

func (p *player) servePlayerMedia(w *responseWriter, r *http.Request, peer netip.Addr) {
	path := r.URL.Path
	switch path {
	case "/v1/session", "/v1/media":
	default:
		id, prefix := strings.CutPrefix(path, "/v1/subtitle/")
		id, suffix := strings.CutSuffix(id, ".vtt")
		if !prefix || !suffix || !subtitleIDPattern.MatchString(id) {
			http.NotFound(w, r)
			return
		}
	}
	if rejectRequestBody(w, r) {
		return
	}
	media, authorized := p.authorize(peer, requestBearer(r))
	if !authorized {
		unauthorized(w)
		return
	}
	allowedMethods := "GET"
	if path == "/v1/media" {
		allowedMethods = "GET, HEAD"
	}
	if r.Method != http.MethodGet && !(path == "/v1/media" && r.Method == http.MethodHead) {
		methodNotAllowed(w, allowedMethods)
		return
	}
	if media == nil || media.ctx.Err() != nil {
		http.Error(w, "waiting for media", http.StatusConflict)
		return
	}
	media.serve(w, r)
}

func readPairJSON(w *responseWriter, r *http.Request, target any) bool {
	data, err := io.ReadAll(http.MaxBytesReader(w.ResponseWriter, r.Body, maxPairBody))
	var tooLarge *http.MaxBytesError
	if errors.As(err, &tooLarge) {
		w.Header().Set("Connection", "close")
		http.Error(w, "pairing request too large", http.StatusRequestEntityTooLarge)
		return false
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err != nil || !utf8.Valid(data) || decoder.Decode(target) != nil || decoder.Decode(new(any)) != io.EOF {
		w.Header().Set("Connection", "close")
		http.Error(w, "invalid pairing request", http.StatusBadRequest)
		return false
	}
	return true
}

func writePairJSON(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(value)
}
