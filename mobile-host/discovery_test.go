package main

import (
	"bytes"
	"encoding/json"
	"net"
	"net/netip"
	"testing"
	"time"
)

func TestDiscoveryRepliesToPrivateSubnetWithOnlyIdentity(t *testing.T) {
	const nonce = "0123456789abcdef0123456789abcdef"
	request := []byte("LUMA-DISCOVER-1 " + nonce)
	responder := discoveryResponder{
		subnet: netip.MustParsePrefix("192.168.8.0/24"), name: "테스트 PC",
		rate: peerRate{window: time.Second, peerLimit: 4, total: 64},
	}
	now := time.Now()
	peer := netip.MustParseAddr("192.168.8.7")
	for _, ip := range []string{"192.168.9.7", "127.0.0.1", "203.0.113.4"} {
		if reply := responder.reply(netip.MustParseAddr(ip), request, now); reply != nil {
			t.Fatalf("discovery answered an outside peer %s", ip)
		}
	}
	for _, malformed := range []string{
		"LUMA-DISCOVER-1 " + nonce + "\n",
		"LUMA-DISCOVER-1 0123456789ABCDEF0123456789ABCDEF",
		"LUMA-DISCOVER-2 " + nonce,
	} {
		if reply := responder.reply(peer, []byte(malformed), now); reply != nil {
			t.Fatal("discovery answered a malformed query")
		}
	}
	for index := 0; index < 4; index++ {
		var reply map[string]any
		data := responder.reply(peer, request, now)
		if len(data) > 384 || json.Unmarshal(data, &reply) != nil || len(reply) != 4 ||
			reply["protocol"] != "luma-discovery-1" || reply["nonce"] != nonce ||
			reply["name"] != "테스트 PC" || reply["port"] != float64(48762) {
			t.Fatalf("incorrect discovery reply: %s", data)
		}
	}
	if reply := responder.reply(peer, request, now); reply != nil {
		t.Fatal("discovery exceeded the per-peer limit")
	}
	if reply := responder.reply(peer, request, now.Add(time.Second)); reply == nil {
		t.Fatal("discovery never reopened the rate window")
	}
}

func TestDiscoveryReceiverSurvivesLargeDatagram(t *testing.T) {
	receiver, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	sender, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		receiver.Close()
		t.Fatal(err)
	}
	d := &lanDiscovery{receiver: receiver, sender: sender, done: make(chan struct{})}
	go d.serve(discoveryResponder{})
	defer d.Close()
	if _, err := sender.WriteToUDP(bytes.Repeat([]byte{'x'}, 4000), receiver.LocalAddr().(*net.UDPAddr)); err != nil {
		t.Fatal(err)
	}
	select {
	case <-d.done:
		t.Fatal("a large datagram stopped discovery")
	case <-time.After(100 * time.Millisecond):
	}
}
