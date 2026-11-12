package main

import (
	"encoding/json"
	"errors"
	"net/netip"
	"os"
)

type outboundPeer struct {
	Pin      string `json:"pin"`
	DeviceID string `json:"deviceId"`
	Token    string `json:"token"`
}

type outboundCredentials struct {
	Version int                     `json:"version"`
	Peers   map[string]outboundPeer `json:"peers"`
}

func receiverEndpoint(value string) (netip.AddrPort, error) {
	endpoint, err := netip.ParseAddrPort(value)
	if err != nil || !endpoint.Addr().Is4() || !endpoint.Addr().IsPrivate() || endpoint.Port() != playerPort {
		return netip.AddrPort{}, errInvalidLAN
	}
	return endpoint, nil
}

func readOutboundCredentials(path string) (outboundCredentials, error) {
	data, err := readProtectedFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return outboundCredentials{Version: 1, Peers: make(map[string]outboundPeer)}, nil
	}
	if err != nil {
		return outboundCredentials{}, errCredentialStore
	}
	defer clear(data)
	var store outboundCredentials
	if json.Unmarshal(data, &store) != nil || store.Version != 1 || store.Peers == nil ||
		len(store.Peers) > maxRememberedDevices {
		return outboundCredentials{}, errCredentialStore
	}
	for address, peer := range store.Peers {
		if _, err := receiverEndpoint(address); err != nil || !validPairValue(peer.Pin, 32) ||
			!validPairValue(peer.DeviceID, 16) || !validPairValue(peer.Token, 32) {
			return outboundCredentials{}, errCredentialStore
		}
	}
	return store, nil
}

func rememberedPC(path, endpoint string) (outboundPeer, bool, error) {
	path, unlock, err := acquireCredentialPath(path)
	if err != nil {
		return outboundPeer{}, false, err
	}
	defer unlock()
	store, err := readOutboundCredentials(path)
	peer, exists := store.Peers[endpoint]
	return peer, exists, err
}

func rememberPC(path, endpoint string, peer outboundPeer) error {
	path, unlock, err := acquireCredentialPath(path)
	if err != nil {
		return err
	}
	defer unlock()
	store, err := readOutboundCredentials(path)
	if err != nil {
		return err
	}
	if _, exists := store.Peers[endpoint]; !exists && len(store.Peers) >= maxRememberedDevices {
		return errCredentialStore
	}
	store.Peers[endpoint] = peer
	data, err := json.Marshal(store)
	if err != nil {
		return errCredentialStore
	}
	defer clear(data)
	return writeProtectedFile(path, data)
}
