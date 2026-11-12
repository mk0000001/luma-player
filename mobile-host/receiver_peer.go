package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"mime"
	"net"
	"net/http"
	"net/netip"
	"time"
	"unicode/utf8"
)

var errPeerResponse = errors.New("the remote PC returned an invalid response")

type receiverPeer struct {
	address   netip.AddrPort
	client    *http.Client
	transport *http.Transport
}

func newReceiverPeer(address netip.AddrPort, pin string, dial func(context.Context, string, string) (net.Conn, error)) *receiverPeer {
	if dial == nil {
		dial = (&net.Dialer{Timeout: 5 * time.Second, KeepAlive: 30 * time.Second}).DialContext
	}
	transport := &http.Transport{
		DialContext: func(ctx context.Context, _ string, requested string) (net.Conn, error) {
			if requested != address.String() {
				return nil, errPeerResponse
			}
			return dial(ctx, "tcp4", requested)
		},
		DisableCompression:     true,
		TLSHandshakeTimeout:    5 * time.Second,
		ResponseHeaderTimeout:  2*ffmpegStartupTimeout + 10*time.Second,
		MaxResponseHeaderBytes: 8 * 1024,
		MaxConnsPerHost:        6,
		IdleConnTimeout:        15 * time.Second,
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true, // VerifyConnection checks the self-signed identity and its pin.
			MinVersion:         tls.VersionTLS12,
			VerifyConnection: func(state tls.ConnectionState) error {
				if len(state.PeerCertificates) != 1 {
					return errPeerResponse
				}
				cert := state.PeerCertificates[0]
				key, ok := cert.PublicKey.(*ecdsa.PublicKey)
				now := time.Now()
				if !ok || key.Curve != elliptic.P256() ||
					now.Before(cert.NotBefore) || !now.Before(cert.NotAfter) ||
					cert.VerifyHostname(address.Addr().String()) != nil ||
					cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature) != nil {
					return errPeerResponse
				}
				if pin != "" && subtle.ConstantTimeCompare([]byte(peerPin(state)), []byte(pin)) != 1 {
					return errPeerResponse
				}
				return nil
			},
		},
	}
	return &receiverPeer{
		address: address, transport: transport,
		client: &http.Client{
			Transport: transport, CheckRedirect: func(*http.Request, []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
	}
}

func peerPin(state tls.ConnectionState) string {
	hash := sha256.Sum256(state.PeerCertificates[0].RawSubjectPublicKeyInfo)
	return base64.RawURLEncoding.EncodeToString(hash[:])
}

func (p *receiverPeer) request(ctx context.Context, method, path, token, span string, body io.Reader) (*http.Response, error) {
	request, err := http.NewRequestWithContext(ctx, method, "https://"+p.address.String()+path, body)
	if err != nil {
		return nil, errPeerResponse
	}
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	if span != "" {
		request.Header.Set("Range", span)
	}
	if body != nil {
		request.Header.Set("Content-Type", "application/json")
	}
	response, err := p.client.Do(request)
	if err != nil {
		return nil, errPeerResponse
	}
	return response, nil
}

type peerJSONResponse struct {
	status   int
	pin      string
	revision string
}

func (p *receiverPeer) json(ctx context.Context, method, path, token string, input, output any, limit int64) (peerJSONResponse, error) {
	ctx, cancel := context.WithTimeout(ctx, 6*time.Second)
	defer cancel()
	var body io.Reader
	if input != nil {
		data, err := json.Marshal(input)
		if err != nil {
			return peerJSONResponse{}, errPeerResponse
		}
		body = bytes.NewReader(data)
	}
	response, err := p.request(ctx, method, path, token, "", body)
	if err != nil {
		return peerJSONResponse{}, err
	}
	defer response.Body.Close()
	result := peerJSONResponse{status: response.StatusCode}
	if response.StatusCode != http.StatusOK {
		return result, nil
	}
	contentType, _, err := mime.ParseMediaType(response.Header.Get("Content-Type"))
	if err != nil || contentType != "application/json" || response.Header.Get("Content-Encoding") != "" ||
		response.TLS == nil || len(response.TLS.PeerCertificates) != 1 {
		return result, errPeerResponse
	}
	data, err := io.ReadAll(io.LimitReader(response.Body, limit+1))
	if err != nil || int64(len(data)) > limit || !utf8.Valid(data) || json.Unmarshal(data, output) != nil {
		return result, errPeerResponse
	}
	result.pin = peerPin(*response.TLS)
	result.revision = response.Header.Get("ETag")
	return result, nil
}

func (p *receiverPeer) identity(ctx context.Context) (string, string, error) {
	var identity struct{ Name string }
	result, err := p.json(ctx, http.MethodGet, pairIdentityPath, "", nil, &identity, maxPairBody)
	if err != nil || result.status != http.StatusOK || !validDeviceName(identity.Name) {
		return "", "", errPeerResponse
	}
	name := sanitizeReceiverText(identity.Name)
	if name == "" {
		return "", "", errPeerResponse
	}
	return result.pin, name, nil
}
