package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
)

const (
	maxRememberedDevices = 16
	maxCredentialBytes   = 64 * 1024
)

var errCredentialStore = errors.New("could not access the remembered devices")

type rememberedDevice struct {
	Name string `json:"name"`
	Hash string `json:"hash"`
}

type storedCredentials struct {
	Version int                         `json:"version"`
	Key     []byte                      `json:"privateKey"`
	Devices map[string]rememberedDevice `json:"devices"`
}

type credentialFile struct {
	path    string
	unlock  func()
	key     *ecdsa.PrivateKey
	devices map[string]rememberedDevice
}

func openCredentialFile(path string) (*credentialFile, error) {
	if !filepath.IsAbs(path) || !localVolume(path) {
		return nil, errCredentialStore
	}
	path = filepath.Clean(path)
	parent := filepath.Dir(path)
	if parent == path || os.MkdirAll(parent, 0700) != nil {
		return nil, errCredentialStore
	}
	parent, err := filepath.EvalSymlinks(parent)
	if err != nil || !localVolume(parent) {
		return nil, errCredentialStore
	}
	path = filepath.Join(parent, filepath.Base(path))
	unlock, err := lockCredentialFile(path + ".lock")
	if err != nil {
		return nil, errCredentialStore
	}

	store := &credentialFile{path: path, unlock: unlock}
	if err := store.load(); err != nil {
		store.unlock()
		return nil, errCredentialStore
	}
	return store, nil
}

func (store *credentialFile) load() error {
	info, err := os.Lstat(store.path)
	if errors.Is(err, os.ErrNotExist) {
		key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
		if err != nil {
			return errCredentialStore
		}
		store.key = key
		store.devices = make(map[string]rememberedDevice)
		return store.save(store.devices)
	}
	if err != nil || !info.Mode().IsRegular() || reparsePoint(info) || info.Size() > maxCredentialBytes {
		return errCredentialStore
	}
	file, err := os.Open(store.path)
	if err != nil {
		return errCredentialStore
	}
	defer file.Close()
	ciphertext, err := io.ReadAll(io.LimitReader(file, maxCredentialBytes+1))
	if err != nil || len(ciphertext) == 0 || len(ciphertext) > maxCredentialBytes {
		return errCredentialStore
	}
	plaintext, err := unprotectCredentials(ciphertext)
	if err != nil {
		return errCredentialStore
	}
	defer clear(plaintext)
	var saved storedCredentials
	if json.Unmarshal(plaintext, &saved) != nil || saved.Version != 1 ||
		len(saved.Devices) > maxRememberedDevices {
		return errCredentialStore
	}
	defer clear(saved.Key)
	key, err := x509.ParseECPrivateKey(saved.Key)
	if err != nil || key.Curve != elliptic.P256() {
		return errCredentialStore
	}
	for id, device := range saved.Devices {
		if !validPairValue(id, 16) || !validDeviceName(device.Name) || !validPairValue(device.Hash, 32) {
			return errCredentialStore
		}
	}
	store.key = key
	store.devices = saved.Devices
	if store.devices == nil {
		store.devices = make(map[string]rememberedDevice)
	}
	return nil
}

func (store *credentialFile) save(devices map[string]rememberedDevice) error {
	key, err := x509.MarshalECPrivateKey(store.key)
	if err != nil {
		return errCredentialStore
	}
	defer clear(key)
	data, err := json.Marshal(storedCredentials{Version: 1, Key: key, Devices: devices})
	if err != nil {
		return errCredentialStore
	}
	defer clear(data)
	encrypted, err := protectCredentials(data)
	if err != nil || len(encrypted) == 0 || len(encrypted) > maxCredentialBytes {
		return errCredentialStore
	}
	temporary, err := os.CreateTemp(filepath.Dir(store.path), ".luma-pairing-*")
	if err != nil {
		return errCredentialStore
	}
	defer os.Remove(temporary.Name())
	_, writeErr := temporary.Write(encrypted)
	syncErr := temporary.Sync()
	closeErr := temporary.Close()
	if writeErr != nil || syncErr != nil || closeErr != nil || replaceCredentialFile(temporary.Name(), store.path) != nil {
		return errCredentialStore
	}
	return nil
}
