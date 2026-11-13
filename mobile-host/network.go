package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"math/big"
	"net"
	"net/netip"
	"sync"
	"time"
)

const (
	maxConnections        = 8
	maxConnectionsPerPeer = 6
)

func allowedPeer(ip netip.Addr, subnet netip.Prefix) bool {
	return ip.Is4() && ip.IsPrivate() && subnet.Contains(ip)
}

func listenLAN(cfg validatedConfig) (net.Listener, error) {
	socket, err := net.Listen("tcp4", cfg.address.String())
	if err != nil {
		return nil, err
	}
	return &lanListener{
		Listener: socket, subnet: cfg.subnet,
		slots: make(chan struct{}, maxConnections),
	}, nil
}

type lanListener struct {
	net.Listener
	subnet   netip.Prefix
	loopback bool
	slots    chan struct{}
	mu       sync.Mutex
	peers    map[netip.Addr]int
}

func (l *lanListener) acquire(ip netip.Addr) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	if !l.loopback && l.peers[ip] >= maxConnectionsPerPeer {
		return false
	}
	select {
	case l.slots <- struct{}{}:
		if !l.loopback {
			if l.peers == nil {
				l.peers = make(map[netip.Addr]int)
			}
			l.peers[ip]++
		}
		return true
	default:
		return false
	}
}

func (l *lanListener) release(ip netip.Addr) {
	l.mu.Lock()
	defer l.mu.Unlock()
	<-l.slots
	if !l.loopback {
		if l.peers[ip] == 1 {
			delete(l.peers, ip)
		} else {
			l.peers[ip]--
		}
	}
}

func (l *lanListener) Accept() (net.Conn, error) {
	for {
		conn, err := l.Listener.Accept()
		if err != nil {
			return nil, err
		}
		peer, ok := conn.RemoteAddr().(*net.TCPAddr)
		var ip netip.Addr
		if ok {
			ip = peer.AddrPort().Addr().Unmap()
		}
		allowed := allowedPeer(ip, l.subnet)
		if l.loopback {
			allowed = ip == netip.AddrFrom4([4]byte{127, 0, 0, 1})
		}
		if allowed && l.acquire(ip) {
			return &lanConnection{Conn: conn, release: func() { l.release(ip) }}, nil
		}
		_ = conn.Close()
	}
}

type lanConnection struct {
	net.Conn
	release func()
	once    sync.Once
}

func (c *lanConnection) Close() error {
	err := c.Conn.Close()
	c.once.Do(c.release)
	return err
}

func createPairing(ip netip.Addr) (tls.Certificate, string, string, error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, "", "", err
	}
	certificate, err := createCertificate(key, ip, 48*time.Hour)
	if err != nil {
		return tls.Certificate{}, "", "", err
	}
	fingerprint := sha256.Sum256(certificate.Certificate[0])
	var pairing [48]byte
	copy(pairing[:32], fingerprint[:])
	if _, err := rand.Read(pairing[32:]); err != nil {
		return tls.Certificate{}, "", "", err
	}
	return certificate, base64.RawURLEncoding.EncodeToString(pairing[:]), base64.RawURLEncoding.EncodeToString(pairing[32:]), nil
}

func createCertificate(key *ecdsa.PrivateKey, ip netip.Addr, lifetime time.Duration) (tls.Certificate, error) {
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return tls.Certificate{}, err
	}
	now := time.Now()
	certificate := &x509.Certificate{
		SerialNumber: serial.Add(serial, big.NewInt(1)),
		NotBefore:    now.Add(-5 * time.Minute),
		NotAfter:     now.Add(lifetime),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IPAddresses:  []net.IP{net.IP(ip.AsSlice())},
	}
	der, err := x509.CreateCertificate(rand.Reader, certificate, certificate, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	return tls.Certificate{Certificate: [][]byte{der}, PrivateKey: key}, nil
}
