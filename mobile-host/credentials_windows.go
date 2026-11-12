package main

import (
	"bytes"
	"runtime"
	"sync"
	"syscall"
	"unsafe"
)

var (
	kernel32      = syscall.NewLazyDLL("kernel32.dll")
	loadLibraryEx = kernel32.NewProc("LoadLibraryExW")
	moveFileEx    = kernel32.NewProc("MoveFileExW")
	loadDPAPI     = sync.OnceValues(func() (*syscall.DLL, error) {
		const searchSystem32 = 0x800
		name, _ := syscall.UTF16PtrFromString("crypt32.dll")
		handle, _, err := loadLibraryEx.Call(uintptr(unsafe.Pointer(name)), 0, searchSystem32)
		if handle == 0 {
			return nil, err
		}
		return &syscall.DLL{Name: "crypt32.dll", Handle: syscall.Handle(handle)}, nil
	})
)

type dataBlob struct {
	size uint32
	data *byte
}

func protectCredentials(data []byte) ([]byte, error) {
	return callDPAPI("CryptProtectData", data)
}

func unprotectCredentials(data []byte) ([]byte, error) {
	return callDPAPI("CryptUnprotectData", data)
}

func callDPAPI(operation string, data []byte) ([]byte, error) {
	const forbidUI = 0x1
	if len(data) == 0 || len(data) > maxCredentialBytes {
		return nil, errCredentialStore
	}
	library, err := loadDPAPI()
	if err != nil {
		return nil, errCredentialStore
	}
	procedure, err := library.FindProc(operation)
	if err != nil {
		return nil, errCredentialStore
	}
	entropy := []byte("LumaPlayer LAN pairing credentials v1")
	input := dataBlob{size: uint32(len(data)), data: &data[0]}
	optionalEntropy := dataBlob{size: uint32(len(entropy)), data: &entropy[0]}
	var output dataBlob
	ok, _, _ := procedure.Call(
		uintptr(unsafe.Pointer(&input)), 0, uintptr(unsafe.Pointer(&optionalEntropy)),
		0, 0, forbidUI, uintptr(unsafe.Pointer(&output)),
	)
	runtime.KeepAlive(data)
	runtime.KeepAlive(entropy)
	if output.data != nil {
		defer syscall.LocalFree(syscall.Handle(uintptr(unsafe.Pointer(output.data))))
	}
	if ok == 0 || output.data == nil || output.size == 0 || output.size > maxCredentialBytes {
		return nil, errCredentialStore
	}
	buffer := unsafe.Slice(output.data, int(output.size))
	result := bytes.Clone(buffer)
	clear(buffer)
	return result, nil
}

func lockCredentialFile(path string) (func(), error) {
	name, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		return nil, err
	}
	handle, err := syscall.CreateFile(
		name, syscall.GENERIC_READ|syscall.GENERIC_WRITE, 0, nil, syscall.OPEN_ALWAYS,
		syscall.FILE_ATTRIBUTE_HIDDEN|syscall.FILE_FLAG_OPEN_REPARSE_POINT, 0,
	)
	if err != nil {
		return nil, err
	}
	return func() { _ = syscall.CloseHandle(handle) }, nil
}

func replaceCredentialFile(from, to string) error {
	const replaceAndFlush = 0x1 | 0x8
	source, err := syscall.UTF16PtrFromString(from)
	if err != nil {
		return err
	}
	destination, err := syscall.UTF16PtrFromString(to)
	if err != nil {
		return err
	}
	ok, _, err := moveFileEx.Call(uintptr(unsafe.Pointer(source)), uintptr(unsafe.Pointer(destination)), replaceAndFlush)
	if ok == 0 {
		return err
	}
	return nil
}
