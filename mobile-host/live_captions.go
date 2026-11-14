package main

import (
	"io"
	"net/http"
	"net/netip"
	"sync"
	"time"
)

const liveCaptionPath = "/v1/captions"
const captionPCMBytesPerMS = 32 // mono, 16 kHz, signed 16-bit little endian
const captionAudioCapacity = 120_000 * captionPCMBytesPerMS
const maxCaptionAudioMS = 15_000

type captionState struct {
	State            string `json:"state"`
	Enabled          bool   `json:"enabled"`
	Translation      bool   `json:"translation"`
	TranslationState string `json:"translationState,omitempty"`
	Reading          bool   `json:"reading"`
	Original         bool   `json:"original"`
	CanAI            bool   `json:"canAI"`
	AheadMS          int64  `json:"aheadMs"`
	Backend          string `json:"backend,omitempty"`
	VTT              string `json:"vtt,omitempty"`
}

func (p *player) setCaptionSnapshot(revision string, state *captionState) {
	if state == nil || len(state.VTT) > maxSubtitleBytes || (state.VTT != "" && !hasVTTHeader(state.VTT)) ||
		len(state.Backend) > 32 || state.AheadMS < 0 || state.AheadMS > 120_000 {
		return
	}
	switch state.State {
	case "off", "waiting", "processing", "ready", "failed", "file", "prepared":
	default:
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.media != nil && p.media.revision == revision && p.media.session.config.LiveCaptions {
		if p.media.captions == nil || p.media.captions.VTT != state.VTT {
			p.media.captionRevision++
		}
		p.media.captions = state
	}
}

func (p *player) serveCaptionState(w *responseWriter, r *http.Request, peer netip.Addr) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, "POST")
		return
	}
	var request struct {
		Revision        string `json:"revision"`
		PositionMS      int64  `json:"positionMs"`
		CaptionRevision int64  `json:"captionRevision"`
	}
	if !readPairJSON(w, r, &request) {
		return
	}
	p.mu.Lock()
	now := p.now()
	device := p.authorizeLocked(peer, requestBearer(r), false, now)
	if device == nil {
		p.mu.Unlock()
		unauthorized(w)
		return
	}
	media := p.media
	if media == nil || media.revision != request.Revision || !media.session.config.LiveCaptions {
		p.mu.Unlock()
		http.Error(w, "captions are unavailable for this media", http.StatusConflict)
		return
	}
	if request.PositionMS < 0 || request.PositionMS > maxRestartableDurationMS ||
		(media.session.config.DurationMS > 0 && request.PositionMS > media.session.config.DurationMS) {
		p.mu.Unlock()
		http.Error(w, "invalid caption position", http.StatusBadRequest)
		return
	}
	_, allowed := p.controlDevices[device]
	allowed = allowed && p.remoteControl != ""
	refresh := allowed && now.Sub(media.lastCaptionClock) >= 750*time.Millisecond
	if refresh {
		media.lastCaptionClock = now
	}
	state := captionState{State: "waiting"}
	if media.captions != nil {
		state = *media.captions
	}
	if media.captionRevision == request.CaptionRevision {
		state.VTT = ""
	}
	reply := struct {
		captionState
		Revision   int64 `json:"revision"`
		CanControl bool  `json:"canControl"`
	}{state, media.captionRevision, allowed}
	p.mu.Unlock()
	if refresh {
		p.announce(playerStatus{Status: "captionClock", Revision: media.revision, Position: request.PositionMS})
	}
	w.Header().Set("Cache-Control", "no-store")
	writePairJSON(w.ResponseWriter, reply)
}

func (p *player) sendCaptionAudio(revision, requestID string, position, duration int64) {
	if requestID == "" || len(requestID) > 32 || position < 0 || position > maxRestartableDurationMS ||
		duration <= 0 || duration > maxCaptionAudioMS {
		return
	}
	p.mu.Lock()
	media := p.media
	p.mu.Unlock()
	if media == nil || media.revision != revision || !media.session.config.LiveCaptions ||
		media.session.config.SourceKind != "bluray" {
		return
	}
	audio := media.session.captionAudio.Load()
	if audio == nil {
		p.announce(playerStatus{Status: "captionAudio", Revision: revision, ID: requestID, Action: "waiting"})
		return
	}
	pcm, status := audio.read(position, duration)
	p.announce(playerStatus{Status: "captionAudio", Revision: revision, ID: requestID, Action: status, PCM: pcm})
}

// The remuxer writes to this bounded ring from its existing Blu-ray input.
// Keeping audio in memory lets recognition use the same optical reader as video.
type captionAudioBuffer struct {
	mu     sync.Mutex
	start  int64
	total  int64
	data   []byte
	closed bool
}

func (a *captionAudioBuffer) reset(start int64) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.start, a.total, a.closed = start, 0, false
	if a.data == nil {
		a.data = make([]byte, captionAudioCapacity)
	}
}

func (a *captionAudioBuffer) Write(chunk []byte) (int, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.closed || len(a.data) == 0 {
		return 0, io.ErrClosedPipe
	}
	size := len(chunk)
	for len(chunk) > 0 {
		at := int(a.total % int64(len(a.data)))
		n := copy(a.data[at:], chunk)
		a.total += int64(n)
		chunk = chunk[n:]
	}
	return size, nil
}

func (a *captionAudioBuffer) finish() {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.closed = true
}

func (a *captionAudioBuffer) read(start, duration int64) ([]byte, string) {
	a.mu.Lock()
	defer a.mu.Unlock()
	if len(a.data) == 0 {
		return nil, "waiting"
	}
	offset := (start - a.start) * captionPCMBytesPerMS
	count := duration * captionPCMBytesPerMS
	if offset < max(0, a.total-int64(len(a.data))) {
		return nil, "expired"
	}
	if offset+count > a.total {
		if a.closed {
			return nil, "ended"
		}
		return nil, "waiting"
	}
	result := make([]byte, count)
	at := int(offset % int64(len(a.data)))
	n := copy(result, a.data[at:])
	copy(result[n:], a.data)
	return result, "ready"
}
