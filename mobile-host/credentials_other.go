//go:build !windows

package main

func protectCredentials([]byte) ([]byte, error)   { return nil, errCredentialStore }
func unprotectCredentials([]byte) ([]byte, error) { return nil, errCredentialStore }
func lockCredentialFile(string) (func(), error)   { return nil, errCredentialStore }
func replaceCredentialFile(string, string) error  { return errCredentialStore }
