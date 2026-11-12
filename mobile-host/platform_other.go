//go:build !windows

package main

import (
	"io/fs"
	"os/exec"
)

func localVolume(string) bool { return true }

func reparsePoint(info fs.FileInfo) bool { return info.Mode()&fs.ModeSymlink != 0 }

func hideProcess(*exec.Cmd) {}
