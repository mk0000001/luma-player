package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"io"
	"net/netip"
	"time"
)

func writeTranslationIdentity(output io.Writer) error {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return err
	}
	cert, err := createCertificate(key, netip.MustParseAddr("127.0.0.1"), 24*time.Hour)
	if err != nil {
		return err
	}
	privateKey, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return err
	}
	return json.NewEncoder(output).Encode(map[string]string{
		"certificate": string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: cert.Certificate[0]})),
		"privateKey":  string(pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: privateKey})),
	})
}
