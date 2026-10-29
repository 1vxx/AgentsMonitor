# Protocol and API (v1)

The authoritative type definitions are in `crates/agentsmon-protocol/src/lib.rs`; the dashboard mirrors them in `web/src/types.ts`. All timestamps are milliseconds since the Unix epoch. A session key is `<agent>:<session id>`.

The same routes are served on the Unix socket (`~/.agentsmon/agentsmon.sock`) and on the HTTP listener (`127.0.0.1:7788` by default).

## Ingest

### `POST /v1/ingest`

The body is one envelope or an array of envelopes. An invalid element is rejected on its own; the rest of the batch is still applied.

```json
[
  {"agent": "claude-code", "sid": "4f0c…", "ts": 1793087903676, "type": "tool.start", "id": "toolu_…", "tool": "Bash", "subject": "cargo test"},
  {"agent": "codex", "type": "raw", "payload": {"hook_event_name": "Stop", "session_id": "…"}}
]
```

Response: `{"accepted": <n>, "rejected": ["<reason>", …]}`. `rejected` is omitted when empty.

| Field | Description |
| --- | --- |
| `agent` | `claude-code`, `codex`, or any name of 1–64 characters |
| `sid` | The agent's session id, 1–128 characters. Optional for `raw` events, whose adapter reads it from the payload |
| `ts` | Optional. A timestamp ahead of the daemon's clock is replaced by the daemon's |
| `type` | The event type; the remaining fields depend on it |

### Events

Fields marked `?` are optional.

| `type` | Fields |
| --- | --- |
| `session.start` | `source?` (`startup`, `resume`, `clear`, `attach`, …) and session metadata: `cwd?` `project?` `model?` `permission_mode?` `agent_version?` `surface?` `interactive?` `transcript_path?` `title?` (the session's short title; the daemon also reads Claude Code's from its transcript) |
| `session.end` | `reason?` |
| `heartbeat` | `busy?` and session metadata |
| `prompt` | `preview?` |
| `turn.start` | `turn_id?` `agent_id?` (subagent) `preview?` |
| `turn.end` | `outcome` (`answer`, `aborted`, `refusal`, `error`) `turn_id?` `agent_id?` `duration_ms?` `usage?` (`input`, `output`, `cache_read`, `cache_write`, `model?`) `preview?` |
| `tool.start` | `tool` `id?` `agent_id?` `subject?` |
| `tool.end` | `tool` `id?` `agent_id?` `ok?` `duration_ms?` |
| `attention` | `kind` (`permission`, `input`, `idle`) `tool?` `subject?` `message?` |
| `attention.clear` | — |
| `compact.start` | `trigger?` |
| `compact.end` | — |
| `usage` | `context?` (`tokens?`, `window?`, `percent?`) `cost_usd?` `rate_limits?` (list of `kind`, `percent_used`, `resets_at?`) |
| `notice` | `kind` `message?` |
| `raw` | `payload`: an agent's own hook payload, mapped by that agent's adapter |

`heartbeat`, `usage` and `raw` are not kept in the event history. Their effect is kept in the session, the quota and the quota history.

### OpenTelemetry

| Route | Description |
| --- | --- |
| `POST /v1/logs` | OTLP/HTTP log export, optional. Claude Code `api_request` records become token counts, deduplicated by `request_id` with what the daemon reads from the transcripts; every other record is discarded |
| `POST /v1/metrics` | Accepted and ignored, so an exporter pointed here reports no errors |

Only the JSON encoding is accepted (`OTEL_EXPORTER_OTLP_LOGS_PROTOCOL=http/json`, no compression); protobuf receives `415`. Responses follow OTLP: `{"partialSuccess": {}}`. The body limit is 32 MiB.

## Read

| Route | Description |
| --- | --- |
| `GET /v1/health` | Name, version, protocol and uptime. Never requires a token |
| `GET /v1/snapshot` | `{server, sessions, quotas, summary}` for the sessions in memory: running ones not idle for 2 h, and those ended in the last 30 min (both under `[sessions]` in the config) |
| `GET /v1/summary` | The compact summary for display devices, its `ts` the time it is read. |
| `GET /v1/sessions` | `scope=live` (default) returns sessions in memory; `scope=all` pages through the store. Also `agent`, `limit` (default 100, max 1,000) and `offset` |
| `GET /v1/sessions/{key}` | One session, from memory or the store |
| `GET /v1/sessions/{key}/events` | The session's timeline, newest first. `before` (a `seq`) and `limit` (default 200, max 2,000) |
| `GET /v1/events` | The global event history, newest first. `key`, `before`, `limit` |
| `GET /v1/quotas` | The current rate-limit windows of each agent |
| `GET /v1/quotas/history` | Rate-limit readings. `agent`, `hours` (default 168, max 2,160) |
| `GET /v1/tokens` | Token totals (`input`, `output`, `cache_read`, `cache_write`, `requests`) over `[since, until)`, default the last 24 h. `group` = `model`, `source`, `agent` or `session` adds `groups`. `agent` (`claude-code`, `codex`, ...) counts only that agent's requests. `last_ts` is absent until the first request is counted |
| `GET /v1/tokens/series` | Totals per bucket from `since`. `bucket_ms` defaults to one hour, is at least one minute, and is widened to keep at most 2,000 buckets. Empty buckets are omitted. `agent` as for `/v1/tokens` |
| `GET /v1/stats` | Daemon metrics: throughput, rejected events, counted and duplicate requests, OTLP exports and service names, WebSocket clients, resident memory, CPU time, row counts, database size (`db_bytes`) and retention policy (`retention`) |

## Write

| Route | Description |
| --- | --- |
| `DELETE /v1/sessions/{key}` | Forgets a session and its events. Its token counts are kept |
| `POST /v1/maintenance` | Runs a cleanup pass now. `ended=all` also forgets every ended session. Returns `{forgotten, too_old, transcript_gone, ended, archived, db_bytes_before, db_bytes_after, duration_ms}` |

## WebSocket: `GET /v1/ws`

The first message is always a `snapshot`; changes follow. Add `?feed=1` to also receive history events.

| `type` | Payload |
| --- | --- |
| `snapshot` | As `GET /v1/snapshot` |
| `session` | `{session}`: a session that was created or changed, in full |
| `session.removed` | `{key}`: a session left memory (forgotten, archived, idle for 2 h, or ended more than 30 min ago) |
| `quota` | `{quota}` |
| `summary` | `{summary}`, only when it changed |
| `tokens` | `{ts, added}`: model requests were counted. Clients refresh their token windows |
| `event` | `{record}`, only with `feed=1` |

A client that falls behind the broadcast buffer receives a fresh `snapshot`.

## Authentication and origins

- **Unix socket:** owner-only (`0600`); no authentication.
- **HTTP:** when `http.token` is set, every route except `/v1/health` requires `Authorization: Bearer <token>` or `?token=<token>`.
- **Browsers:** a request with an `Origin` header is accepted only from `localhost`, `127.0.0.1` or `[::1]` on any port, or from an origin listed in `http.allowed_origins`. Accepted origins receive CORS headers.
