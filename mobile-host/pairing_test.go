package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"net/http"
	"net/netip"
	"testing"
	"time"
)

func TestPairingBindsProofAndApprovalToAttemptAndPeer(t *testing.T) {
	peer := netip.MustParseAddr("192.168.22.4")
	other := netip.MustParseAddr("192.168.22.5")
	pin := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{0x7c}, 32))
	nonce := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{0x8f}, 32))
	commitment := sha256.Sum256([]byte("luma-pair-commit-v1\n" + pin + "\n" + testPlayerDeviceID + "\n" + nonce))
	var announced []playerStatus
	p := newPlayer("PC", pin, make(map[string]rememberedDevice), nil, func(status playerStatus) {
		announced = append(announced, status)
	})
	now := time.Now()
	p.now = func() time.Time { return now }
	request := pairStartRequest{
		DeviceID: testPlayerDeviceID, Device: "Phone",
		Commitment: base64.RawURLEncoding.EncodeToString(commitment[:]),
	}
	started, status := p.beginPairing(peer, request)
	if status != http.StatusOK {
		t.Fatalf("could not start a valid attempt: %d", status)
	}
	if _, err := p.decidePairing(started.ID, "once"); err == nil {
		t.Fatal("unrevealed attempt could be approved")
	}
	reveal := pairRevealRequest{ID: started.ID, ClientNonce: nonce}
	if status := p.revealPairing(other, reveal); status != http.StatusNotFound {
		t.Fatal("a different peer revealed the attempt")
	}
	forged := reveal
	forged.ClientNonce = base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{0x92}, 32))
	if status := p.revealPairing(peer, forged); status != http.StatusBadRequest || len(announced) != 0 {
		t.Fatal("a forged nonce could prompt the PC")
	}
	if status := p.revealPairing(peer, reveal); status != http.StatusOK || len(announced) != 1 {
		t.Fatal("valid proof did not prompt the PC")
	}
	if status := p.revealPairing(peer, reveal); status != http.StatusOK || len(announced) != 1 {
		t.Fatal("retried reveal prompted the PC twice")
	}
	if _, err := p.decidePairing(started.ID, "remember"); err == nil {
		t.Fatal("PC could remember a phone that requested a one-time connection")
	}
	if _, err := p.decidePairing(started.ID, "once"); err != nil {
		t.Fatal(err)
	}
	if result := p.pollPairing(other, started.ID); result.State != "denied" || result.Token != "" {
		t.Fatal("a different peer could poll an approved token")
	}
	result := p.pollPairing(peer, started.ID)
	if _, device := p.authorize(peer, result.Token, false); device == nil {
		t.Fatal("approved peer could not use its token")
	}
	if _, device := p.authorize(other, result.Token, false); device != nil {
		t.Fatal("approved token worked from a different peer")
	}
	now = now.Add(90 * time.Second)
	if result := p.pollPairing(peer, started.ID); result.State != "denied" || result.Token != "" {
		t.Fatal("the attempt survived its deadline")
	}
}

func TestPairingCapsPendingAttemptsPerPeer(t *testing.T) {
	p := newPlayer("PC", "", nil, nil, nil)
	request := pairStartRequest{
		DeviceID: testPlayerDeviceID, Device: "Phone",
		Commitment: base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{0x23}, 32)),
	}
	peer := netip.MustParseAddr("192.168.22.4")
	for index := 0; index < 3; index++ {
		if _, status := p.beginPairing(peer, request); status != http.StatusOK {
			t.Fatalf("attempt %d rejected early", index+1)
		}
	}
	if _, status := p.beginPairing(peer, request); status != http.StatusTooManyRequests {
		t.Fatal("one peer could create a fourth pending attempt")
	}
	if _, status := p.beginPairing(netip.MustParseAddr("192.168.22.5"), request); status != http.StatusOK {
		t.Fatal("one peer's pending requests blocked another phone")
	}
}

func TestDeviceLivenessExpiresUsedTokensButAllowsInitialApprovalWindow(t *testing.T) {
	p := newPlayer("PC", "", nil, nil, nil)
	peer := netip.MustParseAddr("192.168.22.4")
	now := time.Now()
	p.now = func() time.Time { return now }
	token, err := pairRandom(16)
	if err != nil {
		t.Fatal(err)
	}
	p.activateDevice(testPlayerDeviceID, "Phone", peer, token)
	now = now.Add(45 * time.Second)
	if devices := p.deviceList(); len(devices) != 1 || devices[0].Active {
		t.Fatal("an unused grant looked connected beyond the idle window")
	}
	if _, device := p.authorize(peer, token, false); device == nil {
		t.Fatal("the initial unused grant expired before 90 seconds")
	}
	now = now.Add(29 * time.Second)
	if _, device := p.authorize(peer, token, false); device == nil {
		t.Fatal("authenticated liveness did not refresh the idle window")
	}
	now = now.Add(30 * time.Second)
	if _, device := p.authorize(peer, token, false); device != nil || len(p.deviceList()) != 0 {
		t.Fatal("idle temporary grant remained authorized or listed")
	}
	p.activateDevice(testPlayerDeviceID, "Phone", peer, token)
	now = now.Add(90 * time.Second)
	if _, device := p.authorize(peer, token, false); device != nil {
		t.Fatal("unused grant survived the approval window")
	}
}
