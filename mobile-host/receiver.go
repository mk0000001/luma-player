package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"
	"unicode"
)

var (
	errReceiverPair    = errors.New("could not pair with the remote PC")
	errPairingDenied   = errors.New("the remote PC denied or expired the pairing request")
	errReceiverConfirm = errors.New("the pairing confirmation expired")
)

type receiverStatus struct {
	Status     string              `json:"status"`
	Code       string              `json:"code,omitempty"`
	Name       string              `json:"name,omitempty"`
	URL        string              `json:"url,omitempty"`
	Title      string              `json:"title,omitempty"`
	MIMEType   string              `json:"mimeType,omitempty"`
	DurationMS int64               `json:"durationMs,omitempty"`
	Seekable   *bool               `json:"seekable,omitempty"`
	Subtitles  *[]receiverSubtitle `json:"subtitles,omitempty"`
	Message    string              `json:"message,omitempty"`
}

type receiverSubtitle struct {
	URL   string `json:"url"`
	Label string `json:"label"`
}

type receiverApproval struct {
	ready   atomic.Bool
	confirm chan struct{}
}

func (approval *receiverApproval) readCommands(input *bufio.Reader, cancel context.CancelFunc) {
	defer cancel()
	for {
		line, err := readBoundedLine(input)
		if err != nil {
			return
		}
		var command struct{ Command string }
		if json.Unmarshal(line, &command) == nil && command.Command == "confirm" && approval.ready.Load() {
			select {
			case approval.confirm <- struct{}{}:
			default:
			}
		}
	}
}

func runReceiver(input *bufio.Reader, stdout io.Writer, raw initialConfig,
	dial func(context.Context, string, string) (net.Conn, error)) int {
	out := json.NewEncoder(stdout)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	fail := func(status, message string) int {
		if ctx.Err() != nil {
			return 0
		}
		_ = out.Encode(receiverStatus{Status: status, Message: message})
		if status == "pairingRequired" {
			return 0
		}
		return 1
	}
	endpoint, err := receiverEndpoint(raw.Endpoint)
	if err != nil || !validDeviceName(raw.Name) || !filepath.IsAbs(raw.Store) || !localVolume(raw.Store) {
		return fail("error", errInvalidConfig.Error())
	}

	approval := receiverApproval{confirm: make(chan struct{}, 1)}
	go approval.readCommands(input, cancel)
	var saved outboundPeer
	var remembered bool
	if !raw.Fresh {
		saved, remembered, err = rememberedPC(raw.Store, endpoint.String())
		if err != nil {
			return fail("pairingRequired", errCredentialStore.Error())
		}
	}

	var name, token string
	var peer *receiverPeer
	if remembered {
		peer = newReceiverPeer(endpoint, saved.Pin, dial)
		defer peer.transport.CloseIdleConnections()
		_, name, err = peer.identity(ctx)
		if err == nil {
			token, err = peer.rememberedLogin(ctx, saved)
		}
		if err != nil {
			return fail("pairingRequired", "the remembered PC could not be authenticated")
		}
	} else {
		first := newReceiverPeer(endpoint, "", dial)
		var pin string
		pin, name, err = first.identity(ctx)
		first.transport.CloseIdleConnections()
		if err != nil {
			return fail("error", "could not connect to the remote PC")
		}
		peer = newReceiverPeer(endpoint, pin, dial)
		defer peer.transport.CloseIdleConnections()
		deviceID, err := pairRandom(16)
		if err != nil {
			return fail("error", errReceiverPair.Error())
		}
		result, err := peer.pair(ctx, pin, deviceID, raw.Name, raw.Remember, &approval,
			func(code string) error { return out.Encode(receiverStatus{Status: "pairCode", Code: code, Name: name}) })
		if err != nil {
			return fail("error", err.Error())
		}
		token = result.Token
		if result.RememberToken != "" {
			err := rememberPC(raw.Store, endpoint.String(), outboundPeer{
				Pin: pin, DeviceID: deviceID, Token: result.RememberToken,
			})
			if err != nil {
				return fail("error", errCredentialStore.Error())
			}
		}
	}

	if ctx.Err() != nil {
		return 0
	}
	bridge, err := startPlaybackBridge(ctx, peer, token)
	if err != nil {
		return fail("error", "could not start local playback")
	}
	defer bridge.close()
	if out.Encode(receiverStatus{Status: "connected", Name: name}) != nil {
		return 1
	}
	if err := bridge.watch(ctx, out); err != nil && ctx.Err() == nil {
		_ = out.Encode(receiverStatus{Status: "ended"})
	}
	return 0
}

func (p *receiverPeer) rememberedLogin(ctx context.Context, saved outboundPeer) (string, error) {
	var login struct{ Token string }
	result, err := p.json(ctx, http.MethodPost, pairLoginPath, saved.Token,
		map[string]string{"deviceId": saved.DeviceID}, &login, maxPairBody)
	if err != nil || result.status != http.StatusOK || !validPairValue(login.Token, 16) {
		return "", errReceiverPair
	}
	return login.Token, nil
}

func (p *receiverPeer) pair(parent context.Context, pin, deviceID, name string, remember bool,
	approval *receiverApproval, announce func(string) error) (pairPollResponse, error) {
	ctx, cancel := context.WithTimeout(parent, pairingTTL)
	defer cancel()
	clientNonce, err := pairRandom(32)
	if err != nil {
		return pairPollResponse{}, errReceiverPair
	}
	var started pairStartResponse
	response, err := p.json(ctx, http.MethodPost, pairStartPath, "", pairStartRequest{
		DeviceID: deviceID, Device: name, Remember: remember, Commitment: pairCommitment(pin, deviceID, clientNonce),
	}, &started, maxPairBody)
	if err != nil || response.status != http.StatusOK || !validPairValue(started.ID, 16) ||
		!validPairValue(started.ServerNonce, 32) {
		return pairPollResponse{}, errReceiverPair
	}
	var revealed pairPollResponse
	response, err = p.json(ctx, http.MethodPost, pairRevealPath, "",
		pairRevealRequest{ID: started.ID, ClientNonce: clientNonce}, &revealed, maxPairBody)
	if err != nil || response.status != http.StatusOK || revealed.State != "pending" {
		return pairPollResponse{}, errReceiverPair
	}
	approval.ready.Store(true)
	defer approval.ready.Store(false)
	if announce(pairSAS(pin, deviceID, clientNonce, started.ServerNonce)) != nil {
		return pairPollResponse{}, errReceiverPair
	}

	ticker := time.NewTicker(750 * time.Millisecond)
	defer ticker.Stop()
	var approved pairPollResponse
	confirmed := false
	for {
		if approved.State == "" {
			var result pairPollResponse
			response, err := p.json(ctx, http.MethodPost, pairPollPath, "", map[string]string{"id": started.ID}, &result, maxPairBody)
			if err != nil || response.status != http.StatusOK {
				return pairPollResponse{}, errReceiverPair
			}
			switch result.State {
			case "approved":
				if !validPairValue(result.Token, 16) ||
					(result.RememberToken != "" && (!remember || !validPairValue(result.RememberToken, 32))) {
					return pairPollResponse{}, errReceiverPair
				}
				approved = result
			case "denied":
				return pairPollResponse{}, errPairingDenied
			case "pending":
			default:
				return pairPollResponse{}, errReceiverPair
			}
		}
		if confirmed && approved.State != "" {
			return approved, nil
		}
		select {
		case <-ctx.Done():
			return pairPollResponse{}, errReceiverConfirm
		case <-approval.confirm:
			confirmed = true
		case <-ticker.C:
		}
	}
}

func sanitizeReceiverText(text string) string {
	return strings.TrimSpace(strings.Map(func(character rune) rune {
		if unicode.IsControl(character) || unicode.Is(unicode.Cf, character) {
			return -1
		}
		return character
	}, text))
}
