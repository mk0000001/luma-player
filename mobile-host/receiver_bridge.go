package main

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"mime"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync/atomic"
	"time"
	"unicode/utf8"
)

const maxSessionMetadata = 64 * 1024

type bridgeMedia struct {
	metadata sessionMetadata
	tracks   map[string]subtitleMetadata
	prefix   string
	revision string
	ctx      context.Context
	cancel   context.CancelFunc
}

type playbackBridge struct {
	peer    *receiverPeer
	token   string
	address string
	server  *http.Server
	done    chan struct{}
	media   atomic.Pointer[bridgeMedia]
}

func startPlaybackBridge(ctx context.Context, peer *receiverPeer, token string) (*playbackBridge, error) {
	socket, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	listener := &lanListener{Listener: socket, loopback: true, slots: make(chan struct{}, maxConnections)}
	bridge := &playbackBridge{peer: peer, token: token, address: socket.Addr().String(), done: make(chan struct{})}
	bridge.server = &http.Server{
		Handler:                      http.HandlerFunc(bridge.serveHTTP),
		BaseContext:                  func(net.Listener) context.Context { return ctx },
		ErrorLog:                     log.New(io.Discard, "", 0),
		DisableGeneralOptionsHandler: true,
		ReadHeaderTimeout:            5 * time.Second,
		ReadTimeout:                  10 * time.Second,
		WriteTimeout:                 2*ffmpegStartupTimeout + 20*time.Second,
		IdleTimeout:                  15 * time.Second,
		MaxHeaderBytes:               8 * 1024,
	}
	go func() {
		defer close(bridge.done)
		_ = bridge.server.Serve(listener)
	}()
	return bridge, nil
}

func (b *playbackBridge) close() {
	if media := b.media.Swap(nil); media != nil {
		media.cancel()
	}
	_ = b.server.Close()
	<-b.done
}

func (b *playbackBridge) watch(ctx context.Context, output *json.Encoder) error {
	ticker := time.NewTicker(4 * time.Second)
	defer ticker.Stop()
	failures := 0
	for {
		var metadata sessionMetadata
		response, err := b.peer.json(ctx, http.MethodGet, "/v1/session", b.token, nil, &metadata, maxSessionMetadata)
		switch {
		case err != nil:
			failures++
			if failures >= 3 {
				return err
			}
		case response.status == http.StatusConflict:
			if b.media.Load() != nil {
				return io.EOF
			}
			failures = 0
		case response.status != http.StatusOK:
			return errPeerResponse
		default:
			failures = 0
			if err := b.publish(ctx, metadata, response.revision, output); err != nil {
				return err
			}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-b.done:
			return io.EOF
		case <-ticker.C:
		}
	}
}

func (b *playbackBridge) publish(ctx context.Context, metadata sessionMetadata, revision string, output *json.Encoder) error {
	tracks, err := validateBridgeMetadata(&metadata)
	if err != nil {
		return err
	}
	if len(revision) != 24 || revision[0] != '"' || revision[23] != '"' || !validPairValue(revision[1:23], 16) {
		data, err := json.Marshal(metadata)
		if err != nil {
			return errPeerResponse
		}
		revision = digestPairValue(string(data))
	}
	if current := b.media.Load(); current != nil && current.revision == revision {
		return nil
	}
	random, err := pairRandom(16)
	if err != nil {
		return errPeerResponse
	}
	mediaCtx, cancel := context.WithCancel(ctx)
	media := &bridgeMedia{metadata: metadata, tracks: tracks, prefix: "/" + random, revision: revision, ctx: mediaCtx, cancel: cancel}
	if old := b.media.Swap(media); old != nil {
		old.cancel()
	}
	base := "http://" + b.address + media.prefix
	subtitles := make([]receiverSubtitle, 0, len(metadata.Subtitles))
	for _, track := range metadata.Subtitles {
		subtitles = append(subtitles, receiverSubtitle{URL: base + "/subtitle/" + track.ID + ".vtt", Label: track.Label})
	}
	return output.Encode(receiverStatus{
		Status: "playback", URL: base + "/media", Title: metadata.Title, MIMEType: metadata.MIMEType,
		DurationMS: metadata.DurationMS, Seekable: &metadata.Seekable, Subtitles: &subtitles,
	})
}

func validateBridgeMetadata(metadata *sessionMetadata) (map[string]subtitleMetadata, error) {
	if metadata.Version != protocolVersion || metadata.DurationMS < 0 || len(metadata.Title) > 1024 ||
		!receiverMIME(metadata.MIMEType) || len(metadata.Subtitles) > maxSubtitles {
		return nil, errPeerResponse
	}
	if metadata.SourceKind != "file" &&
		(metadata.SourceKind != "bluray" || metadata.Seekable || metadata.MIMEType != matroskaMIME) {
		return nil, errPeerResponse
	}
	metadata.Title = sanitizeReceiverText(metadata.Title)
	tracks := make(map[string]subtitleMetadata, len(metadata.Subtitles))
	for index := range metadata.Subtitles {
		track := &metadata.Subtitles[index]
		filename := track.ID + ".vtt"
		if !subtitleIDPattern.MatchString(track.ID) || len(track.Label) > 512 ||
			track.Path != "/v1/subtitle/"+filename {
			return nil, errPeerResponse
		}
		if _, duplicate := tracks[filename]; duplicate {
			return nil, errPeerResponse
		}
		track.Label = sanitizeReceiverText(track.Label)
		if track.Label == "" {
			track.Label = track.ID
		}
		tracks[filename] = *track
	}
	return tracks, nil
}

func receiverMIME(value string) bool {
	switch value {
	case matroskaMIME, "video/mp4", "video/webm", "video/quicktime", "video/x-msvideo",
		"video/mp2t", "audio/mp4", "audio/flac", "audio/mpeg", "audio/wav":
		return true
	}
	return false
}

func (b *playbackBridge) serveHTTP(original http.ResponseWriter, r *http.Request) {
	w := newResponseWriter(original, r)
	media := b.media.Load()
	if media == nil || r.Host != b.address || !validRequestTarget(r) {
		http.NotFound(w, r)
		return
	}
	path, ok := strings.CutPrefix(r.URL.Path, media.prefix+"/")
	if !ok {
		http.NotFound(w, r)
		return
	}
	if rejectRequestBody(w, r) {
		return
	}
	ctx, cancel := context.WithCancel(r.Context())
	stop := context.AfterFunc(media.ctx, cancel)
	defer func() {
		stop()
		cancel()
	}()
	w.ctx = ctx
	if media.ctx.Err() != nil {
		return
	}
	if path == "media" {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			methodNotAllowed(w, "GET, HEAD")
			return
		}
		b.serveBridgeMedia(w, r.WithContext(ctx), media.metadata)
		return
	}
	filename, isSubtitle := strings.CutPrefix(path, "subtitle/")
	track, known := media.tracks[filename]
	if !isSubtitle || !known {
		http.NotFound(w, r)
		return
	}
	if r.Method != http.MethodGet {
		methodNotAllowed(w, "GET")
		return
	}
	// libmpv may open a subtitle with this initial range even though it
	// downloads the entire WebVTT. The upstream request stays un-ranged.
	ranges := r.Header.Values("Range")
	if len(ranges) > 1 || (len(ranges) == 1 && ranges[0] != "bytes=0-") {
		http.Error(w, "range unavailable", http.StatusRequestedRangeNotSatisfiable)
		return
	}
	b.serveBridgeSubtitle(w, ctx, track.Path)
}

func (b *playbackBridge) serveBridgeMedia(w *responseWriter, r *http.Request, metadata sessionMetadata) {
	headers := r.Header.Values("Range")
	var span string
	var bounds byteRange
	if len(headers) > 0 {
		var valid bool
		bounds, valid = parseByteRange(headers[0])
		if len(headers) != 1 || !valid || (!metadata.Seekable && (bounds.suffix || bounds.first != 0 || bounds.last != -1)) {
			http.Error(w, "range unavailable", http.StatusRequestedRangeNotSatisfiable)
			return
		}
		if metadata.Seekable {
			span = headers[0]
		}
	}
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()
	response, err := b.peer.request(ctx, r.Method, "/v1/media", b.token, span, nil)
	if err != nil {
		http.Error(w, "remote playback unavailable", http.StatusBadGateway)
		return
	}
	defer response.Body.Close()
	if response.StatusCode == http.StatusRequestedRangeNotSatisfiable && span != "" {
		http.Error(w, "range unavailable", http.StatusRequestedRangeNotSatisfiable)
		return
	}
	expected := http.StatusOK
	if span != "" {
		expected = http.StatusPartialContent
	}
	contentType, _, err := mime.ParseMediaType(response.Header.Get("Content-Type"))
	if response.StatusCode != expected || err != nil || contentType != metadata.MIMEType ||
		response.Header.Get("Content-Encoding") != "" ||
		(span != "" && !bounds.matches(response.Header.Get("Content-Range"), response.ContentLength)) {
		http.Error(w, "remote playback unavailable", http.StatusBadGateway)
		return
	}
	w.Header().Set("Content-Type", metadata.MIMEType)
	if metadata.Seekable {
		w.Header().Set("Accept-Ranges", "bytes")
	} else {
		w.Header().Set("Accept-Ranges", "none")
	}
	if span != "" {
		w.Header().Set("Content-Range", response.Header.Get("Content-Range"))
	}
	if response.ContentLength >= 0 {
		w.Header().Set("Content-Length", strconv.FormatInt(response.ContentLength, 10))
	}
	w.WriteHeader(expected)
	w.flushWrites = !metadata.Seekable
	if r.Method == http.MethodGet && w.controller.Flush() == nil {
		if _, err := io.Copy(w, &idleMediaReader{source: response.Body, onIdle: cancel}); err != nil {
			panic(http.ErrAbortHandler)
		}
	}
}

func (b *playbackBridge) serveBridgeSubtitle(w *responseWriter, parent context.Context, path string) {
	ctx, cancel := context.WithTimeout(parent, 10*time.Second)
	defer cancel()
	response, err := b.peer.request(ctx, http.MethodGet, path, b.token, "", nil)
	if err != nil {
		http.Error(w, "remote subtitle unavailable", http.StatusBadGateway)
		return
	}
	defer response.Body.Close()
	contentType, _, typeErr := mime.ParseMediaType(response.Header.Get("Content-Type"))
	data, err := io.ReadAll(io.LimitReader(response.Body, maxSubtitleBytes+1))
	if response.StatusCode != http.StatusOK || typeErr != nil || contentType != "text/vtt" ||
		response.Header.Get("Content-Encoding") != "" || err != nil || len(data) > maxSubtitleBytes ||
		!utf8.Valid(data) || !hasVTTHeader(string(data)) {
		http.Error(w, "remote subtitle unavailable", http.StatusBadGateway)
		return
	}
	w.Header().Set("Content-Type", "text/vtt; charset=utf-8")
	_, _ = w.Write(data)
}

type byteRange struct {
	first  int64
	last   int64
	suffix bool
}

func parseByteRange(value string) (byteRange, bool) {
	span, ok := strings.CutPrefix(value, "bytes=")
	first, last, found := strings.Cut(span, "-")
	if !ok || !found {
		return byteRange{}, false
	}
	if first == "" {
		length, err := rangeNumber(last)
		return byteRange{last: length, suffix: true}, err == nil && length > 0
	}
	start, err := rangeNumber(first)
	if err != nil {
		return byteRange{}, false
	}
	if last == "" {
		return byteRange{first: start, last: -1}, true
	}
	end, err := rangeNumber(last)
	return byteRange{first: start, last: end}, err == nil && end >= start
}

func rangeNumber(value string) (int64, error) {
	if value == "" || strings.IndexFunc(value, func(c rune) bool { return c < '0' || c > '9' }) >= 0 {
		return 0, errors.New("invalid range")
	}
	return strconv.ParseInt(value, 10, 64)
}

func (span byteRange) matches(contentRange string, contentLength int64) bool {
	parts, ok := strings.CutPrefix(contentRange, "bytes ")
	rangePart, totalPart, found := strings.Cut(parts, "/")
	received, valid := parseByteRange("bytes=" + rangePart)
	total, err := rangeNumber(totalPart)
	if !ok || !found || !valid || received.suffix || received.last < 0 || err != nil || total <= 0 {
		return false
	}
	first, last := span.first, total-1
	if span.suffix {
		first = max(0, total-span.last)
	} else if span.last >= 0 {
		last = min(last, span.last)
	}
	return first <= last && received.first == first && received.last == last && contentLength == last-first+1
}
