# Claude API Usage Display — Design Spec
_2026-05-21_

## Goal
Replace the rotating multi-pane display with a single, always-on Claude API usage screen. The ESP32-2432S028R fetches real token usage from the Anthropic API every 5 minutes and displays it on the built-in 320×240 ILI9341 TFT.

## Screen Layout
```
┌─────────────────────────────────┐
│       CLAUDE API USAGE          │  ← yellow, large
│                                 │
│  Input:      1,234,567 tokens   │
│  Output:       234,567 tokens   │
│  Total:      1,469,134 tokens   │
│                                 │
│  Monthly [██████████░░░░] 73%   │
│  1.47M of 2M token limit        │
│                                 │
│  Last sync:  14:32              │  ← gray, small
└─────────────────────────────────┘
```

## Architecture

### Credentials
- `src/config.h` (gitignored) holds WiFi SSID, WiFi password, Anthropic API key, and monthly token limit.
- `src/config.h.example` (committed) contains placeholder values so the repo documents the required fields without exposing secrets.

### Data Flow
1. Boot → connect WiFi (retry up to 10 times, 500 ms apart)
2. On connect → immediately fetch usage from Anthropic API
3. Render usage screen
4. Every 5 minutes → re-fetch and update numbers in place
5. On fetch failure → keep stale data, update "Last sync" to show minutes elapsed

### Networking
- Library: `WiFiClientSecure` + `HTTPClient` (both built into ESP32 Arduino)
- JSON parsing: `ArduinoJson` (added to `platformio.ini`)
- Endpoint: `GET https://api.anthropic.com/v1/usage` with current-month date range
- Headers: `x-api-key`, `anthropic-version`

### Error States
| Condition | Display |
|-----------|---------|
| WiFi connecting | "Connecting to WiFi…" |
| WiFi failed | "WiFi Error — check config.h" |
| API fetch failed | Keep last data, show "Sync failed X min ago" |
| No data yet | "Fetching…" placeholder |

### Monthly Limit
User-defined constant in `config.h` (`MONTHLY_TOKEN_LIMIT`). Progress bar = total tokens / limit. Bar turns red above 90%.

## Files Changed
| File | Change |
|------|--------|
| `src/config.h` | New — gitignored, real credentials |
| `src/config.h.example` | New — committed placeholder |
| `src/main.cpp` | Rewritten — WiFi, API fetch, single usage pane |
| `platformio.ini` | Add `ArduinoJson` dependency |
| `.gitignore` | Add `src/config.h` |
