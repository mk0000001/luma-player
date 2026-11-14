package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"os"
	"path/filepath"
	"slices"
)

const blurayPacketBytes = 192

// The byte offset is valid for a single, untrimmed clip. Other playlists
// continue to use FFmpeg's time-based seek across the virtual title.
func blurayChapterOffset(cfg validatedConfig, startMS int64) int64 {
	if cfg.SourceKind != "bluray" || startMS <= 0 || !slices.Contains(cfg.ChaptersMS, startMS) || !playlistPattern.MatchString(cfg.Playlist) {
		return 0
	}
	root := filepath.Join(cfg.Source, "BDMV")
	playlist := readDiscMetadata(filepath.Join(root, "PLAYLIST", cfg.Playlist+".mpls"))
	if len(playlist) < 20 || !bytes.HasPrefix(playlist, []byte("MPLS")) {
		return 0
	}
	items := discSection(playlist, be32(playlist[8:]))
	if len(items) < 28 || be16(items[2:]) != 1 {
		return 0
	}
	length := be16(items[6:])
	if length < 20 || length > len(items)-8 {
		return 0
	}
	item := items[8 : 8+length]
	clip := string(item[:5])
	if !playlistPattern.MatchString(clip) || string(item[5:9]) != "M2TS" || item[10]&0x10 != 0 || item[11] != 0 {
		return 0
	}
	in, out := int64(be32(item[12:])), int64(be32(item[16:]))
	if out <= in || startMS >= (out-in)/45 {
		return 0
	}

	marks := discSection(playlist, be32(playlist[12:]))
	if len(marks) < 2 || be16(marks) > (len(marks)-2)/14 {
		return 0
	}
	for i := range be16(marks) {
		mark := marks[2+i*14:][:14]
		ticks := int64(be32(mark[4:]))
		// libVLC reports chapter positions in milliseconds. Permit its
		// conversion to round by one millisecond.
		delta := (ticks-in)/45 - startMS
		if mark[1] == 1 && be16(mark[2:]) == 0 && ticks > in && ticks < out && delta >= -1 && delta <= 1 {
			info := readDiscMetadata(filepath.Join(root, "CLIPINF", clip+".clpi"))
			return clipEntryOffset(info, in, ticks)
		}
	}
	return 0
}

func clipEntryOffset(info []byte, in, chapter int64) int64 {
	if len(info) < 20 || !bytes.HasPrefix(info, []byte("HDMV")) {
		return 0
	}
	cpi := discSection(info, be32(info[16:]))
	if len(cpi) < 20 || cpi[1]&0xf != 1 || cpi[3] != 1 {
		return 0
	}
	entry := cpi[4:16]
	coarseCount := int(be32(entry[2:])>>2) & 0xffff
	fineCount := int(be32(entry[4:])) & 0x3ffff
	offset := be32(entry[8:]) + 2
	if offset < 16 || offset > len(cpi)-4 || coarseCount == 0 || fineCount == 0 ||
		(entry[3]>>2)&0xf != 1 {
		return 0
	}
	table := cpi[offset:]
	fineStart := be32(table)
	if fineStart < 4+coarseCount*8 || fineStart > len(table) || fineCount > (len(table)-fineStart)/4 {
		return 0
	}
	first := true
	var previousTicks, previousPacket int64
	var matched int64
	for i := range coarseCount {
		coarse := table[4+i*8:][:8]
		timing := be32(coarse)
		start := timing >> 14
		end := fineCount
		if i+1 < coarseCount {
			end = be32(table[12+i*8:]) >> 14
		}
		if start >= end || end > fineCount || (i == 0 && start != 0) {
			return 0
		}
		for j := start; j < end; j++ {
			fine := be32(table[fineStart+j*4:])
			ticks := (int64(timing&0x3ffe) << 18) + (int64(fine>>17&0x7ff) << 8)
			packet := int64(be32(coarse[4:])&^0x1ffff) + int64(fine&0x1ffff)
			if first {
				// libbluray starts on the first 32-packet unit. An entry
				// near the clip start in that unit establishes the origin.
				if packet >= 32 || ticks > in || in-ticks >= 256 {
					return 0
				}
				first = false
			} else if ticks < previousTicks || packet <= previousPacket {
				return 0
			}
			if ticks <= chapter && chapter-ticks < 256 {
				matched = packet
			}
			previousTicks, previousPacket = ticks, packet
		}
	}
	return matched * blurayPacketBytes
}

func readDiscMetadata(path string) []byte {
	const limit = 4 * 1024 * 1024
	path, err := validateLocalPath(path, false)
	if err != nil {
		return nil
	}
	file, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer file.Close()
	data, err := io.ReadAll(io.LimitReader(file, limit+1))
	if err != nil || len(data) > limit {
		return nil
	}
	return data
}

func discSection(data []byte, offset int) []byte {
	if offset < 0 || offset > len(data)-4 {
		return nil
	}
	size := be32(data[offset:])
	if size < 0 || size > len(data)-offset-4 {
		return nil
	}
	return data[offset+4 : offset+4+size]
}

func be16(data []byte) int { return int(binary.BigEndian.Uint16(data)) }
func be32(data []byte) int { return int(binary.BigEndian.Uint32(data)) }
