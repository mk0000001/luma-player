package main

import (
	"context"
	"io"
	"time"
)

const mediaReadIdleTimeout = 25 * time.Second

// The deadline covers an individual upstream read and is suspended while
// writing downstream. onIdle must interrupt a blocked source read.
type idleMediaReader struct {
	source  io.Reader
	onIdle  func()
	timer   *time.Timer
	expired bool
}

func (r *idleMediaReader) Read(data []byte) (int, error) {
	if r.expired {
		return 0, context.DeadlineExceeded
	}
	if r.timer == nil {
		r.timer = time.AfterFunc(mediaReadIdleTimeout, r.onIdle)
	} else {
		r.timer.Reset(mediaReadIdleTimeout)
	}
	n, err := r.source.Read(data)
	if !r.timer.Stop() {
		r.expired = true
		return n, context.DeadlineExceeded
	}
	return n, err
}
