package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net"
	"net/netip"
	"strings"
	"time"
)

const (
	playerPort      = 48762
	discoveryQuery  = "LUMA-DISCOVER-1 "
	discoveryScheme = "luma-discovery-1"
)

type discoveryReply struct {
	Protocol string `json:"protocol"`
	Nonce    string `json:"nonce"`
	Name     string `json:"name"`
	Port     uint16 `json:"port"`
}

type discoveryResponder struct {
	subnet netip.Prefix
	name   string
	rate   peerRate
}

func (d *discoveryResponder) reply(peer netip.Addr, data []byte, now time.Time) []byte {
	if !allowedPeer(peer, d.subnet) || len(data) != len(discoveryQuery)+32 {
		return nil
	}
	nonce, ok := strings.CutPrefix(string(data), discoveryQuery)
	if !ok {
		return nil
	}
	for _, digit := range nonce {
		if (digit < '0' || digit > '9') && (digit < 'a' || digit > 'f') {
			return nil
		}
	}
	if !d.rate.allow(peer, now) {
		return nil
	}
	var encoded bytes.Buffer
	encoder := json.NewEncoder(&encoded)
	encoder.SetEscapeHTML(false)
	_ = encoder.Encode(discoveryReply{Protocol: discoveryScheme, Nonce: nonce, Name: d.name, Port: playerPort})
	return encoded.Bytes()
}

type lanDiscovery struct {
	receiver *net.UDPConn
	sender   *net.UDPConn
	done     chan struct{}
}

func startDiscovery(address netip.AddrPort, subnet netip.Prefix, name string, unicastOnly bool) (io.Closer, error) {
	binding := &net.UDPAddr{Port: int(address.Port())}
	if unicastOnly {
		binding.IP = net.IP(address.Addr().AsSlice())
	}
	receiver, err := net.ListenUDP("udp4", binding)
	if err != nil {
		return nil, err
	}
	sender := receiver
	if !unicastOnly {
		// Broadcasts arrive on the wildcard listener. Bind replies to the selected IP.
		sender, err = net.ListenUDP("udp4", net.UDPAddrFromAddrPort(netip.AddrPortFrom(address.Addr(), 0)))
		if err != nil {
			_ = receiver.Close()
			return nil, err
		}
	}
	d := &lanDiscovery{receiver: receiver, sender: sender, done: make(chan struct{})}
	go d.serve(discoveryResponder{
		subnet: subnet, name: name, rate: peerRate{window: time.Second, peerLimit: 4, total: 64},
	})
	return d, nil
}

func (d *lanDiscovery) serve(responder discoveryResponder) {
	defer close(d.done)
	var buffer [65535]byte
	for {
		length, peer, err := d.receiver.ReadFromUDPAddrPort(buffer[:])
		if err != nil {
			return
		}
		if reply := responder.reply(peer.Addr().Unmap(), buffer[:length], time.Now()); reply != nil {
			_ = d.sender.SetWriteDeadline(time.Now().Add(200 * time.Millisecond))
			_, _ = d.sender.WriteToUDPAddrPort(reply, peer)
		}
	}
}

func (d *lanDiscovery) Close() error {
	_ = d.receiver.Close()
	if d.sender != d.receiver {
		_ = d.sender.Close()
	}
	<-d.done
	return nil
}
