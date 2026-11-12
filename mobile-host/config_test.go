package main

import (
	"bufio"
	"bytes"
	"errors"
	"net/netip"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestDiscRejectsLinkedCertificateDirectory(t *testing.T) {
	cfg, _ := discConfig(t)
	link := filepath.Join(cfg.Source, "CERTIFICATE")
	target := t.TempDir()
	if err := os.Symlink(target, link); err != nil {
		if runtime.GOOS != "windows" {
			t.Fatal(err)
		}
		command := exec.Command("powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
			"New-Item -ItemType Junction -Path $env:LUMA_TEST_CERTIFICATE_LINK -Target $env:LUMA_TEST_CERTIFICATE_TARGET -ErrorAction Stop | Out-Null")
		command.Env = append(os.Environ(), "LUMA_TEST_CERTIFICATE_LINK="+link, "LUMA_TEST_CERTIFICATE_TARGET="+target)
		hideProcess(command)
		if err := command.Run(); err != nil {
			t.Skipf("cannot create a synthetic directory link: %v", err)
		}
	}
	if _, err := validateDisc(cfg); !errors.Is(err, errInvalidSource) {
		t.Fatalf("linked CERTIFICATE directory was accepted: %v", err)
	}
}

func TestConfigRejectsInvalidUTF8AndOversizedLine(t *testing.T) {
	inputs := map[string][]byte{
		"UTF-8":       {'{', '"', 'x', '"', ':', '"', 0xff, '"', '}', '\n'},
		"oversized":   append(bytes.Repeat([]byte(" "), 16*1024*1024+1), '\n'),
		"no newline":  []byte(`{"sourceKind":"file"}`),
		"extra value": []byte("{} {}\n"),
	}
	for name, input := range inputs {
		t.Run(name, func(t *testing.T) {
			if _, err := readConfig(bufio.NewReader(bytes.NewReader(input))); err == nil {
				t.Fatal("invalid configuration accepted")
			}
		})
	}
}

func TestConfigRequiresLocalSupportedSourceAndAssignedSubnet(t *testing.T) {
	valid := fileConfig(t)
	unsupported := filepath.Join(t.TempDir(), "sample.exe")
	if err := os.WriteFile(unsupported, []byte("synthetic file"), 0600); err != nil {
		t.Fatal(err)
	}
	cases := map[string]func(*config){
		"wildcard":         func(c *config) { c.Listen = "0.0.0.0:48762" },
		"loopback":         func(c *config) { c.Listen = "127.0.0.1:48762"; c.Subnet = "127.0.0.0/8" },
		"public":           func(c *config) { c.Listen = "203.0.113.3:48762"; c.Subnet = "203.0.113.0/24" },
		"unassigned":       func(c *config) { c.Listen = "10.200.99.42:48762"; c.Subnet = "10.200.99.0/24" },
		"unrelated subnet": func(c *config) { c.Subnet = "172.16.34.0/24" },
		"overbroad subnet": func(c *config) { c.Subnet = "0.0.0.0/0" },
		"relative source":  func(c *config) { c.Source = "sample.mkv" },
		"UNC source":       func(c *config) { c.Source = `\\example.invalid\share\sample.mkv` },
		"unsupported file": func(c *config) { c.Source = unsupported },
		"bad track id":     func(c *config) { c.Subtitles[0].ID = "../media" },
		"duplicate track":  func(c *config) { c.Subtitles = append(c.Subtitles, c.Subtitles[0]) },
	}
	for name, change := range cases {
		t.Run(name, func(t *testing.T) {
			cfg := valid
			cfg.Subtitles = append([]subtitleConfig(nil), valid.Subtitles...)
			change(&cfg)
			if _, err := cfg.validate(); err == nil {
				t.Fatal("invalid configuration accepted")
			}
		})
	}
	if _, err := valid.validate(); err != nil {
		t.Fatalf("assigned private subnet and valid media rejected: %v", err)
	}
}

func TestPeerRequiresRFC1918IPv4WithinSubnet(t *testing.T) {
	for _, tc := range []struct {
		ip, subnet string
		want       bool
	}{
		{"192.168.80.8", "192.168.80.0/24", true},
		{"192.168.81.8", "192.168.80.0/24", false},
		{"10.4.5.6", "10.0.0.0/8", true},
		{"172.31.5.6", "172.16.0.0/12", true},
		{"172.32.5.6", "0.0.0.0/0", false},
		{"8.8.8.8", "0.0.0.0/0", false},
		{"169.254.0.1", "0.0.0.0/0", false},
		{"100.64.0.1", "0.0.0.0/0", false},
		{"127.0.0.1", "0.0.0.0/0", false},
		{"::ffff:192.168.80.8", "192.168.80.0/24", false},
		{"fc00::1", "fc00::/7", false},
	} {
		t.Run(strings.ReplaceAll(tc.ip, ":", "_"), func(t *testing.T) {
			if got := allowedPeer(netip.MustParseAddr(tc.ip), netip.MustParsePrefix(tc.subnet)); got != tc.want {
				t.Fatalf("allowedPeer = %t, want %t", got, tc.want)
			}
		})
	}
}
