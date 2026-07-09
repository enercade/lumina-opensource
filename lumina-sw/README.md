# Lumina Control

Local test app to control Lumina devices, built with React + Vite. It talks to the devices through the MQTT broker (HiveMQ public broker by default) over WebSocket, so it works from any machine — it only needs internet access, not a direct connection to the device.

## Run

Requires Node.js 18+.

```sh
npm install
npm run dev
```

Open the printed URL (default `http://localhost:5173`).

## Usage

1. **Add a device** by its ID: 12 hex digits derived from the WiFi MAC (e.g. `a1b2c3d4e5f6`). The ID is shown on the device's setup portal page and printed on the serial console at boot as `lumina-<mac>`.
2. The app subscribes to `lumina-<mac>/info` (retained), so the device state appears immediately: online/offline, firmware, network, light state.
3. Use the controls — power, color, brightness, effect, buzzer, central-button mode, magia — each publishes a JSON command to `lumina-<mac>/cmd`.

Devices and the broker URL are remembered in the browser's localStorage.

## Broker

Default: `wss://broker.hivemq.com:8884/mqtt` (HiveMQ public broker, WebSocket over TLS). Change it from the ⚙ Broker panel — use the same broker the device is configured for.

> The public broker has no access control: anyone who knows a device ID can observe and control that device. Fine for demos and development; use a private broker for anything more.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
Copyright © 2026 Enercade srl, Torino, Italy.
