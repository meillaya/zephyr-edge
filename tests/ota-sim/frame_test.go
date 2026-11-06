package main

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestStartFrame(t *testing.T) {
	got := startFrame(0x00040000)
	want := []byte{0x01, 0x00, 0x04, 0x00, 0x00}
	if !bytes.Equal(got, want) {
		t.Fatalf("startFrame = %s, want %s", hex.EncodeToString(got), hex.EncodeToString(want))
	}
}

func TestDataFrame(t *testing.T) {
	got := dataFrame([]byte("ab"))
	want := []byte{0x02, 'a', 'b'}
	if !bytes.Equal(got, want) {
		t.Fatalf("dataFrame = %s, want %s", hex.EncodeToString(got), hex.EncodeToString(want))
	}
}

func TestControlFrames(t *testing.T) {
	if got := finishFrame(); !bytes.Equal(got, []byte{0x03}) {
		t.Fatalf("finishFrame = %x", got)
	}
	if got := abortFrame(); !bytes.Equal(got, []byte{0x04}) {
		t.Fatalf("abortFrame = %x", got)
	}
}
