package main

import (
	"bufio"
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"net/netip"
	"sync"
	"time"
	"unicode/utf8"
)

type playerServices struct {
	listen       func(validatedConfig) (net.Listener, error)
	discovery    func(netip.AddrPort, netip.Prefix, string, bool) (io.Closer, error)
	receiverDial func(context.Context, string, string) (net.Conn, error)
}

type playerStatus struct {
	Status   string        `json:"status"`
	Command  string        `json:"command,omitempty"`
	Address  string        `json:"address,omitempty"`
	Version  int           `json:"version,omitempty"`
	ID       string        `json:"id,omitempty"`
	Code     string        `json:"code,omitempty"`
	Device   string        `json:"device,omitempty"`
	DeviceID string        `json:"deviceId,omitempty"`
	Remember *bool         `json:"remember,omitempty"`
	Title    string        `json:"title,omitempty"`
	Message  string        `json:"message,omitempty"`
	Devices  *[]deviceInfo `json:"devices,omitempty"`
}

type playerCommand struct {
	Command  string         `json:"command"`
	Media    *config        `json:"media"`
	ID       string         `json:"id"`
	Decision string         `json:"decision"`
	Options  *deviceOptions `json:"options"`
}

type statusWriter struct {
	mu     sync.Mutex
	output *json.Encoder
}

func (w *statusWriter) write(status playerStatus) error {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.output.Encode(status)
}

type player struct {
	name     string
	pin      string
	now      func() time.Time
	announce func(playerStatus)
	persist  func(map[string]rememberedDevice) error

	mu              sync.Mutex
	attempts        map[string]*pairAttempt
	remembered      map[string]rememberedDevice
	active          map[string]*activeDevice
	media           *sharedMedia
	startRate       peerRate
	loginRate       peerRate
	optionsRevision int64
}

func newPlayer(name, pin string, remembered map[string]rememberedDevice,
	persist func(map[string]rememberedDevice) error, announce func(playerStatus)) *player {
	return &player{
		name: name, pin: pin, now: time.Now, announce: announce, persist: persist, remembered: remembered,
		attempts: make(map[string]*pairAttempt), active: make(map[string]*activeDevice),
		startRate: peerRate{window: 30 * time.Second, peerLimit: 4, total: 32},
		loginRate: peerRate{window: 15 * time.Second, peerLimit: 8, total: 64},
	}
}

func runPlayer(input *bufio.Reader, stdout io.Writer, raw initialConfig, services playerServices) int {
	output := &statusWriter{output: json.NewEncoder(stdout)}
	fail := func(message string) int {
		_ = output.write(playerStatus{Status: "error", Message: message})
		return 1
	}
	address, subnet, err := validateLAN(raw.Listen, raw.Subnet, raw.PeerSubnet)
	if err != nil || address.Port() != playerPort {
		return fail(errInvalidLAN.Error())
	}
	if !validDeviceName(raw.Name) {
		return fail(errInvalidConfig.Error())
	}
	store, err := openCredentialFile(raw.Store)
	if err != nil {
		return fail(errCredentialStore.Error())
	}
	defer store.unlock()
	cert, err := createCertificate(store.key, address.Addr(), 30*24*time.Hour)
	if err != nil {
		return fail("could not create a TLS session")
	}
	spki, err := x509.MarshalPKIXPublicKey(&store.key.PublicKey)
	if err != nil {
		return fail("could not create a TLS session")
	}
	hash := sha256.Sum256(spki)
	p := newPlayer(raw.Name, base64.RawURLEncoding.EncodeToString(hash[:]), store.devices,
		store.save, func(status playerStatus) { _ = output.write(status) })

	listener, err := services.listen(validatedConfig{config: raw.config, address: address, subnet: subnet})
	if err != nil {
		return fail("could not listen on the selected LAN address")
	}
	defer listener.Close()
	discovery, discoveryErr := services.discovery(address, subnet, raw.Name, raw.PeerSubnet != "")
	if discoveryErr == nil {
		defer discovery.Close()
	}

	ctx, cancel := context.WithCancel(context.Background())
	server := newHTTPServer(ctx, http.HandlerFunc(p.serveHTTP), cert)
	finished := serveTLS(server, listener)
	defer func() {
		cancel()
		_ = server.Close()
		p.stopMedia()
		<-finished
	}()
	if err := output.write(playerStatus{
		Status: "ready", Address: listener.Addr().String(), Version: protocolVersion,
	}); err != nil {
		return 1
	}
	if discoveryErr != nil {
		p.announce(playerStatus{Status: "error", Command: "discovery", Message: "could not start LAN discovery"})
	}

	lines := make(chan commandLine)
	go readPlayerCommands(ctx, input, lines)
	for {
		select {
		case <-finished:
			return 1
		case line := <-lines:
			if line.err == io.EOF {
				return 0
			}
			if line.err != nil {
				return fail("invalid player command")
			}
			var command playerCommand
			if !utf8.Valid(line.data) || json.Unmarshal(line.data, &command) != nil {
				p.announce(playerStatus{Status: "error", Message: "invalid player command"})
				continue
			}
			p.handleCommand(raw.config, command)
		}
	}
}

type commandLine struct {
	data []byte
	err  error
}

func readPlayerCommands(ctx context.Context, input *bufio.Reader, lines chan<- commandLine) {
	for {
		data, err := readBoundedLine(input)
		select {
		case lines <- commandLine{data: data, err: err}:
		case <-ctx.Done():
			return
		}
		if err != nil {
			return
		}
	}
}

func (p *player) handleCommand(binding config, command playerCommand) {
	var err error
	switch command.Command {
	case "share":
		err = p.shareMedia(binding, command.Media)
		if err == nil {
			p.announce(playerStatus{Status: "sharing", Title: command.Media.Title})
		}
	case "stop":
		err = p.revokeAndStop("stop")
	case "decide":
		var event playerStatus
		event, err = p.decidePairing(command.ID, command.Decision)
		if err == nil && event.Status != "" {
			p.announce(event)
		}
	case "forget":
		err = p.revokeAndStop("forget")
	case "listDevices", "disconnectDevice", "forgetDevice", "setDeviceOptions":
		if command.Command == "setDeviceOptions" {
			err = p.setDeviceOptions(command.ID, command.Options)
		} else if command.Command != "listDevices" {
			err = p.disconnectDevice(command.ID, command.Command == "forgetDevice")
		}
		if err == nil {
			devices := p.deviceList()
			p.announce(playerStatus{Status: "devices", Devices: &devices})
		}
	default:
		p.announce(playerStatus{Status: "error", Message: "invalid player command"})
	}
	if err != nil {
		p.announce(playerStatus{Status: "error", Command: command.Command, Message: err.Error()})
	}
}

type sharedMedia struct {
	session  *mediaSession
	revision string
	ctx      context.Context
	cancel   context.CancelFunc
}

func (p *player) shareMedia(binding config, media *config) error {
	p.stopMedia()
	if media == nil {
		return errInvalidSource
	}
	request := *media
	request.Listen, request.Subnet, request.PeerSubnet = binding.Listen, binding.Subnet, binding.PeerSubnet
	cfg, err := request.validate()
	if err != nil {
		return err
	}
	revision, err := pairRandom(16)
	if err != nil {
		return errors.New("could not open the selected media")
	}
	session, err := newSession(cfg, "")
	if err != nil {
		return errors.New("could not open the selected media")
	}
	session.configureVariants()
	ctx, cancel := context.WithCancel(context.Background())
	p.mu.Lock()
	p.media = &sharedMedia{session: session, revision: revision, ctx: ctx, cancel: cancel}
	p.mu.Unlock()
	return nil
}

func (p *player) stopMedia() {
	p.mu.Lock()
	old := p.media
	p.media = nil
	p.mu.Unlock()
	if old != nil {
		old.cancel()
		old.session.close()
	}
}

func (p *player) revokeAndStop(command string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if command == "forget" {
		empty := make(map[string]rememberedDevice)
		if err := p.persist(empty); err != nil {
			return errCredentialStore
		}
		p.remembered = empty
	}
	for _, device := range p.active {
		device.cancel()
	}
	clear(p.active)
	clear(p.attempts)
	if p.media != nil {
		p.media.cancel()
		p.media.session.close()
		p.media = nil
	}
	p.announce(playerStatus{Status: "stopped", Command: command})
	return nil
}

func (p *player) authorize(peer netip.Addr, token string, trackRequest bool) (*sharedMedia, *activeDevice) {
	if !validPairValue(token, 16) {
		return nil, nil
	}
	hash := sha256.Sum256([]byte(token))
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	p.expireDevices(now)
	var authorized *activeDevice
	for _, device := range p.active {
		if device.peer == peer && subtle.ConstantTimeCompare(device.hash[:], hash[:]) == 1 {
			authorized = device
		}
	}
	if authorized == nil {
		return nil, nil
	}
	authorized.used = true
	authorized.seen.Store(now.UnixNano())
	if trackRequest {
		authorized.requests.Add(1)
	}
	return p.media, authorized
}

func (shared *sharedMedia) serve(w *responseWriter, r *http.Request, device *activeDevice, route playbackRoute) {
	ctx, cancel := context.WithCancel(r.Context())
	stopShare := context.AfterFunc(shared.ctx, cancel)
	stopDevice := context.AfterFunc(device.ctx, cancel)
	stopped := make(chan struct{})
	interrupt := context.AfterFunc(ctx, func() {
		_ = w.controller.SetWriteDeadline(time.Now())
		close(stopped)
	})
	defer func() {
		stopShare()
		stopDevice()
		if !interrupt() {
			<-stopped
		}
		cancel()
	}()
	if shared.ctx.Err() != nil || device.ctx.Err() != nil {
		cancel()
	}
	w.ctx = ctx
	w.onWrite = device.noteWrite
	r = r.WithContext(ctx)
	switch {
	case route.profile != "":
		shared.session.serveVariant(w, r, route.profile, route.position)
	case route.shiftedSubtitle != "":
		shared.session.serveShiftedSubtitle(w, r, route.shiftedSubtitle, route.position)
	default:
		shared.session.servePlayback(w, r, device.options.Load())
	}
}
