package main

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

const (
	mediaReconnectWait = time.Second
	responseWriteWait  = 12 * time.Second
)

type sessionMetadata struct {
	Version    int                `json:"version"`
	Title      string             `json:"title"`
	MIMEType   string             `json:"mimeType"`
	SourceKind string             `json:"sourceKind"`
	DurationMS int64              `json:"durationMs"`
	Seekable   bool               `json:"seekable"`
	Subtitles  []subtitleMetadata `json:"subtitles"`
}

type subtitleMetadata struct {
	ID       string `json:"id"`
	Label    string `json:"label"`
	Language string `json:"language"`
	Path     string `json:"path"`
}

type mediaSession struct {
	config    validatedConfig
	token     string
	meta      sessionMetadata
	subtitles map[string]string
	file      *os.File
	fileInfo  os.FileInfo
	media     mediaGate
}

func newSession(cfg validatedConfig, token string) (*mediaSession, error) {
	session := &mediaSession{
		config: cfg, token: token, subtitles: make(map[string]string, len(cfg.Subtitles)),
		meta: sessionMetadata{
			Version: protocolVersion, Title: cfg.Title, MIMEType: cfg.mimeType,
			SourceKind: cfg.SourceKind, DurationMS: cfg.DurationMS, Seekable: cfg.SourceKind == "file",
			Subtitles: make([]subtitleMetadata, 0, len(cfg.Subtitles)),
		},
	}
	for _, subtitle := range cfg.Subtitles {
		path := "/v1/subtitle/" + subtitle.ID + ".vtt"
		session.meta.Subtitles = append(session.meta.Subtitles, subtitleMetadata{
			ID: subtitle.ID, Label: subtitle.Label, Language: subtitle.Language, Path: path,
		})
		session.subtitles[path] = subtitle.VTT
	}
	if cfg.SourceKind == "file" {
		file, err := os.Open(cfg.Source)
		if err != nil {
			return nil, err
		}
		info, err := file.Stat()
		if err != nil || !info.Mode().IsRegular() {
			_ = file.Close()
			return nil, errInvalidSource
		}
		session.file, session.fileInfo = file, info
	}
	return session, nil
}

func (s *mediaSession) close() {
	s.media.stopAndWait()
	if s.file != nil {
		_ = s.file.Close()
	}
}

func (s *mediaSession) authorized(r *http.Request) bool {
	token := requestBearer(r)
	return token != "" && subtle.ConstantTimeCompare([]byte(token), []byte(s.token)) == 1
}

func requestBearer(r *http.Request) string {
	values := r.Header.Values("Authorization")
	if len(values) != 1 {
		return ""
	}
	scheme, token, ok := strings.Cut(values[0], " ")
	if !ok || !strings.EqualFold(scheme, "Bearer") {
		return ""
	}
	return token
}

func newResponseWriter(original http.ResponseWriter, r *http.Request) *responseWriter {
	controller := http.NewResponseController(original)
	w := &responseWriter{ResponseWriter: original, controller: controller, ctx: r.Context()}
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	return w
}

func rejectRequestBody(w http.ResponseWriter, r *http.Request) bool {
	if r.ContentLength == 0 && len(r.TransferEncoding) == 0 {
		return false
	}
	w.Header().Set("Connection", "close")
	http.Error(w, "unexpected request body", http.StatusBadRequest)
	return true
}

func unauthorized(w http.ResponseWriter) {
	w.Header().Set("WWW-Authenticate", "Bearer")
	w.Header().Set("Connection", "close")
	http.Error(w, "unauthorized", http.StatusUnauthorized)
}

func validRequestTarget(r *http.Request) bool {
	return !r.URL.IsAbs() && r.URL.RawPath == "" && r.URL.RawQuery == "" && !r.URL.ForceQuery
}

func (s *mediaSession) serveHTTP(original http.ResponseWriter, r *http.Request) {
	w := newResponseWriter(original, r)
	if rejectRequestBody(w, r) {
		return
	}
	if !s.authorized(r) {
		unauthorized(w)
		return
	}
	if !validRequestTarget(r) {
		http.NotFound(w, r)
		return
	}
	s.servePlayback(w, r)
}

func (s *mediaSession) servePlayback(w *responseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/v1/session":
		if r.Method != http.MethodGet {
			methodNotAllowed(w, "GET")
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(s.meta)
	case "/v1/media":
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			methodNotAllowed(w, "GET, HEAD")
			return
		}
		s.serveMedia(w, r)
	default:
		subtitle, exists := s.subtitles[r.URL.Path]
		if !exists {
			http.NotFound(w, r)
			return
		}
		if r.Method != http.MethodGet {
			methodNotAllowed(w, "GET")
			return
		}
		w.Header().Set("Content-Type", "text/vtt; charset=utf-8")
		_, _ = io.WriteString(w, subtitle)
	}
}

func methodNotAllowed(w http.ResponseWriter, allow string) {
	w.Header().Set("Allow", allow)
	http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
}

func (s *mediaSession) serveMedia(w *responseWriter, r *http.Request) {
	if !s.media.acquire(r.Context()) {
		w.Header().Set("Retry-After", "1")
		http.Error(w, "media stream is busy", http.StatusServiceUnavailable)
		return
	}
	defer s.media.release()

	if s.file != nil {
		info, err := s.file.Stat()
		if err != nil || info.Size() != s.fileInfo.Size() || !info.ModTime().Equal(s.fileInfo.ModTime()) {
			http.Error(w, "selected file changed", http.StatusConflict)
			return
		}
		w.Header().Set("Content-Type", s.meta.MIMEType)
		http.ServeContent(w, r, "", s.fileInfo.ModTime(), io.NewSectionReader(s.file, 0, s.fileInfo.Size()))
		return
	}

	w.Header().Set("Accept-Ranges", "none")
	if r.Method == http.MethodHead {
		w.Header().Set("Content-Type", s.meta.MIMEType)
		w.WriteHeader(http.StatusOK)
		return
	}
	s.serveDisc(w, r.Context())
}

type responseWriter struct {
	http.ResponseWriter
	controller  *http.ResponseController
	ctx         context.Context
	onWrite     func()
	flushWrites bool
}

func (w *responseWriter) WriteHeader(status int) {
	w.Header().Set("Cache-Control", "no-store")
	w.ResponseWriter.WriteHeader(status)
}

func (w *responseWriter) Write(data []byte) (int, error) {
	if err := w.ctx.Err(); err != nil {
		return 0, err
	}
	if err := w.controller.SetWriteDeadline(time.Now().Add(responseWriteWait)); err != nil {
		return 0, err
	}
	if err := w.ctx.Err(); err != nil {
		return 0, err
	}
	written, err := w.ResponseWriter.Write(data)
	if err == nil && written > 0 && w.flushWrites {
		err = w.controller.Flush()
	}
	if written > 0 && w.onWrite != nil {
		w.onWrite()
	}
	return written, err
}

type mediaGate struct {
	mu      sync.Mutex
	active  chan struct{}
	stopped bool
}

func (g *mediaGate) acquire(ctx context.Context) bool {
	timer := time.NewTimer(mediaReconnectWait)
	defer timer.Stop()
	for {
		g.mu.Lock()
		if g.stopped || ctx.Err() != nil {
			g.mu.Unlock()
			return false
		}
		active := g.active
		if active == nil {
			g.active = make(chan struct{})
			g.mu.Unlock()
			return true
		}
		g.mu.Unlock()
		select {
		case <-active:
		case <-ctx.Done():
			return false
		case <-timer.C:
			return false
		}
	}
}

func (g *mediaGate) release() {
	g.mu.Lock()
	close(g.active)
	g.active = nil
	g.mu.Unlock()
}

func (g *mediaGate) stopAndWait() {
	g.mu.Lock()
	g.stopped = true
	active := g.active
	g.mu.Unlock()
	if active != nil {
		<-active
	}
}
