package main

import (
	"io/fs"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

var getDriveType = syscall.NewLazyDLL("kernel32.dll").NewProc("GetDriveTypeW")

func reparsePoint(info fs.FileInfo) bool {
	data, ok := info.Sys().(*syscall.Win32FileAttributeData)
	return ok && data.FileAttributes&syscall.FILE_ATTRIBUTE_REPARSE_POINT != 0
}

func localVolume(path string) bool {
	volume := filepath.VolumeName(path)
	if len(volume) != 2 || volume[1] != ':' || strings.ContainsRune(path[2:], ':') {
		return false
	}
	root, err := syscall.UTF16PtrFromString(volume + "\\")
	if err != nil {
		return false
	}
	driveType, _, _ := getDriveType.Call(uintptr(unsafe.Pointer(root)))
	const (
		driveRemovable = 2
		driveFixed     = 3
		driveCDROM     = 5
		driveRAMDisk   = 6
	)
	return driveType == driveRemovable || driveType == driveFixed || driveType == driveCDROM || driveType == driveRAMDisk
}

func hideProcess(command *exec.Cmd) {
	const createNoWindow = 0x08000000
	command.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: createNoWindow}
}
