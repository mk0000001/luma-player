package main

import (
	"context"
	"crypto/sha256"
	"errors"
	"maps"
	"net/netip"
	"sort"
	"sync"
	"sync/atomic"
	"time"
)

const deviceIdleTime = 30 * time.Second

type deviceInfo struct {
	ID         string         `json:"id"`
	Name       string         `json:"name"`
	Address    string         `json:"address"`
	Active     bool           `json:"active"`
	Remembered bool           `json:"remembered"`
	Options    *deviceOptions `json:"options,omitempty"`
}

type deviceOptions struct {
	Revision        int64  `json:"revision"`
	Profile         string `json:"profile,omitempty"`
	HDR             string `json:"hdr,omitempty"`
	SkipSeconds     *int   `json:"skipSeconds,omitempty"`
	SubtitleDelayMS *int   `json:"subtitleDelayMs,omitempty"`
	Fill            *bool  `json:"fill,omitempty"`
}

type activeDevice struct {
	name     string
	peer     netip.Addr
	hash     [sha256.Size]byte
	seen     atomic.Int64
	used     bool
	ctx      context.Context
	cancel   context.CancelFunc
	requests sync.WaitGroup
	options  atomic.Pointer[deviceOptions]
}

// The caller holds p.mu so an old token cannot start another request after removal.
func (p *player) activateDevice(id, name string, peer netip.Addr, token string) {
	previous := p.active[id]
	if previous != nil {
		previous.cancel()
		previous.requests.Wait()
	}
	p.clearDeviceApprovals(id)
	ctx, cancel := context.WithCancel(context.Background())
	device := &activeDevice{
		name: name, peer: peer, hash: sha256.Sum256([]byte(token)), ctx: ctx, cancel: cancel,
	}
	device.seen.Store(p.now().UnixNano())
	if previous != nil {
		device.options.Store(previous.options.Load())
	}
	p.active[id] = device
}

func (d *activeDevice) noteWrite() {
	now := time.Now().UnixNano()
	seen := d.seen.Load()
	if now-seen >= int64(5*time.Second) {
		d.seen.CompareAndSwap(seen, now)
	}
}

func (p *player) deviceList() []deviceInfo {
	p.mu.Lock()
	defer p.mu.Unlock()
	devices := make([]deviceInfo, 0, len(p.active)+len(p.remembered))
	now := p.now()
	p.expireDevices(now)
	for id, device := range p.active {
		_, remembered := p.remembered[id]
		devices = append(devices, deviceInfo{
			ID: id, Name: device.name, Address: device.peer.String(), Remembered: remembered,
			Active:  now.Sub(time.Unix(0, device.seen.Load())) < deviceIdleTime,
			Options: device.options.Load(),
		})
	}
	for id, device := range p.remembered {
		if _, active := p.active[id]; !active {
			devices = append(devices, deviceInfo{ID: id, Name: device.Name, Address: device.Address, Remembered: true})
		}
	}
	sort.Slice(devices, func(i, j int) bool {
		if devices[i].Name != devices[j].Name {
			return devices[i].Name < devices[j].Name
		}
		return devices[i].ID < devices[j].ID
	})
	return devices
}

func (p *player) setDeviceOptions(id string, options *deviceOptions) error {
	if !validPairValue(id, 16) || options == nil || !validDeviceOptions(*options) {
		return errors.New("invalid device playback options")
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.expireDevices(p.now())
	device := p.active[id]
	if device == nil {
		return errors.New("device is no longer connected")
	}
	p.optionsRevision++
	next := *options
	next.Revision = p.optionsRevision
	device.options.Store(&next)
	return nil
}

func validDeviceOptions(options deviceOptions) bool {
	switch options.Profile {
	case "", "auto", "original", "1080p", "720p", "480p":
	default:
		return false
	}
	switch options.HDR {
	case "", "auto", "original", "sdr":
	default:
		return false
	}
	if options.SkipSeconds != nil {
		switch *options.SkipSeconds {
		case 5, 10, 15, 30:
		default:
			return false
		}
	}
	return options.SubtitleDelayMS == nil || (*options.SubtitleDelayMS >= -5000 && *options.SubtitleDelayMS <= 5000)
}

// The caller holds p.mu. Expiring an old token also removes any approval result
// that could hand that token out again.
func (p *player) expireDevices(now time.Time) {
	for id, device := range p.active {
		limit := pairingTTL
		if device.used {
			limit = deviceIdleTime
		}
		if now.Sub(time.Unix(0, device.seen.Load())) < limit {
			continue
		}
		delete(p.active, id)
		device.cancel()
		p.clearDeviceApprovals(id)
	}
}

func (p *player) disconnectDevice(id string, forget bool) error {
	if !validPairValue(id, 16) {
		return errors.New("invalid device")
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if _, exists := p.remembered[id]; forget && exists {
		next := maps.Clone(p.remembered)
		delete(next, id)
		if err := p.persist(next); err != nil {
			return errCredentialStore
		}
		p.remembered = next
	}
	if device := p.active[id]; device != nil {
		delete(p.active, id)
		device.cancel()
		device.requests.Wait()
	}
	for attemptID, attempt := range p.attempts {
		if attempt.DeviceID == id {
			delete(p.attempts, attemptID)
		}
	}
	return nil
}
