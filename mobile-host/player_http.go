package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/netip"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"
)

const maxPairBody = 1024
const remoteStartPath = "/v1/playback/start"
const remoteControlPath = "/v1/player/control"
const remoteFilesPath = "/v1/player/files"

func validRemoteFileID(id string) bool {
	return len(id) == 32 && strings.Trim(id, "0123456789abcdef") == ""
}

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
	case remoteStartPath:
		p.serveRemoteStart(w, r, peer)
	case remoteControlPath:
		p.serveRemoteControl(w, r, peer)
	case remoteFilesPath:
		p.serveRemoteFiles(w, r, peer)
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

func (p *player) serveRemoteFiles(w *responseWriter, r *http.Request, peer netip.Addr) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w, "GET")
		return
	}
	if rejectRequestBody(w, r) {
		return
	}
	p.mu.Lock()
	device := p.authorizeLocked(peer, requestBearer(r), false, p.now())
	_, allowed := p.controlDevices[device]
	files := p.remoteFiles
	p.mu.Unlock()
	if device == nil {
		unauthorized(w)
	} else if !allowed {
		http.Error(w, "remote control is disabled", http.StatusForbidden)
	} else {
		if files == nil {
			files = []remoteFile{}
		}
		writePairJSON(w, struct {
			Files []remoteFile `json:"files"`
		}{files})
	}
}

func (p *player) serveRemoteControl(w *responseWriter, r *http.Request, peer netip.Addr) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, "POST")
		return
	}
	var request struct {
		Action string `json:"action"`
		FileID string `json:"fileId"`
	}
	if !readPairJSON(w, r, &request) {
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	device := p.authorizeLocked(peer, requestBearer(r), false, now)
	if device == nil {
		unauthorized(w)
		return
	}
	if _, allowed := p.controlDevices[device]; p.remoteControl == "" || !allowed {
		http.Error(w, "remote control is disabled", http.StatusForbidden)
		return
	}
	switch request.Action {
	case "file-open":
		found := false
		for _, file := range p.remoteFiles {
			if file.ID == request.FileID {
				found = true
				break
			}
		}
		if !found {
			http.NotFound(w, r)
			return
		}
	case "disc-open", "disc-menu", "disc-popup", "disc-up", "disc-down", "disc-left", "disc-right", "disc-confirm",
		"play-pause", "previous-chapter", "next-chapter", "seek-back", "seek-forward", "volume-down", "volume-up",
		"mute", "fullscreen", "picture-in-picture", "resume", "speed-next", "audio-next", "subtitle-next",
		"caption-ai", "caption-original", "caption-reading", "caption-translation":
	default:
		http.Error(w, "invalid control", http.StatusBadRequest)
		return
	}
	if request.Action != "file-open" && request.FileID != "" {
		http.Error(w, "invalid control parameter", http.StatusBadRequest)
		return
	}
	if !p.lastControl.IsZero() && now.Sub(p.lastControl) < 125*time.Millisecond {
		http.Error(w, "control rate exceeded", http.StatusTooManyRequests)
		return
	}
	p.lastControl = now
	p.announce(playerStatus{Status: "remoteControl", ID: p.remoteControl, Action: request.Action, DeviceID: request.FileID})
	w.WriteHeader(http.StatusAccepted)
}

func (p *player) serveRemoteStart(w *responseWriter, r *http.Request, peer netip.Addr) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, "POST")
		return
	}
	if rejectRequestBody(w, r) {
		return
	}
	status := p.requestRemoteStart(peer, requestBearer(r))
	if status == http.StatusUnauthorized {
		unauthorized(w)
	} else if status == http.StatusAccepted {
		w.WriteHeader(status)
	} else {
		http.Error(w, http.StatusText(status), status)
	}
}

func (p *player) requestRemoteStart(peer netip.Addr, token string) int {
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	device := p.authorizeLocked(peer, token, false, now)
	if device == nil {
		return http.StatusUnauthorized
	}
	grant := p.remoteStart
	if _, allowed := p.controlDevices[device]; grant == "" && allowed {
		grant = p.remoteControl
	}
	if grant == "" {
		return http.StatusForbidden
	}
	if p.media != nil && p.media.ctx.Err() == nil {
		return http.StatusAccepted
	}
	if !p.lastRemoteStart.IsZero() && now.Sub(p.lastRemoteStart) < 2*time.Second {
		return http.StatusTooManyRequests
	}
	p.lastRemoteStart = now
	p.announce(playerStatus{Status: "remoteStart", ID: grant})
	return http.StatusAccepted
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
	route, valid := parsePlaybackRoute(path)
	if !valid {
		http.NotFound(w, r)
		return
	}
	if rejectRequestBody(w, r) {
		return
	}
	trackRequest := route.media
	media, device := p.authorize(peer, requestBearer(r), trackRequest)
	if device == nil {
		unauthorized(w)
		return
	}
	if trackRequest {
		defer device.requests.Done()
	}
	allowedMethods := "GET"
	if route.media {
		allowedMethods = "GET, HEAD"
	}
	if r.Method != http.MethodGet && !(route.media && r.Method == http.MethodHead) {
		methodNotAllowed(w, allowedMethods)
		return
	}
	if media == nil || media.ctx.Err() != nil {
		http.Error(w, "waiting for media", http.StatusConflict)
		return
	}
	if path == "/v1/session" {
		w.Header().Set("ETag", "\""+media.revision+"\"")
	}
	media.serve(w, r, device, route)
}

type playbackRoute struct {
	media           bool
	profile         string
	position        int64
	shiftedSubtitle string
}

func parsePlaybackRoute(path string) (playbackRoute, bool) {
	switch path {
	case "/v1/session":
		return playbackRoute{}, true
	case "/v1/media":
		return playbackRoute{media: true}, true
	}
	if request, exists := strings.CutPrefix(path, variantPrefix); exists {
		profile, start, ok := strings.Cut(request, "/")
		if !ok {
			return playbackRoute{}, false
		}
		switch profile {
		case originalProfile, "1080p", "720p", "480p":
		default:
			return playbackRoute{}, false
		}
		position, valid := canonicalPosition(start, false)
		return playbackRoute{media: true, profile: profile, position: position}, valid
	}
	if request, exists := strings.CutPrefix(path, "/v1/subtitle-at/"); exists {
		origin, filename, found := strings.Cut(request, "/")
		id, suffix := strings.CutSuffix(filename, ".vtt")
		position, valid := canonicalPosition(origin, true)
		return playbackRoute{position: position, shiftedSubtitle: filename}, found && suffix && valid && subtitleIDPattern.MatchString(id)
	}
	id, prefix := strings.CutPrefix(path, "/v1/subtitle/")
	id, suffix := strings.CutSuffix(id, ".vtt")
	return playbackRoute{}, prefix && suffix && subtitleIDPattern.MatchString(id)
}

func canonicalPosition(value string, signed bool) (int64, bool) {
	position, err := strconv.ParseInt(value, 10, 64)
	return position, err == nil && (signed || position >= 0) && strconv.FormatInt(position, 10) == value
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
