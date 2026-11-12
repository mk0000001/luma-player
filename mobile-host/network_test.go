package main

import (
	"errors"
	"net"
	"net/netip"
	"testing"
)

type testPeer struct {
	net.Conn
	address net.Addr
	closed  bool
}

func (peer *testPeer) RemoteAddr() net.Addr { return peer.address }

func (peer *testPeer) Close() error {
	peer.closed = true
	return nil
}

type peerQueue struct {
	net.Listener
	peers []*testPeer
}

func (queue *peerQueue) add(ip string) *testPeer {
	peer := &testPeer{address: &net.TCPAddr{IP: net.ParseIP(ip)}}
	queue.peers = append(queue.peers, peer)
	return peer
}

func (queue *peerQueue) Accept() (net.Conn, error) {
	if len(queue.peers) == 0 {
		return nil, net.ErrClosed
	}
	peer := queue.peers[0]
	queue.peers = queue.peers[1:]
	return peer, nil
}

func TestLANListenerRejectsOutsidePeersAndCapsSockets(t *testing.T) {
	queue := &peerQueue{}
	listener := &lanListener{
		Listener: queue,
		subnet:   netip.MustParsePrefix("192.168.4.0/24"),
		slots:    make(chan struct{}, maxConnections),
	}
	public := queue.add("203.0.113.4")
	otherLAN := queue.add("192.168.5.9")
	var open []net.Conn
	for index := 0; index < 8; index++ {
		queue.add("192.168.4.9")
		conn, err := listener.Accept()
		if err != nil {
			t.Fatalf("allowed peer %d was rejected: %v", index+1, err)
		}
		open = append(open, conn)
	}
	if !public.closed || !otherLAN.closed {
		t.Fatal("listener kept a disallowed peer open")
	}
	overflow := queue.add("192.168.4.9")
	if conn, err := listener.Accept(); conn != nil || !errors.Is(err, net.ErrClosed) || !overflow.closed {
		t.Fatal("listener accepted a ninth concurrent socket")
	}
	_ = open[0].Close()
	_ = open[0].Close()
	queue.add("192.168.4.9")
	conn, err := listener.Accept()
	if err != nil {
		t.Fatal("listener did not release a closed socket slot")
	}
	_ = conn.Close()
	for _, conn := range open[1:] {
		_ = conn.Close()
	}
}
