package main

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"strings"
)

var inlineVTTTime = regexp.MustCompile(`<(?:[0-9]{2,9}:)?[0-9]{2}:[0-9]{2}\.[0-9]{3}>`)

func (s *mediaSession) serveShiftedSubtitle(w *responseWriter, r *http.Request, filename string, origin int64) {
	if origin < -5000 || (origin > s.meta.DurationMS && origin-s.meta.DurationMS > 5000) {
		http.Error(w, "subtitle origin unavailable", http.StatusRequestedRangeNotSatisfiable)
		return
	}
	original, exists := s.subtitles["/v1/subtitle/"+filename]
	if !exists {
		http.NotFound(w, r)
		return
	}
	select {
	case s.subtitleTransforms <- struct{}{}:
		defer func() { <-s.subtitleTransforms }()
	default:
		w.Header().Set("Retry-After", "1")
		http.Error(w, "subtitle conversion is busy", http.StatusServiceUnavailable)
		return
	}
	shifted, valid := shiftWebVTT(r.Context(), original, origin)
	if r.Context().Err() != nil {
		return
	}
	if !valid {
		http.Error(w, "shifted subtitle unavailable", http.StatusUnprocessableEntity)
		return
	}
	w.Header().Set("Content-Type", "text/vtt; charset=utf-8")
	_, _ = io.WriteString(w, shifted)
}

func shiftWebVTT(ctx context.Context, original string, origin int64) (string, bool) {
	if origin == 0 {
		return original, true
	}
	text := strings.ReplaceAll(strings.ReplaceAll(original, "\r\n", "\n"), "\r", "\n")
	_, remaining, _ := strings.Cut(text, "\n")
	var output strings.Builder
	output.WriteString("WEBVTT\n\n")
	for remaining != "" {
		if ctx.Err() != nil {
			return "", false
		}
		remaining = strings.TrimLeft(remaining, "\n")
		block, rest, _ := strings.Cut(remaining, "\n\n")
		remaining = rest
		shifted := shiftVTTCue(block, origin)
		if shifted == "" {
			continue
		}
		if output.Len()+len(shifted)+2 > maxSubtitleBytes {
			return "", false
		}
		output.WriteString(shifted)
		output.WriteString("\n\n")
	}
	return output.String(), true
}

func shiftVTTCue(block string, origin int64) string {
	timing, payload, _ := strings.Cut(block, "\n")
	switch {
	case timing == "STYLE", timing == "REGION":
		return block
	case timing == "NOTE", strings.HasPrefix(timing, "NOTE "), strings.HasPrefix(timing, "NOTE\t"):
		return ""
	}
	var identifier string
	if !strings.Contains(timing, "-->") {
		identifier = timing + "\n"
		timing, payload, _ = strings.Cut(payload, "\n")
	}
	start, afterArrow, found := strings.Cut(timing, "-->")
	if !found {
		return ""
	}
	afterArrow = strings.TrimSpace(afterArrow)
	end, settings := afterArrow, ""
	if index := strings.IndexAny(afterArrow, " \t"); index >= 0 {
		end, settings = afterArrow[:index], afterArrow[index:]
	}
	first, firstOK := parseVTTTime(strings.TrimSpace(start))
	last, lastOK := parseVTTTime(end)
	if !firstOK || !lastOK || last <= first || last <= origin {
		return ""
	}
	payload = inlineVTTTime.ReplaceAllStringFunc(payload, func(tag string) string {
		position, ok := parseVTTTime(tag[1 : len(tag)-1])
		if !ok {
			return tag
		}
		if position <= origin {
			return ""
		}
		return "<" + formatVTTTime(position-origin) + ">"
	})
	return identifier + formatVTTTime(max(0, first-origin)) + " --> " + formatVTTTime(last-origin) + settings + "\n" + payload
}

func parseVTTTime(value string) (int64, bool) {
	if len(value) < 9 || len(value) > 19 {
		return 0, false
	}
	clock, milliseconds, found := strings.Cut(value, ".")
	parts := strings.Split(clock, ":")
	if !found || len(milliseconds) != 3 || (len(parts) != 2 && len(parts) != 3) {
		return 0, false
	}
	var hours int64
	if len(parts) == 3 {
		if len(parts[0]) < 2 || len(parts[0]) > 9 {
			return 0, false
		}
		var err error
		hours, err = rangeNumber(parts[0])
		if err != nil {
			return 0, false
		}
		parts = parts[1:]
	}
	if len(parts[0]) != 2 || len(parts[1]) != 2 {
		return 0, false
	}
	minutes, minuteErr := rangeNumber(parts[0])
	seconds, secondErr := rangeNumber(parts[1])
	fraction, fractionErr := rangeNumber(milliseconds)
	if minuteErr != nil || secondErr != nil || fractionErr != nil || minutes >= 60 || seconds >= 60 {
		return 0, false
	}
	return ((hours*60+minutes)*60+seconds)*1000 + fraction, true
}

func formatVTTTime(ms int64) string {
	return fmt.Sprintf("%02d:%02d:%02d.%03d", ms/3600000, ms/60000%60, ms/1000%60, ms%1000)
}
