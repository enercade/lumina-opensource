// App.jsx - Lumina Control: local test app driving Lumina devices over MQTT.
import { useEffect, useMemo, useState } from 'react'
import { useLumina, DEFAULT_BROKER } from './useLumina.js'

const EFFECTS = [
  ['fix', 'Fixed'],
  ['breath', 'Breath'],
  ['halo', 'Halo'],
  ['bounce', 'Bounce'],
  ['demo', 'Demo'],
  ['magia', 'Magia'],
]

const BUZZERS = ['info', 'success', 'warning', 'error']
const CENTER_MODES = ['magia', 'buzzer', 'both']

const loadJson = (key, fallback) => {
  try {
    return JSON.parse(localStorage.getItem(key)) ?? fallback
  } catch {
    return fallback
  }
}

const normalizeDeviceId = (raw) =>
  raw.toLowerCase().replace(/^lumina-/, '').replace(/[^0-9a-f]/g, '')

export default function App() {
  const [broker, setBroker] = useState(() => loadJson('lumina.broker', DEFAULT_BROKER))
  const [deviceIds, setDeviceIds] = useState(() => loadJson('lumina.devices', []))
  const [selectedId, setSelectedId] = useState(() => loadJson('lumina.selected', null))
  const [newId, setNewId] = useState('')
  const [brokerDraft, setBrokerDraft] = useState(broker)
  const [showSettings, setShowSettings] = useState(false)

  const { connectionState, devices, sendCommand } = useLumina(broker, deviceIds)

  useEffect(() => localStorage.setItem('lumina.broker', JSON.stringify(broker)), [broker])
  useEffect(() => localStorage.setItem('lumina.devices', JSON.stringify(deviceIds)), [deviceIds])
  useEffect(() => localStorage.setItem('lumina.selected', JSON.stringify(selectedId)), [selectedId])

  const activeId = deviceIds.includes(selectedId) ? selectedId : deviceIds[0] ?? null
  const info = activeId ? devices[activeId] : null
  const online = Boolean(info?.online)

  const send = (cmd) => activeId && sendCommand(activeId, cmd)

  const addDevice = () => {
    const id = normalizeDeviceId(newId)
    if (id.length !== 12) return alert('Device ID must be 12 hex digits (the WiFi MAC), e.g. a1b2c3d4e5f6')
    if (!deviceIds.includes(id)) setDeviceIds([...deviceIds, id])
    setSelectedId(id)
    setNewId('')
  }

  const removeDevice = (id) => {
    setDeviceIds(deviceIds.filter((d) => d !== id))
    if (selectedId === id) setSelectedId(null)
  }

  const connLabel = useMemo(
    () => ({ connected: 'broker connected', connecting: 'connecting…', disconnected: 'disconnected', error: 'connection error' })[connectionState],
    [connectionState],
  )

  return (
    <div className="wrap">
      <header>
        <h1>💡 Lumina Control</h1>
        <button className="ghost" onClick={() => setShowSettings(!showSettings)}>⚙ Broker</button>
      </header>
      <p className={`conn ${connectionState}`}>{connLabel} · {broker}</p>

      {showSettings && (
        <section className="card">
          <label>MQTT broker (WebSocket URL)</label>
          <div className="row">
            <input value={brokerDraft} onChange={(e) => setBrokerDraft(e.target.value)}
              placeholder={DEFAULT_BROKER} />
            <button onClick={() => { setBroker(brokerDraft.trim() || DEFAULT_BROKER); setShowSettings(false) }}>Apply</button>
          </div>
          <small>Default: HiveMQ public broker. Anyone who knows a device ID can control it — use a private broker for anything beyond testing.</small>
        </section>
      )}

      <section className="card">
        <label>Devices</label>
        <div className="row">
          <input value={newId} onChange={(e) => setNewId(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && addDevice()}
            placeholder="Device ID (12 hex digits, printed at boot / on the setup page)" />
          <button onClick={addDevice}>Add</button>
        </div>
        <div className="devices">
          {deviceIds.map((id) => {
            const d = devices[id]
            return (
              <div key={id}
                className={`device ${id === activeId ? 'active' : ''}`}
                onClick={() => setSelectedId(id)}>
                <span className={`dot ${d?.online ? 'on' : 'off'}`} />
                <span className="devid">lumina-{id}</span>
                <button className="ghost" onClick={(e) => { e.stopPropagation(); removeDevice(id) }}>✕</button>
              </div>
            )
          })}
          {deviceIds.length === 0 && <small>No devices yet — add one by its ID.</small>}
        </div>
      </section>

      {activeId && (
        <>
          <section className="card">
            <label>Status — lumina-{activeId}</label>
            {info ? (
              <div className="status">
                <span className={`badge ${online ? 'ok' : 'ko'}`}>{online ? 'online' : 'offline'}</span>
                {info.fw && <span>fw v{info.fw}</span>}
                {info.ssid && <span>{info.ssid}</span>}
                {info.ip && <span>{info.ip}</span>}
                {typeof info.rssi === 'number' && info.rssi !== 0 && <span>{info.rssi} dBm</span>}
                {typeof info.uptime_s === 'number' && <span>up {formatUptime(info.uptime_s)}</span>}
              </div>
            ) : (
              <small>Waiting for the device to publish its state…</small>
            )}
            <button className="ghost" onClick={() => send({ get: 'info' })}>Refresh</button>
          </section>

          <section className="card">
            <label>Power</label>
            <div className="row">
              <button className={`power on ${info?.power === 1 ? 'active' : ''}`} onClick={() => send({ power: 1 })}>ON</button>
              <button className={`power off ${info?.power === 0 ? 'active' : ''}`} onClick={() => send({ power: 0 })}>OFF</button>
            </div>
          </section>

          <section className="card">
            <label>Color</label>
            <input type="color" value={`#${info?.color ?? '00aaff'}`}
              onChange={(e) => send({ color: e.target.value.slice(1) })} />
          </section>

          <section className="card">
            <label>Brightness <span className="val">{info?.brightness ?? '–'}%</span></label>
            <input type="range" min="0" max="100"
              value={info?.brightness ?? 50}
              onChange={(e) => send({ brightness: Number(e.target.value) })} />
          </section>

          <section className="card">
            <label>Effect</label>
            <div className="grid">
              {EFFECTS.map(([value, name]) => (
                <button key={value} className={info?.effect === value ? 'active' : ''}
                  onClick={() => send({ effect: value })}>{name}</button>
              ))}
            </div>
          </section>

          <section className="card">
            <label>Buzzer</label>
            <div className="grid">
              {BUZZERS.map((b) => (
                <button key={b} onClick={() => send({ buzzer: b })}>{b}</button>
              ))}
            </div>
          </section>

          <section className="card">
            <label>Central button action</label>
            <div className="grid three">
              {CENTER_MODES.map((m) => (
                <button key={m} className={info?.cnmode === m ? 'active violet' : ''}
                  onClick={() => send({ cnmode: m })}>{m}</button>
              ))}
            </div>
            <button className="magia" onClick={() => send({ magia: true })}>✨ Magia!</button>
          </section>
        </>
      )}

      <footer>
        <small>Lumina · Enercade srl · COACH / LAUDS Factories</small>
      </footer>
    </div>
  )
}

function formatUptime(seconds) {
  if (seconds < 60) return `${seconds}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return `${h}h ${m}m`
}
