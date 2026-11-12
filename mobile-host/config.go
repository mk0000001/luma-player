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
	Listen       string           `json:"listen"`
	Subnet       string           `json:"subnet"`
	SourceKind   string           `json:"sourceKind"`
	Source       string           `json:"source"`
	Playlist     string           `json:"playlist"`
	AudioIndex   int              `json:"audioIndex"`
	AudioTrackID *int             `json:"audioTrackId,omitempty"`
	AudioMode    string           `json:"audioMode"`
	FFmpeg       string           `json:"ffmpeg"`
	DurationMS   int64            `json:"durationMs"`
	Title        string           `json:"title"`
	Subtitles    []subtitleConfig `json:"subtitles"`
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
	Mode     string `json:"mode"`
	Name     string `json:"name"`
	Store    string `json:"store"`
	Endpoint string `json:"endpoint"`
	Remember bool   `json:"remember"`
	Fresh    bool   `json:"fresh"`
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
	addr, subnet, err := validateLAN(cfg.Listen, cfg.Subnet)
	if err != nil {
		return validatedConfig{}, err
	}
	if cfg.DurationMS < 0 || len(cfg.Title) > 1024 {
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

func validateLAN(listen, network string) (netip.AddrPort, netip.Prefix, error) {
	addr, addrErr := netip.ParseAddrPort(listen)
	subnet, subnetErr := netip.ParsePrefix(network)
	if addrErr != nil || subnetErr != nil || !allowedPeer(addr.Addr(), subnet) ||
		subnet != subnet.Masked() || !assignedSubnet(addr.Addr(), subnet) {
		return netip.AddrPort{}, netip.Prefix{}, errInvalidLAN
	}
	return addr, subnet, nil
}

func assignedSubnet(ip netip.Addr, subnet netip.Prefix) bool {
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
			if err == nil && prefix.Addr() == ip && prefix.Masked() == subnet {
				return true
			}
		}
	}
	return false
}

func validateDisc(cfg config) (config, error) {
	if !playlistPattern.MatchString(cfg.Playlist) || cfg.AudioIndex < -1 || cfg.AudioIndex > 8191 ||
		(cfg.AudioTrackID != nil && (*cfg.AudioTrackID < -1 || *cfg.AudioTrackID > 8191)) ||
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

	cfg.FFmpeg, err = validateLocalPath(cfg.FFmpeg, false)
	if err != nil || (runtime.GOOS == "windows" && !strings.EqualFold(filepath.Ext(cfg.FFmpeg), ".exe")) {
		return config{}, errInvalidSource
	}
	return cfg, nil
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
