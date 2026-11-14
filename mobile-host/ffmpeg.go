package main

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"io"
	"math/bits"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"sync/atomic"
	"time"
)

const (
	ffmpegStartupTimeout = 60 * time.Second
	maxMatroskaHeader    = 1024 * 1024
)

type remuxOutcome int

const (
	remuxFailed remuxOutcome = iota
	remuxNeedsPCM
	remuxStreamed
	remuxStartupTimeout
)

func (s *mediaSession) serveDisc(w *responseWriter, ctx context.Context) {
	s.serveProgressive(w, ctx, originalProfile, 0)
}

func (s *mediaSession) serveProgressive(w *responseWriter, ctx context.Context, profile string, start int64) {
	offset := blurayChapterOffset(s.config, start)
	args := variantArguments(s.config, profile, start, false, offset)
	optical := s.config.SourceKind == "bluray"
	remux := func(args []string) remuxOutcome {
		if !optical || !s.config.LiveCaptions || s.config.AudioIndex < 0 && s.config.AudioTrackID == nil {
			return streamMatroska(ctx, w, s.config.FFmpeg, s.file, args, s.reportStream, optical, nil)
		}
		audio := new(captionAudioBuffer)
		audio.reset(start)
		s.captionAudio.Store(audio)
		// Stdout is Matroska and stderr is raw PCM. The inherited handles work on
		// Windows, with no separate optical reader or local network listener.
		withAudio := append(slices.Clone(args), captionAudioOutput(s.config)...)
		outcome := streamMatroska(ctx, w, s.config.FFmpeg, s.file, withAudio, s.reportStream, optical, audio)
		audio.finish()
		if outcome == remuxFailed && ctx.Err() == nil {
			outcome = streamMatroska(ctx, w, s.config.FFmpeg, s.file, args, s.reportStream, optical, nil)
		}
		return outcome
	}
	outcome := remux(args)
	if outcome == remuxFailed && ctx.Err() == nil && offset > 0 {
		offset = 0
		outcome = remux(variantArguments(s.config, profile, start, false, 0))
	}
	pcm := outcome == remuxNeedsPCM && profile == originalProfile
	if pcm && ctx.Err() == nil {
		outcome = remux(variantArguments(s.config, profile, start, true, offset))
	}
	if outcome != remuxStreamed && ctx.Err() == nil {
		if s.reportStream != nil {
			reason := "startup-failed"
			if outcome == remuxStartupTimeout {
				reason = "startup-timeout"
			}
			s.reportStream(reason)
		}
		http.Error(w, "could not start the selected media stream", http.StatusServiceUnavailable)
	}
}

func captionAudioOutput(cfg validatedConfig, options ...string) []string {
	args := []string{"-map", selectedAudioMap(cfg), "-vn", "-sn", "-dn",
		"-af", "aresample=16000:async=1:first_pts=0", "-ac", "1", "-c:a", "pcm_s16le",
		"-f", "s16le", "-flush_packets", "1", "-loglevel", "quiet"}
	return append(append(args, options...), "pipe:2")
}

func streamMatroska(parent context.Context, w *responseWriter, ffmpeg string, input *os.File, args []string, report func(string), optical bool, captionAudio *captionAudioBuffer) remuxOutcome {
	ctx, cancel := context.WithCancel(parent)
	defer cancel()
	var timedOut atomic.Bool
	timer := time.AfterFunc(ffmpegStartupTimeout, func() {
		timedOut.Store(true)
		cancel()
	})
	defer timer.Stop()

	command := exec.CommandContext(ctx, ffmpeg, args...)
	if input != nil {
		// Inherit the already-authorized file handle. The fd protocol supports
		// seeking a regular file without opening a mutable pathname again.
		if _, err := input.Seek(0, io.SeekStart); err != nil {
			return remuxFailed
		}
		command.Stdin = input
	}
	command.Dir = filepath.Dir(ffmpeg)
	command.WaitDelay = 2 * time.Second
	hideProcess(command)
	stdout, err := command.StdoutPipe()
	if err != nil {
		return remuxFailed
	}
	defer stdout.Close()
	processDone := make(chan struct{})
	defer close(processDone)
	var stopPipeClose func() bool
	if optical {
		stdin, err := command.StdinPipe()
		if err != nil {
			return remuxFailed
		}
		defer stdin.Close()
		command.Cancel = func() error {
			// Let FFmpeg unwind its optical reads before WaitDelay can kill it.
			_, err := io.WriteString(stdin, "q\n")
			if errors.Is(err, os.ErrClosed) {
				return os.ErrProcessDone
			}
			return err
		}
		stopPipeClose = context.AfterFunc(ctx, func() {
			timer := time.AfterFunc(command.WaitDelay, func() { _ = stdout.Close() })
			defer timer.Stop()
			<-processDone
		})
	} else {
		stopPipeClose = context.AfterFunc(ctx, func() { _ = stdout.Close() })
	}
	defer stopPipeClose()
	var diagnostics ffmpegDiagnostics
	command.Stderr = &diagnostics
	if captionAudio != nil {
		command.Stderr = captionAudio
	}
	if command.Start() != nil {
		return remuxFailed
	}

	prefix, err := readMatroskaPrefix(stdout)
	if err != nil {
		if err != io.EOF && err != io.ErrUnexpectedEOF {
			cancel()
		}
		waitErr := command.Wait()
		if timedOut.Load() {
			return remuxStartupTimeout
		}
		if waitErr != nil && ctx.Err() == nil &&
			bytes.Contains(diagnostics.data, []byte("No wav codec tag found for codec pcm_bluray")) {
			return remuxNeedsPCM
		}
		return remuxFailed
	}
	timer.Stop()

	w.Header().Set("Content-Type", matroskaMIME)
	w.writeWait = progressiveWriteWait
	_, err = w.Write(prefix)
	if err == nil {
		err = w.controller.Flush()
	}
	if err == nil {
		copyInput := &idleMediaReader{source: stdout, onIdle: func() {
			cancel()
			if report != nil && parent.Err() == nil {
				report("stream-read-timeout")
			}
		}}
		_, err = io.Copy(w, copyInput)
	}
	if err != nil {
		cancel()
		if optical {
			// Discard remaining output so FFmpeg can finish its normal shutdown.
			_, _ = io.Copy(io.Discard, stdout)
		}
	}
	exitTimer := time.AfterFunc(3*time.Second, cancel)
	defer exitTimer.Stop()
	if waitErr := command.Wait(); (waitErr != nil || err != nil) && parent.Err() == nil {
		panic(http.ErrAbortHandler)
	}
	return remuxStreamed
}

type ffmpegDiagnostics struct {
	data []byte
}

func (d *ffmpegDiagnostics) Write(data []byte) (int, error) {
	const limit = 32 * 1024
	d.data = append(d.data, data[:min(len(data), limit-len(d.data))]...)
	return len(data), nil
}

var errInvalidMatroska = errors.New("invalid Matroska stream")

// FFmpeg can write the EBML and Segment headers before rejecting an audio codec.
// Keep the response uncommitted until its first Cluster has been written.
func readMatroskaPrefix(stdout io.Reader) ([]byte, error) {
	const (
		ebml    = 0x1a45dfa3
		segment = 0x18538067
		cluster = 0x1f43b675
	)
	var prefix bytes.Buffer
	limited := &io.LimitedReader{R: stdout, N: maxMatroskaHeader}
	reader := bufio.NewReader(io.TeeReader(limited, &prefix))
	id, size, known, err := readEBMLElement(reader)
	if err != nil {
		return nil, err
	}
	if id != ebml || !known || size > maxMatroskaHeader {
		return nil, errInvalidMatroska
	}
	if _, err = io.CopyN(io.Discard, reader, int64(size)); err != nil {
		return nil, err
	}
	id, _, _, err = readEBMLElement(reader)
	if err != nil {
		return nil, err
	}
	if id != segment {
		return nil, errInvalidMatroska
	}
	for {
		id, size, known, err = readEBMLElement(reader)
		if err != nil {
			if limited.N == 0 {
				return nil, errInvalidMatroska
			}
			return nil, err
		}
		if id == cluster {
			return prefix.Bytes(), nil
		}
		if !known || size > uint64(limited.N)+uint64(reader.Buffered()) {
			return nil, errInvalidMatroska
		}
		if _, err = io.CopyN(io.Discard, reader, int64(size)); err != nil {
			return nil, err
		}
	}
}

func readEBMLElement(reader io.ByteReader) (id uint64, size uint64, known bool, err error) {
	id, _, err = readEBMLInteger(reader, 4, false)
	if err != nil {
		return 0, 0, false, err
	}
	size, bytes, err := readEBMLInteger(reader, 8, true)
	return id, size, size != (1<<(7*bytes))-1, err
}

func readEBMLInteger(reader io.ByteReader, maximum int, removeMarker bool) (uint64, int, error) {
	first, err := reader.ReadByte()
	if err != nil {
		return 0, 0, err
	}
	length := bits.LeadingZeros8(first) + 1
	if length > maximum {
		return 0, 0, errInvalidMatroska
	}
	if removeMarker {
		first &= 0xff >> length
	}
	value := uint64(first)
	for index := 1; index < length; index++ {
		next, err := reader.ReadByte()
		if err != nil {
			return 0, 0, err
		}
		value = value<<8 | uint64(next)
	}
	return value, length, nil
}
