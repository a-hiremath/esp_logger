# esp_logger

Portable ESP32-based event logger for ultra-low-friction self-tracking (caffeine, melatonin, and subjective state).  
Designed as an end-to-end telemetry system: **device → MQTT → ingestion service → PostgreSQL → Grafana dashboards**.

This project is intentionally minimalist on-device (fast input, reliable timestamps) and powerful off-device (queryable database + visualization), so you can turn daily “micro-events” into analyzable time-series data.

---

## Why this exists

Most self-tracking fails because the logging interface is too high-friction. `esp_logger` solves that by providing a dedicated physical logger you can use in seconds, anywhere, with consistent event formatting and server-side observability.

**Goals (v1):**
- Portable logging with an encoder + OLED UI
- Accurate timestamps via hardware RTC
- Structured event schema for analysis
- Server pipeline with dashboards (Grafana) for fast inspection and trend analysis

---

## System overview

**Hardware (device)**
- ESP32
- Rotary encoder input
- SH1106 OLED display
- DS1302 RTC module (portable, offline timestamps)

**Backend (homelab / workstation)**
- MQTT broker (Mosquitto)
- Ingestion service (MQTT consumer → DB writer)
- PostgreSQL storage
- Grafana dashboards

**Data flow**
1. User logs an event on-device (e.g., caffeine dose).
2. Device timestamps it using the RTC.
3. Device publishes JSON event payload via MQTT.
4. Ingester validates/parses and writes to PostgreSQL (`events` table).
5. Grafana queries PostgreSQL for visualization and analysis.

---

## What it logs (v1)

### Objective events
- `caffeine` (mg)
- `melatonin` (mg)

### Subjective state (planned / minimal)
- Two low-friction ratings (e.g., energy + focus), logged 0–10

---

## Event schema

Events are published as JSON over MQTT with a stable schema version.

Example payload:

```json
{
  "schema": 1,
  "event_id": "esp32-01-1707770000",
  "timestamp": "2026-02-12T14:45:00",
  "device_id": "esp32-01",
  "event_type": "caffeine",
  "value": 100,
  "unit": "mg"
}
