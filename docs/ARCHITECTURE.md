# Architecture

## Overview

```
Integrations (one per agent)        agentsmon daemon                                   Consumers
────────────────────────────        ──────────────────────────────────────────         ─────────────────
Claude Code plugin ─ canonical ─▶   ingest ─▶ adapters ─▶ reducer ─▶ hub ─────────▶    WebSocket, REST
Command hooks ────── raw ───────▶   (UDS)     (raw → canonical)        │                (dashboard, CLI,
Claude Code OTel ─── OTLP JSON ─▶   /v1/logs ─▶ token ledger ──────────┤                 display device)
                                                                       ├─▶ store thread (SQLite)
                                                       janitor ────────┘
```

| Component | Source | Role |
| --- | --- | --- |
| Integrations | `plugins/`, `agentsmon hook` | Report what the agent does. First-party plugins send canonical events; agents limited to command hooks forward their payloads unchanged as `raw` events |
| Adapters | `src/adapters/` | The only agent-specific code in the daemon: map raw hook payloads and OpenTelemetry records to canonical form, and define each agent's liveness `Profile` |
| Reducer | `src/reducer.rs` | Pure function `apply(session, ts, event)`: no clock, no I/O, fully unit-tested |
| Hub | `src/hub.rs` | Owns sessions, quotas and the event sequence behind one lock. Each batch serializes every changed session once and broadcasts it |
| Store | `src/store.rs` | SQLite in WAL mode. One thread batches writes into transactions; reads use a second connection |
| Janitor | `src/janitor.rs` | Follows deletion and archiving in Claude, applies retention, compacts the database |
| OTLP | `src/otlp.rs` | Reads token counts from OTLP/HTTP JSON log exports |
| Desktop | `src/desktop.rs` | Reads Claude Desktop's archive state |
| Titles | `src/titles.rs` | Reads the short title Claude Code gives each session from its transcript |

The daemon runs on a single-threaded Tokio runtime. File system and database work that may block runs on the blocking pool.

## Canonical events

Every agent maps onto one vocabulary, specified in [PROTOCOL.md](PROTOCOL.md):

`session.start` `session.end` `heartbeat` `prompt` `turn.start` `turn.end` `tool.start` `tool.end` `attention` `attention.clear` `compact.start` `compact.end` `usage` `notice`

`turn.*` and `tool.*` carry an optional `agent_id` for subagents. `usage` carries context fill, the cost estimate and the account's rate-limit windows; the daemon files the windows under the agent's account-level quota.

## Status derivation

Events record only what is in flight: the main turn, running tools, subagents, a pending approval or question, a compaction. `derive_status` reads the status from those facts, in this order of precedence:

`ended` > `waiting_permission` > `waiting_input` > `compacting` > `tool` > `thinking` > `idle`

- The end of a main turn clears the main loop's tools and pending items. Background subagents remain until their own `turn.end`, or until a heartbeat reports `busy: false`.
- A pending approval clears when its tool ends. A parallel tool ending does not clear it.
- `busy: false` in a heartbeat means the integration has nothing in flight: the daemon drops any turn or tool whose end event was lost.
- Activity on an ended session revives it (a resume, or heartbeats after a `lost` end).

## Liveness

| | Claude Code (heartbeat every 20 s) | Other agents |
| --- | --- | --- |
| Marked `stale` | After 65 s of silence | After 120 s of silence, only if it sends heartbeats |
| Ended as `lost` | After 30 min of silence | After 12 h of silence |

The sweep runs every 5 s. A gap of more than 30 s between sweeps means the machine slept; no session is judged by its silence for the next 75 s. On start, the daemon restores unfinished sessions from SQLite as `stale` until they report again.

## Claude Code plugin

`plugins/claude-code/hooks/register.ts` uses Claude Code's hooks-module API. It runs inside Claude Code, so no process starts per event.

| Engine event | Canonical events |
| --- | --- |
| `session.start`, `classic.SessionStart` | `session.start`, sent once; the latter adds `source`, the session id, permission mode and transcript path |
| `session.end` | `session.end`, sent within the exit budget. After `/clear` the plugin continues under the new id |
| `turn.start`, `turn.complete` | `prompt`, `turn.start`, `turn.end` with subagent id, duration, token usage and answer preview |
| `tool.call` (wrapping `next`) | `tool.start`, `tool.end`; `AskUserQuestion` also raises `attention` (input) |
| `classic.PermissionRequest` | `attention` (permission) |
| `classic.Notification` | `idle_prompt` → `attention` (idle); `elicitation_dialog` → `attention` (input) |
| `classic.PreCompact`, `classic.PostCompact` | `compact.start`, `compact.end` |
| `session.measure` | `usage`: context, cost, five-hour and seven-day windows |
| `classic.UserPromptSubmit` | A heartbeat when the permission mode changed |
| `session.attach` | Announces a session that was waiting to be used |

**Announcing.** Interactive (terminal) sessions announce at launch. Non-interactive ones (Claude Desktop, SDK, `-p`) announce on first use: the first turn, the first tool call, or a surface attaching. Claude Desktop keeps a spare session warm for the next new session; until it is used, it never appears in the monitor. Metadata such as the project name is read at announce time, after the spare has been claimed.

**Delivery.** Hooks only append to a queue. `$.clock.after(0)` sends after the hook's dispatch has resolved, so a session never waits on the monitor. One request is in flight at a time, which keeps events ordered. Failures retry with exponential backoff from 1 s to 30 s. The queue holds at most 1,000 events and drops the oldest. On announce, the plugin also reports `$.session.usage()`, so rate limits are known before the first answer.

**Known limitation.** The API has no event for an approval being granted. A long command that was approved shows `waiting_permission` until it finishes; a denied one clears at once.

## Token accounting

Token counts are the only figures that must be exact, so they are kept in a ledger of their own rather than derived from status events.

- **Source.** Claude Code's OpenTelemetry `api_request` event: one per model request, with `request_id`, the four token counts, the model, `query_source` (main loop, subagent, compaction, background) and the session id. It covers every request Claude Code makes, including background requests that never reach a transcript. The daemon accepts OTLP JSON at `POST /v1/logs` and discards every other record. A record is recognised when its `service.name` starts with `claude-code` (Claude Desktop reports `claude-code-desktop`) or its scope is `com.anthropic.claude_code.events`.
- **Exactly once.** The ledger's primary key is `request_id` (`INSERT OR IGNORE`). An in-memory window of up to 50,000 ids from the last 6 h, seeded from the last 2 h of the ledger on start, keeps session totals exact across exporter retries, duplicate deliveries and restarts.
- **Attribution.** Each request is added to its session's `stats`, which sets `exact_tokens`; from then on `turn.end` usage is no longer added. Requests that arrive after the session ended (the final export at exit) still count. Requests for a session not yet known wait until it appears.
- **Retention.** Requests are kept individually for `token_detail_days`, then folded into `token_hourly` by whole hours, then into `token_daily` by whole days, and dropped after `token_daily_days`. Every query reads all three tables, so totals do not change when data is folded. A window is exact to the request, hour or day, depending on how far back it reaches.
- **Verification.** For a real session with a subagent, the ledger matched the session transcript request by request (6 of 6, all four counts), and the session's totals matched the ledger.
- **Fallback.** Without OpenTelemetry export, session token figures are summed from turn ends. That misses interrupted turns and requests outside turns, and the dashboard marks those figures `≈`.

## Retention and cleanup

The janitor runs on two schedules.

**Every 15 s**, over the sessions in memory (what the dashboard shows):

- An ended session whose transcript no longer exists was deleted in Claude Code, or never kept one. A running session whose transcript existed and is now gone was deleted while running. Both are forgotten at once. A 24 h tombstone stops a late heartbeat or end event from recreating them; a new `session.start` may.
- A session archived in Claude Desktop leaves memory, and therefore the live view, but stays in the store. `desktop.rs` reads `archived-sessions.idx` only when it changes and maps each local id to the Claude Code session id through `cliSessionId` in `local_<id>.json`. The format is internal to Claude Desktop: it is read-only and best effort, and anything unreadable is skipped.
- A session that reappears after leaving memory (unarchived, or resumed days later) is reloaded from the store, so its statistics continue.
- Each Claude Code session takes its title from its transcript, where Claude Code writes `custom-title` (a `/rename`, or Claude Desktop's title), `ai-title` (its generated one) and `agent-name` lines. The latest of each kind counts and the kinds rank in that order. `titles.rs` reads only what each transcript gained since the last pass and parses only lines that name a title kind. Like the Desktop records, the format is internal: read-only and best effort. A stored session out of memory without a title is looked at once by the 10 min pass.

**At start and every 10 min**, and on demand through `POST /v1/maintenance`:

1. Ended sessions older than `sessions_days`, or whose transcript is gone, are removed from memory, the store and connected clients, together with their events. With `ended=all`, every ended session is removed.
2. The store prunes events and rate-limit readings past their retention and folds token tiers by whole buckets. It then runs `PRAGMA incremental_vacuum` and `wal_checkpoint(TRUNCATE)` to return space to the file system. A database created without incremental auto-vacuum is converted once, with `VACUUM`, on first open.

Removing a session never removes its token counts: the ledger records account usage.

## Shutdown

SIGINT, SIGTERM and SIGHUP (the terminal closing) trigger the same sequence: remove the socket, then wait up to 3 s for the store to commit everything received. SIGKILL cannot be handled; the data written so far is already committed in small batches, and the stale socket is replaced on the next start.

## Security model

- The Unix socket is owner-only (`0600`) and needs no authentication.
- The TCP listener binds to loopback by default. A non-loopback address requires `http.token`, sent as `Authorization: Bearer <token>` or `?token=`.
- Requests with an `Origin` header are accepted only from loopback origins and `http.allowed_origins`. This stops other web pages from reading sessions or posting events.

## Adding an agent

Codex is the next integration. Neither the protocol nor the reducer changes.

1. **Adapter.** Add `src/adapters/codex.rs` with `normalize(&Value) -> Result<Normalized, AdapterError>` and register it, with a liveness `Profile`, in `adapters/mod.rs`. Codex hook events (`SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `PermissionRequest`, `Stop`, `Interrupt`, `SessionEnd`) resemble Claude Code's command hooks; `adapters/claude.rs` is the model. Map `Interrupt` to `turn.end` (aborted) and `request_user_input` to `attention` (input). `adapters/tool.rs` already extracts subjects from `shell` and `exec_command`.
2. **Integration.** Add `plugins/codex/` with `.codex-plugin/plugin.json` whose `hooks` file runs `agentsmon hook --agent codex` for each event (`timeout: 1`). The forwarder prints nothing and always exits 0.
3. **Tokens and rate limits.** Map Codex's own OpenTelemetry events in `adapters::usage_from_otel`. Its rate-limit windows are written to the session rollout logs (`~/.codex/sessions/**/rollout-*.jsonl`); a collector task can read them incrementally and emit `usage` events (confirm field names against real logs).
4. **Dashboard.** `web/src/agents.ts` already names Codex; agents not listed there appear under their own name.

## Display device

The ESP32 display (1.75" AMOLED) is the next phase. The data it needs is already served:

- `GET /v1/summary` returns a compact summary: the overall `mood`, counts per status, the eight most urgent sessions and each agent's rate limits. The WebSocket sends a `summary` message only when the summary changes.
- `mood` is a single signal for the animation: `attention` (someone waits for you) > `working` > `error` (a turn failed in the last 5 min) > `idle` > `offline`.
- For access over the LAN, set `http.listen = "0.0.0.0:7788"` and `http.token`.
