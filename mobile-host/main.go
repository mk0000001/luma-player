package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"encoding/json"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"time"
)

const protocolVersion = 1

type startupStatus struct {
	Status      string `json:"status"`
	Address     string `json:"address,omitempty"`
	PairingCode string `json:"pairingCode,omitempty"`
	Version     int    `json:"version,omitempty"`
	Message     string `json:"message,omitempty"`
}

func main() {
	os.Exit(run(os.Stdin, os.Stdout))
}

func run(stdin io.Reader, stdout io.Writer) int {
	return runWithListener(stdin, stdout, listenLAN)
}

func runWithListener(stdin io.Reader, stdout io.Writer, listen func(validatedConfig) (net.Listener, error)) int {
	return runWithServices(stdin, stdout, playerServices{listen: listen, discovery: startDiscovery})
}

func runWithServices(stdin io.Reader, stdout io.Writer, services playerServices) int {
	input := bufio.NewReaderSize(stdin, 32*1024)
	raw, err := readConfig(input)
	if err != nil || (raw.Mode != "" && raw.Mode != "player") {
		_ = json.NewEncoder(stdout).Encode(startupStatus{Status: "error", Message: "invalid configuration"})
		return 1
	}
	if raw.Mode == "player" {
		return runPlayer(input, stdout, raw, services)
	}
	return runOneShot(input, stdout, raw.config, services.listen)
}

func runOneShot(input *bufio.Reader, stdout io.Writer, raw config, listen func(validatedConfig) (net.Listener, error)) int {
	output := json.NewEncoder(stdout)
	fail := func(message string) int {
		_ = output.Encode(startupStatus{Status: "error", Message: message})
		return 1
	}

	cfg, err := raw.validate()
	if err != nil {
		return fail(err.Error())
	}
	certificate, pairingCode, token, err := createPairing(cfg.address.Addr())
	if err != nil {
		return fail("could not create a TLS session")
	}
	session, err := newSession(cfg, token)
	if err != nil {
		return fail("could not open the selected media")
	}
	defer session.close()

	listener, err := listen(cfg)
	if err != nil {
		return fail("could not listen on the selected LAN address")
	}
	defer listener.Close()

	ctx, cancel := context.WithCancel(context.Background())
	server := newHTTPServer(ctx, http.HandlerFunc(session.serveHTTP), certificate)
	finished := serveTLS(server, listener)
	defer func() {
		cancel()
		_ = server.Close()
		<-finished
	}()

	ready := startupStatus{
		Status: "ready", Address: listener.Addr().String(), PairingCode: pairingCode, Version: protocolVersion,
	}
	if err := output.Encode(ready); err != nil {
		return 1
	}
	parentDone := make(chan struct{})
	go func() {
		_, _ = io.Copy(io.Discard, input)
		close(parentDone)
	}()

	select {
	case <-parentDone:
		return 0
	case <-finished:
		return 1
	}
}

func newHTTPServer(ctx context.Context, handler http.Handler, certificate tls.Certificate) *http.Server {
	var protocols http.Protocols
	protocols.SetHTTP1(true)
	return &http.Server{
		Handler:                      handler,
		BaseContext:                  func(net.Listener) context.Context { return ctx },
		ErrorLog:                     log.New(io.Discard, "", 0),
		DisableGeneralOptionsHandler: true,
		ReadHeaderTimeout:            5 * time.Second,
		ReadTimeout:                  10 * time.Second,
		WriteTimeout:                 2*ffmpegStartupTimeout + 15*time.Second,
		IdleTimeout:                  15 * time.Second,
		MaxHeaderBytes:               8 * 1024,
		Protocols:                    &protocols,
		TLSConfig: &tls.Config{
			Certificates:           []tls.Certificate{certificate},
			MinVersion:             tls.VersionTLS12,
			SessionTicketsDisabled: true,
		},
	}
}

func serveTLS(server *http.Server, listener net.Listener) <-chan struct{} {
	finished := make(chan struct{})
	go func() {
		_ = server.ServeTLS(listener, "", "")
		close(finished)
	}()
	return finished
}
