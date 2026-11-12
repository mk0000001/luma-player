package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestWindowsCredentialFileEncryptsAndLocksRememberedHashes(t *testing.T) {
	path := filepath.Join(t.TempDir(), "subdir", "credentials.dpapi")
	store, err := openCredentialFile(filepath.ToSlash(path))
	if err != nil {
		t.Fatal(err)
	}
	if other, err := openCredentialFile(path); err == nil {
		other.unlock()
		t.Fatal("a concurrent writer could open the same credentials")
	}
	const token = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefg0123456789"
	wantHash := digestPairValue(token)
	record := map[string]rememberedDevice{
		testPlayerDeviceID: {Name: "Phone", Hash: wantHash},
	}
	if err := store.save(record); err != nil {
		store.unlock()
		t.Fatal(err)
	}
	store.unlock()
	encrypted, err := os.ReadFile(path)
	if err != nil || json.Valid(encrypted) || bytes.Contains(encrypted, []byte(wantHash)) {
		t.Fatal("credential file was not encrypted")
	}
	decrypted, err := unprotectCredentials(encrypted)
	if err != nil {
		t.Fatal(err)
	}
	defer clear(decrypted)
	if bytes.Contains(decrypted, []byte(token)) || !bytes.Contains(decrypted, []byte(wantHash)) {
		t.Fatal("the store retained the bearer secret instead of its digest")
	}
	loaded, err := openCredentialFile(path)
	if err != nil {
		t.Fatal(err)
	}
	defer loaded.unlock()
	if loaded.devices[testPlayerDeviceID] != record[testPlayerDeviceID] || !loaded.key.Equal(store.key) {
		t.Fatal("identity or remembered digest changed across an atomic save")
	}
}
