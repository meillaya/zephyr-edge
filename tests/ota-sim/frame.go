package main

import "encoding/binary"

// OTA frame opcodes shared with firmware/src/ota/ota_update.h and docs/OTA.md.
const (
	opStart  byte = 0x01
	opData   byte = 0x02
	opFinish byte = 0x03
	opAbort  byte = 0x04
)

// startFrame encodes [0x01][uint32 big-endian image size].
func startFrame(size uint32) []byte {
	frame := make([]byte, 5)
	frame[0] = opStart
	binary.BigEndian.PutUint32(frame[1:], size)
	return frame
}

// dataFrame encodes [0x02][chunk bytes].
func dataFrame(chunk []byte) []byte {
	frame := make([]byte, 1+len(chunk))
	frame[0] = opData
	copy(frame[1:], chunk)
	return frame
}

// finishFrame encodes [0x03].
func finishFrame() []byte { return []byte{opFinish} }

// abortFrame encodes [0x04].
func abortFrame() []byte { return []byte{opAbort} }
