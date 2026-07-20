module github.com/zephyr-edge-platform/fleet-sim

go 1.24

require (
	github.com/eclipse/paho.mqtt.golang v1.5.0
	github.com/fxamacker/cbor/v2 v2.7.0
)

require (
	github.com/gorilla/websocket v1.5.3 // indirect
	github.com/x448/float16 v0.8.4 // indirect
	github.com/zephyr-edge-platform/cloud v0.0.0-00010101000000-000000000000
	golang.org/x/net v0.33.0 // indirect
	golang.org/x/sync v0.7.0 // indirect
)

replace github.com/zephyr-edge-platform/cloud => ../../cloud
