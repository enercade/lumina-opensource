// useLumina.js - MQTT hook for talking to Lumina devices.
//
// Topics (topic root = "lumina-<mac>", 12 lowercase hex digits):
//   lumina-<mac>/cmd   commands to the device (JSON)
//   lumina-<mac>/info  retained state published by the device (JSON)
import { useEffect, useRef, useState, useCallback } from 'react'
import mqtt from 'mqtt'

export const DEFAULT_BROKER = 'wss://broker.hivemq.com:8884/mqtt'

const topicRoot = (deviceId) => `lumina-${deviceId}`

// Connects to the broker and tracks the info topic of every known device.
// Returns per-device state plus a sendCommand(deviceId, cmd) function.
export function useLumina(brokerUrl, deviceIds) {
  const clientRef = useRef(null)
  const [connectionState, setConnectionState] = useState('connecting')
  const [devices, setDevices] = useState({})

  // One connection per broker URL.
  useEffect(() => {
    setConnectionState('connecting')
    const client = mqtt.connect(brokerUrl, {
      clientId: `lumina-control-${Math.random().toString(16).slice(2, 10)}`,
      keepalive: 30,
      reconnectPeriod: 3000,
    })
    clientRef.current = client

    client.on('connect', () => setConnectionState('connected'))
    client.on('reconnect', () => setConnectionState('connecting'))
    client.on('close', () => setConnectionState('disconnected'))
    client.on('error', () => setConnectionState('error'))

    client.on('message', (topic, payload) => {
      const match = topic.match(/^lumina-([0-9a-f]{12})\/info$/)
      if (!match) return
      let info
      try {
        info = JSON.parse(payload.toString())
      } catch {
        return
      }
      setDevices((prev) => ({
        ...prev,
        [match[1]]: { ...info, lastSeen: Date.now() },
      }))
    })

    return () => {
      client.end(true)
      clientRef.current = null
    }
  }, [brokerUrl])

  // Keep subscriptions aligned with the device list (retained info arrives
  // immediately on subscribe).
  const idsKey = deviceIds.join(',')
  useEffect(() => {
    const client = clientRef.current
    if (!client) return
    const topics = deviceIds.map((id) => `${topicRoot(id)}/info`)
    if (topics.length) client.subscribe(topics, { qos: 1 })
    return () => {
      if (topics.length && client.connected) client.unsubscribe(topics)
    }
  }, [idsKey, connectionState])

  const sendCommand = useCallback((deviceId, cmd) => {
    const client = clientRef.current
    if (!client || !client.connected) return false
    client.publish(`${topicRoot(deviceId)}/cmd`, JSON.stringify(cmd), { qos: 1 })
    return true
  }, [])

  return { connectionState, devices, sendCommand }
}
