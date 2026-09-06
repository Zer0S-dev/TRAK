# TRAK Connect — 3.0.11 test

## Scope

This test release validates the first end-to-end WebSocket/JSON path:

`TRAK -> WebSocket -> TRAK Connect server -> WordPress -> [trak_connect]`

The payload is intentionally limited to latitude/longitude:

```json
{"type":"position","lat":49.123456,"lon":1.234567}
```

## Firmware dependency

Install the Arduino `WebSockets` library (arduinoWebSockets / Links2004).

## Current transport scope

- Wi-Fi: enabled.
- `ws://`: enabled.
- WSS: not enabled in this test.
- A7670 raw TCP/WebSocket transport: not enabled in this test.
- Existing Trackserver path is untouched.

## Test URL

The current firmware test default is:

`ws://192.168.1.100:8765/trak`

It is stored in the `trakconnect` NVS namespace under `url` for the next dashboard configuration step.

## WordPress side

Install `TRAK_Connect_WP/` as a plugin directory. Install its Composer dependency and start `websocket-server.php` from CLI. The WebSocket daemon must be a persistent process; it cannot run inside a normal WordPress/PHP-FPM request.

Then create a WordPress page containing:

`[trak_connect]`

The shortcode displays the last received latitude/longitude.
