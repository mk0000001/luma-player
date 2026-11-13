package main

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strconv"
	"time"
)

const (
	originalProfile          = "original"
	variantPrefix            = "/v1/media/variant/"
	maxRestartableDurationMS = 999_990_000
)

var qualityProfiles = []string{"1080p", "720p", "480p"}

func (s *mediaSession) configureVariants() {
	boundedTimeline := s.config.DurationMS > 0 && s.config.DurationMS < maxRestartableDurationMS
	if s.config.SourceKind == "bluray" {
		s.meta.Restartable = boundedTimeline
	}
	if boundedTimeline && s.config.FFmpeg != "" && s.config.VideoRange != "unknown" &&
		(s.config.SourceKind == "bluray" || fileVariantDemuxer(s.config.mimeType) != "") &&
		supportsQualityEncoding(s.config) {
		s.meta.Profiles = append(s.meta.Profiles, qualityProfiles...)
		s.meta.Restartable = boundedTimeline
	}
}

func (s *mediaSession) serveVariant(w *responseWriter, r *http.Request, profile string, start int64) {
	if !slices.Contains(s.meta.Profiles, profile) {
		http.NotFound(w, r)
		return
	}
	if start == 0 && profile == originalProfile {
		s.serveMedia(w, r)
		return
	}
	if start > 0 && (!s.meta.Restartable || start >= s.meta.DurationMS) {
		http.Error(w, "media start unavailable", http.StatusRequestedRangeNotSatisfiable)
		return
	}
	if !validProgressiveRange(r) {
		http.Error(w, "range unavailable", http.StatusRequestedRangeNotSatisfiable)
		return
	}
	w.Header().Set("Accept-Ranges", "none")
	if r.Method == http.MethodHead {
		w.Header().Set("Content-Type", matroskaMIME)
		w.WriteHeader(http.StatusOK)
		return
	}
	if !s.acquireMedia(w, r) {
		return
	}
	defer s.media.release()
	if s.file != nil {
		info, err := os.Stat(s.config.Source)
		if err != nil || !os.SameFile(info, s.fileInfo) || info.Size() != s.fileInfo.Size() ||
			!info.ModTime().Equal(s.fileInfo.ModTime()) {
			http.Error(w, "selected file changed", http.StatusConflict)
			return
		}
	}
	s.serveProgressive(w, r.Context(), profile, start)
}

func validProgressiveRange(r *http.Request) bool {
	ranges := r.Header.Values("Range")
	return len(ranges) == 0 || (len(ranges) == 1 && ranges[0] == "bytes=0-")
}

func fileVariantDemuxer(mimeType string) string {
	switch mimeType {
	case "video/mp4", "video/quicktime":
		return "mov"
	case matroskaMIME, "video/webm":
		return "matroska"
	case "video/mp2t":
		return "mpegts"
	case "video/x-msvideo":
		return "avi"
	default:
		return ""
	}
}

func variantArguments(cfg validatedConfig, profile string, start int64, convertPCM bool) []string {
	return variantArgumentsForSeek(cfg, profile, start, convertPCM, true)
}

func variantArgumentsForSeek(cfg validatedConfig, profile string, start int64, convertPCM, chapterSeek bool) []string {
	audioMap := "0:a:" + strconv.Itoa(max(0, cfg.AudioIndex))
	if cfg.AudioTrackID != nil && *cfg.AudioTrackID >= 0 {
		audioMap = "0:a:i:" + strconv.Itoa(*cfg.AudioTrackID)
	} else if cfg.AudioIndex == -1 || cfg.SourceKind == "file" {
		audioMap += "?"
	}
	args := []string{"-hide_banner", "-nostdin", "-loglevel", "error"}
	chapter := 1
	if chapterSeek && cfg.SourceKind == "bluray" && start > 0 && len(cfg.ChaptersMS) > 1 && cfg.ChaptersMS[0] == 0 {
		if index, found := slices.BinarySearch(cfg.ChaptersMS, start); found {
			chapter = index + 1
		}
	}
	if start > 0 && chapter == 1 {
		args = append(args, "-ss", fmt.Sprintf("%d.%03d", start/1000, start%1000))
	}
	if cfg.SourceKind == "bluray" {
		playlist, _ := strconv.Atoi(cfg.Playlist)
		args = append(args, "-protocol_whitelist", "bluray", "-playlist", strconv.Itoa(playlist))
		if chapter > 1 {
			args = append(args, "-chapter", strconv.Itoa(chapter))
		}
		args = append(args, "-i", "bluray:"+filepath.ToSlash(cfg.Source))
	} else {
		demuxer := fileVariantDemuxer(cfg.mimeType)
		args = append(args, "-protocol_whitelist", "fd", "-f", demuxer)
		if demuxer == "mov" {
			args = append(args, "-enable_drefs", "0")
		}
		args = append(args, "-fd", "0", "-i", "fd:")
	}
	videoMap := "0:v:0"
	if cfg.SourceKind == "file" {
		videoMap = "0:V:0"
	}
	args = append(args, "-map", videoMap, "-map", audioMap)
	if profile == originalProfile {
		args = append(args, "-c:v", "copy")
		switch {
		case cfg.SourceKind == "bluray" && cfg.AudioMode == "compatible":
			args = append(args, aacEncoding...)
		case convertPCM:
			args = append(args, "-c:a", "pcm_s24le")
		default:
			args = append(args, "-c:a", "copy")
		}
	} else {
		args = append(args, qualityEncoding(profile, cfg.VideoRange)...)
	}
	return append(args,
		"-sn", "-dn", "-map_metadata", "-1", "-map_chapters", "-1",
		"-f", "matroska", "-live", "1", "-flush_packets", "1", "pipe:1",
	)
}

var aacEncoding = []string{"-c:a", "aac", "-ac", "2", "-b:a", "192k"}

func qualityEncoding(profile, videoRange string) []string {
	width, height := 854, 480
	switch profile {
	case "1080p":
		width, height = 1920, 1080
	case "720p":
		width, height = 1280, 720
	}
	scale := fmt.Sprintf("scale=w='min(iw,%d)':h='min(ih,%d)':force_original_aspect_ratio=decrease:force_divisible_by=2", width, height)
	filter := scale + ",format=yuv420p"
	var color []string
	if videoRange == "pq" || videoRange == "hlg" {
		transfer := "smpte2084"
		if videoRange == "hlg" {
			transfer = "arib-std-b67"
		}
		filter = "zscale=transferin=" + transfer + ":transfer=linear:npl=100,format=gbrpf32le," +
			"zscale=primaries=bt709,tonemap=tonemap=mobius:param=0.3:desat=2," +
			"zscale=transfer=bt709:primaries=bt709:matrix=bt709:range=limited," + filter +
			",sidedata=mode=delete:type=MASTERING_DISPLAY_METADATA,sidedata=mode=delete:type=CONTENT_LIGHT_LEVEL" +
			",sidedata=mode=delete:type=DYNAMIC_HDR_PLUS,sidedata=mode=delete:type=DOVI_METADATA" +
			",sidedata=mode=delete:type=DOVI_RPU_BUFFER"
		color = []string{"-color_trc", "bt709", "-color_primaries", "bt709", "-colorspace", "bt709", "-color_range", "tv"}
	}
	args := []string{"-vf", filter, "-c:v", "libx264", "-preset", "veryfast", "-crf", "21", "-tune", "zerolatency"}
	args = append(args, color...)
	return append(args, aacEncoding...)
}

// Probe the actual bundled conversion chain using generated samples, without
// opening another optical reader or reading the selected source.
func supportsQualityEncoding(cfg validatedConfig) bool {
	ctx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	video := "color=gray:s=64x64:r=24"
	if cfg.VideoRange == "pq" || cfg.VideoRange == "hlg" {
		transfer := "smpte2084"
		if cfg.VideoRange == "hlg" {
			transfer = "arib-std-b67"
		}
		video += ",format=yuv420p10le,setparams=range=limited:color_primaries=bt2020:colorspace=bt2020nc:color_trc=" + transfer
	}
	args := []string{
		"-hide_banner", "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", video,
		"-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo", "-map", "0:v:0", "-map", "1:a:0",
	}
	args = append(args, qualityEncoding("480p", cfg.VideoRange)...)
	args = append(args, "-frames:v", "1", "-frames:a", "1", "-f", "matroska", "pipe:1")
	command := exec.CommandContext(ctx, cfg.FFmpeg, args...)
	command.Dir = filepath.Dir(cfg.FFmpeg)
	command.Stdout, command.Stderr = io.Discard, io.Discard
	command.WaitDelay = time.Second
	hideProcess(command)
	return command.Run() == nil
}
