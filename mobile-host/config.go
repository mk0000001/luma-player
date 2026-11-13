package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"io"
	"io/fs"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strings"
	"unicode/utf8"
)

const (
	maxConfigBytes   = 16 * 1024 * 1024
	maxSubtitleBytes = 4 * 1024 * 1024
	maxSubtitles     = 8
	matroskaMIME     = "video/x-matroska"
)

var (
	errInvalidConfig   = errors.New("invalid configuration")
	errInvalidLAN      = errors.New("invalid LAN address or subnet")
	errInvalidSource   = errors.New("invalid media source")
	errInvalidSubtitle = errors.New("invalid subtitle configuration")
	playlistPattern    = regexp.MustCompile(`^[0-9]{5}$`)
	subtitleIDPattern  = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)
)

type config struct {
	Listen        string           `json:"listen"`
	Subnet        string           `json:"subnet"`
	PeerSubnet    string           `json:"peerSubnet,omitempty"`
	SourceKind    string           `json:"sourceKind"`
	Source        string           `json:"source"`
	Playlist      string           `json:"playlist"`
	AudioIndex    int              `json:"audioIndex"`
	AudioTrackID  *int             `json:"audioTrackId,omitempty"`
	AudioMode     string           `json:"audioMode"`
	FFmpeg        string           `json:"ffmpeg"`
	DurationMS    int64            `json:"durationMs"`
	ChaptersMS    []int64          `json:"chaptersMs,omitempty"`
	ChapterTitles []string         `json:"chapterTitles,omitempty"`
	VideoRange    string           `json:"videoRange,omitempty"`
	Title         string           `json:"title"`
	Subtitles     []subtitleConfig `json:"subtitles"`
}

type subtitleConfig struct {
	ID       string `json:"id"`
	Label    string `json:"label"`
	Language string `json:"language"`
	VTT      string `json:"vtt"`
}

type validatedConfig struct {
	config
	address  netip.AddrPort
	subnet   netip.Prefix
	mimeType string
}

type initialConfig struct {
	config
	Mode         string `json:"mode"`
	Name         string `json:"name"`
	Store        string `json:"store"`
	Endpoint     string `json:"endpoint"`
	LocalAddress string `json:"localAddress,omitempty"`
	Remember     bool   `json:"remember"`
	Fresh        bool   `json:"fresh"`
}

func readConfig(input *bufio.Reader) (initialConfig, error) {
	line, err := readBoundedLine(input)
	if err != nil {
		return initialConfig{}, err
	}
	var cfg initialConfig
	if !utf8.Valid(line) || json.Unmarshal(line, &cfg) != nil {
		return initialConfig{}, errInvalidConfig
	}
	return cfg, nil
}

func readBoundedLine(input *bufio.Reader) ([]byte, error) {
	var line []byte
	for {
		part, err := input.ReadSlice('\n')
		if len(line)+len(part) > maxConfigBytes+1 {
			return nil, errInvalidConfig
		}
		line = append(line, part...)
		if err == nil {
			return line, nil
		}
		if err == io.EOF && len(line) == 0 {
			return nil, io.EOF
		}
		if err != bufio.ErrBufferFull {
			return nil, errInvalidConfig
		}
	}
}

func (cfg config) validate() (validatedConfig, error) {
	addr, subnet, err := validateLAN(cfg.Listen, cfg.Subnet, cfg.PeerSubnet)
	if err != nil {
		return validatedConfig{}, err
	}
	if cfg.DurationMS < 0 || len(cfg.Title) > 1024 {
		return validatedConfig{}, errInvalidConfig
	}
	if len(cfg.ChaptersMS) > 512 || !slices.IsSorted(cfg.ChaptersMS) {
		return validatedConfig{}, errInvalidConfig
	}
	for _, position := range cfg.ChaptersMS {
		if position < 0 || position >= cfg.DurationMS {
			return validatedConfig{}, errInvalidConfig
		}
	}
	if len(cfg.ChapterTitles) != 0 && len(cfg.ChapterTitles) != len(cfg.ChaptersMS) {
		return validatedConfig{}, errInvalidConfig
	}
	titleBytes := 0
	for _, title := range cfg.ChapterTitles {
		titleBytes += len(title)
		if len(title) > 640 {
			return validatedConfig{}, errInvalidConfig
		}
	}
	if titleBytes > 16384 {
		return validatedConfig{}, errInvalidConfig
	}
	cfg.ChaptersMS = slices.Compact(slices.Clone(cfg.ChaptersMS))
	if len(cfg.ChapterTitles) != len(cfg.ChaptersMS) {
		cfg.ChapterTitles = nil
	}
	if cfg.VideoRange == "" {
		cfg.VideoRange = "unknown"
	}
	if !slices.Contains([]string{"sdr", "pq", "hlg", "unknown"}, cfg.VideoRange) {
		return validatedConfig{}, errInvalidConfig
	}
	if !validSubtitles(cfg.Subtitles) {
		return validatedConfig{}, errInvalidSubtitle
	}

	var mimeType string
	switch cfg.SourceKind {
	case "file":
		mimeType = fileMediaType(filepath.Ext(cfg.Source))
		if mimeType == "" {
			return validatedConfig{}, errInvalidSource
		}
		cfg.Source, err = validateLocalPath(cfg.Source, false)
		if cfg.FFmpeg != "" {
			if !validAudioSelection(cfg) {
				return validatedConfig{}, errInvalidConfig
			}
			// Original file sharing can continue without the optional encoder.
			cfg.FFmpeg, _ = validateFFmpeg(cfg.FFmpeg)
		}
	case "bluray":
		mimeType = matroskaMIME
		cfg, err = validateDisc(cfg)
	default:
		err = errInvalidSource
	}
	if err != nil {
		return validatedConfig{}, err
	}
	return validatedConfig{config: cfg, address: addr, subnet: subnet, mimeType: mimeType}, nil
}

func validateLAN(listen, network, peerNetwork string) (netip.AddrPort, netip.Prefix, error) {
	addr, addrErr := netip.ParseAddrPort(listen)
	subnet, subnetErr := netip.ParsePrefix(network)
	if addrErr != nil || subnetErr != nil || !allowedPeer(addr.Addr(), subnet) ||
		subnet != subnet.Masked() || !assignedIPv4(addr.Addr(), subnet) {
		return netip.AddrPort{}, netip.Prefix{}, errInvalidLAN
	}
	if peerNetwork != "" {
		peers, err := netip.ParsePrefix(peerNetwork)
		if err != nil || peers.Bits() < 24 || !peers.Addr().Is4() ||
			!peers.Addr().IsPrivate() || peers != peers.Masked() || peers.String() != peerNetwork {
			return netip.AddrPort{}, netip.Prefix{}, errInvalidLAN
		}
		subnet = peers
	}
	return addr, subnet, nil
}

// If a subnet is provided, the interface's actual prefix must match it.
func assignedIPv4(ip netip.Addr, subnet netip.Prefix) bool {
	interfaces, err := net.Interfaces()
	if err != nil {
		return false
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
			prefix, err := netip.ParsePrefix(addr.String())
			if err == nil && prefix.Addr() == ip && (!subnet.IsValid() || prefix.Masked() == subnet) {
				return true
			}
		}
	}
	return false
}

func validateDisc(cfg config) (config, error) {
	if !playlistPattern.MatchString(cfg.Playlist) || !validAudioSelection(cfg) ||
		(cfg.AudioMode != "original" && cfg.AudioMode != "compatible") {
		return config{}, errInvalidConfig
	}
	var err error
	cfg.Source, err = validateLocalPath(cfg.Source, true)
	if err != nil {
		return config{}, err
	}
	if err = validateDiscTree(filepath.Join(cfg.Source, "BDMV")); err != nil {
		return config{}, err
	}
	for _, directory := range [...]string{"CERTIFICATE", "AACS", "BDSVM", "BDPLUS"} {
		sibling := filepath.Join(cfg.Source, directory)
		info, err := os.Lstat(sibling)
		if errors.Is(err, fs.ErrNotExist) {
			continue
		}
		if err != nil || !info.IsDir() || validateDiscTree(sibling) != nil {
			return config{}, errInvalidSource
		}
	}
	if _, err = validateLocalPath(filepath.Join(cfg.Source, "BDMV", "PLAYLIST", cfg.Playlist+".mpls"), false); err != nil {
		return config{}, err
	}

	cfg.FFmpeg, err = validateFFmpeg(cfg.FFmpeg)
	if err != nil {
		return config{}, errInvalidSource
	}
	return cfg, nil
}

func validAudioSelection(cfg config) bool {
	return cfg.AudioIndex >= -1 && cfg.AudioIndex <= 8191 &&
		(cfg.AudioTrackID == nil || (*cfg.AudioTrackID >= -1 && *cfg.AudioTrackID <= 8191))
}

func validateFFmpeg(path string) (string, error) {
	resolved, err := validateLocalPath(path, false)
	if err != nil || (runtime.GOOS == "windows" && !strings.EqualFold(filepath.Ext(resolved), ".exe")) {
		return "", errInvalidSource
	}
	return resolved, nil
}

func validateDiscTree(tree string) error {
	return filepath.WalkDir(tree, func(_ string, entry fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return errInvalidSource
		}
		info, err := entry.Info()
		if err != nil || (!info.IsDir() && !info.Mode().IsRegular()) || reparsePoint(info) {
			return errInvalidSource
		}
		return nil
	})
}

func validateLocalPath(path string, directory bool) (string, error) {
	if !filepath.IsAbs(path) || !localVolume(path) {
		return "", errInvalidSource
	}
	path, err := filepath.EvalSymlinks(path)
	if err != nil || !localVolume(path) {
		return "", errInvalidSource
	}
	info, err := os.Stat(path)
	if err != nil || (directory && !info.IsDir()) || (!directory && !info.Mode().IsRegular()) {
		return "", errInvalidSource
	}
	return path, nil
}

func validSubtitles(tracks []subtitleConfig) bool {
	if len(tracks) > maxSubtitles {
		return false
	}
	ids := make(map[string]struct{}, len(tracks))
	for _, track := range tracks {
		if !subtitleIDPattern.MatchString(track.ID) || len(track.Label) > 512 ||
			len(track.Language) > 64 || len(track.VTT) > maxSubtitleBytes || !hasVTTHeader(track.VTT) {
			return false
		}
		if _, duplicate := ids[track.ID]; duplicate {
			return false
		}
		ids[track.ID] = struct{}{}
	}
	return true
}

func hasVTTHeader(text string) bool {
	text = strings.TrimPrefix(text, "\uFEFF")
	header, ok := strings.CutPrefix(text, "WEBVTT")
	return ok && (header == "" || strings.ContainsRune(" \t\r\n", rune(header[0])))
}

func fileMediaType(extension string) string {
	switch strings.ToLower(extension) {
	case ".mkv":
		return matroskaMIME
	case ".mp4", ".m4v":
		return "video/mp4"
	case ".webm":
		return "video/webm"
	case ".mov":
		return "video/quicktime"
	case ".avi":
		return "video/x-msvideo"
	case ".ts", ".m2ts", ".mts":
		return "video/mp2t"
	case ".m4a", ".alac":
		return "audio/mp4"
	case ".flac":
		return "audio/flac"
	case ".mp3":
		return "audio/mpeg"
	case ".wav":
		return "audio/wav"
	default:
		return ""
	}
}
