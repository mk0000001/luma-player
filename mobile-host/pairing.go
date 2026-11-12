package main

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"maps"
	"net/http"
	"net/netip"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

const (
	pairingTTL        = 90 * time.Second
	maxPairAttempts   = 32
	maxPendingPerPeer = 3
	maxActiveDevices  = 32
	pairIdentityPath  = "/v1/pair/identity"
	pairStartPath     = "/v1/pair/start"
	pairRevealPath    = "/v1/pair/reveal"
	pairPollPath      = "/v1/pair/poll"
	pairLoginPath     = "/v1/pair/login"
)

var errPairDecision = errors.New("pairing request is no longer available")

type pairStartRequest struct {
	DeviceID   string `json:"deviceId"`
	Device     string `json:"device"`
	Commitment string `json:"commitment"`
	Remember   bool   `json:"remember"`
}

type pairStartResponse struct {
	ID          string `json:"id"`
	ServerNonce string `json:"serverNonce"`
}

type pairRevealRequest struct {
	ID          string `json:"id"`
	ClientNonce string `json:"clientNonce"`
}

type pairPollResponse struct {
	State         string `json:"state"`
	Token         string `json:"token,omitempty"`
	RememberToken string `json:"rememberToken,omitempty"`
}

type pairAttempt struct {
	pairStartRequest
	peer        netip.Addr
	serverNonce string
	expires     time.Time
	revealed    bool
	result      pairPollResponse
}

func (p *player) beginPairing(peer netip.Addr, request pairStartRequest) (pairStartResponse, int) {
	if !validPairValue(request.DeviceID, 16) || !validPairValue(request.Commitment, 32) ||
		!validDeviceName(request.Device) {
		return pairStartResponse{}, http.StatusBadRequest
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	p.expireAttempts(now)
	pending := 0
	for _, attempt := range p.attempts {
		if attempt.peer == peer && attempt.result.State == "pending" {
			pending++
		}
	}
	if len(p.attempts) >= maxPairAttempts || pending >= maxPendingPerPeer || !p.startRate.allow(peer, now) {
		return pairStartResponse{}, http.StatusTooManyRequests
	}
	id, err := pairRandom(16)
	if err != nil {
		return pairStartResponse{}, http.StatusServiceUnavailable
	}
	nonce, err := pairRandom(32)
	if err != nil {
		return pairStartResponse{}, http.StatusServiceUnavailable
	}
	p.attempts[id] = &pairAttempt{
		pairStartRequest: request, peer: peer, serverNonce: nonce, expires: now.Add(pairingTTL),
		result: pairPollResponse{State: "pending"},
	}
	return pairStartResponse{ID: id, ServerNonce: nonce}, http.StatusOK
}

func (p *player) revealPairing(peer netip.Addr, request pairRevealRequest) int {
	if !validPairValue(request.ID, 16) || !validPairValue(request.ClientNonce, 32) {
		return http.StatusBadRequest
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.expireAttempts(p.now())
	attempt, exists := p.attempts[request.ID]
	if !exists || attempt.peer != peer {
		return http.StatusNotFound
	}
	expected := pairCommitment(p.pin, attempt.DeviceID, request.ClientNonce)
	if subtle.ConstantTimeCompare([]byte(expected), []byte(attempt.Commitment)) != 1 {
		return http.StatusBadRequest
	}
	if attempt.revealed {
		return http.StatusOK
	}
	attempt.revealed = true
	p.announce(playerStatus{
		Status: "pairRequest", ID: request.ID, Device: attempt.Device,
		Code: pairSAS(p.pin, attempt.DeviceID, request.ClientNonce, attempt.serverNonce), Remember: &attempt.Remember,
	})
	return http.StatusOK
}

func (p *player) pollPairing(peer netip.Addr, id string) pairPollResponse {
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	p.expireAttempts(now)
	p.expireDevices(now)
	attempt, exists := p.attempts[id]
	if !exists || attempt.peer != peer {
		return pairPollResponse{State: "denied"}
	}
	return attempt.result
}

func (p *player) decidePairing(id, decision string) (playerStatus, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	p.expireAttempts(now)
	p.expireDevices(now)
	attempt, exists := p.attempts[id]
	if !exists || !attempt.revealed || attempt.result.State != "pending" {
		return playerStatus{}, errPairDecision
	}
	if decision == "deny" {
		attempt.result.State = "denied"
		return playerStatus{}, nil
	}
	remember := decision == "remember"
	if (!remember && decision != "once") || (remember && !attempt.Remember) {
		return playerStatus{}, errPairDecision
	}
	if _, exists := p.active[attempt.DeviceID]; !exists && len(p.active) >= maxActiveDevices {
		return playerStatus{}, errors.New("too many approved devices")
	}
	token, err := pairRandom(16)
	if err != nil {
		return playerStatus{}, errPairDecision
	}
	result := pairPollResponse{State: "approved", Token: token}
	if remember {
		if _, exists := p.remembered[attempt.DeviceID]; !exists && len(p.remembered) >= maxRememberedDevices {
			return playerStatus{}, errors.New("too many remembered devices")
		}
		result.RememberToken, err = pairRandom(32)
		if err != nil {
			return playerStatus{}, errCredentialStore
		}
		next := maps.Clone(p.remembered)
		record := rememberedDevice{Name: attempt.Device, Hash: digestPairValue(result.RememberToken)}
		if attempt.peer.IsPrivate() {
			record.Address = attempt.peer.String()
		}
		next[attempt.DeviceID] = record
		if err := p.persist(next); err != nil {
			return playerStatus{}, errCredentialStore
		}
		p.remembered = next
	}
	p.activateDevice(attempt.DeviceID, attempt.Device, attempt.peer, token)
	attempt.result = result
	return playerStatus{Status: "deviceConnected", Device: attempt.Device, DeviceID: attempt.DeviceID, Remember: &remember}, nil
}

func (p *player) login(peer netip.Addr, deviceID, secret string) (string, int) {
	if !validPairValue(deviceID, 16) || !validPairValue(secret, 32) {
		return "", http.StatusUnauthorized
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	p.expireDevices(now)
	if !p.loginRate.allow(peer, now) {
		return "", http.StatusTooManyRequests
	}
	device, exists := p.remembered[deviceID]
	hash := digestPairValue(secret)
	if !exists || subtle.ConstantTimeCompare([]byte(device.Hash), []byte(hash)) != 1 {
		return "", http.StatusUnauthorized
	}
	if _, exists := p.active[deviceID]; !exists && len(p.active) >= maxActiveDevices {
		return "", http.StatusServiceUnavailable
	}
	token, err := pairRandom(16)
	if err != nil {
		return "", http.StatusServiceUnavailable
	}
	p.activateDevice(deviceID, device.Name, peer, token)
	if peer.IsPrivate() {
		device.Address = peer.String()
		p.remembered[deviceID] = device
	}
	remember := true
	p.announce(playerStatus{Status: "deviceConnected", Device: device.Name, DeviceID: deviceID, Remember: &remember})
	return token, http.StatusOK
}

func (p *player) expireAttempts(now time.Time) {
	for id, attempt := range p.attempts {
		if !now.Before(attempt.expires) {
			delete(p.attempts, id)
		}
	}
}

func (p *player) clearDeviceApprovals(deviceID string) {
	for id, attempt := range p.attempts {
		if attempt.DeviceID == deviceID && attempt.result.State == "approved" {
			delete(p.attempts, id)
		}
	}
}

func validDeviceName(value string) bool {
	return len(value) > 0 && len(value) <= 128 && utf8.ValidString(value) && strings.TrimSpace(value) != "" &&
		strings.IndexFunc(value, unicode.IsControl) < 0
}

func validPairValue(value string, size int) bool {
	if len(value) != base64.RawURLEncoding.EncodedLen(size) {
		return false
	}
	var decoded [32]byte
	count, err := base64.RawURLEncoding.Strict().Decode(decoded[:], []byte(value))
	return err == nil && count == size
}

func pairRandom(size int) (string, error) {
	var random [32]byte
	if _, err := rand.Read(random[:size]); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(random[:size]), nil
}

func digestPairValue(value string) string {
	digest := sha256.Sum256([]byte(value))
	return base64.RawURLEncoding.EncodeToString(digest[:])
}

func pairCommitment(pin, id, clientNonce string) string {
	return digestPairValue("luma-pair-commit-v1\n" + pin + "\n" + id + "\n" + clientNonce)
}

func pairSAS(pin, id, clientNonce, serverNonce string) string {
	digest := sha256.Sum256([]byte("luma-pair-code-v1\n" + pin + "\n" + id + "\n" + clientNonce + "\n" + serverNonce))
	return fmt.Sprintf("%06d", binary.BigEndian.Uint32(digest[:4])%1_000_000)
}

type rateWindow struct {
	until time.Time
	used  int
}

type peerRate struct {
	window    time.Duration
	peerLimit int
	total     int
	global    rateWindow
	peers     map[netip.Addr]rateWindow
}

func (r *peerRate) allow(peer netip.Addr, now time.Time) bool {
	if !now.Before(r.global.until) {
		r.global = rateWindow{until: now.Add(r.window)}
	}
	if r.global.used >= r.total {
		return false
	}
	budget := r.peers[peer]
	if !now.Before(budget.until) {
		budget = rateWindow{until: now.Add(r.window)}
	}
	if budget.used >= r.peerLimit {
		return false
	}
	if r.peers == nil {
		r.peers = make(map[netip.Addr]rateWindow)
	} else if len(r.peers) >= 128 {
		for addr, window := range r.peers {
			if !now.Before(window.until) {
				delete(r.peers, addr)
			}
		}
		if _, exists := r.peers[peer]; !exists && len(r.peers) >= 128 {
			return false
		}
	}
	budget.used++
	r.peers[peer] = budget
	r.global.used++
	return true
}
