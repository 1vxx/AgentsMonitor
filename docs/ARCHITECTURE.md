# Architecture

## Overview

```
Integrations (one per agent)        agentsmon daemon                                   Consumers
────────────────────────────        ──────────────────────────────────────────         ─────────────────
Claude Code plugin ─ canonical ─▶   ingest ─▶ adapters ─▶ reducer ─▶ hub ─────────▶    WebSocket, REST
Codex plugin ─────── raw ───────▶   (UDS)     (raw → canonical)        │                (dashboard, CLI,
Claude transcripts ◀─ read ─────    claude collector ──────────────────┤                 display device)
Claude Code OTel ─── OTLP JSON ─▶   /v1/logs ─▶ token ledger ──────────┤
Codex rollout logs ◀─ read ─────    codex collector ───────────────────┤
                                                                       ├─▶ store thread (SQLite)
                                                       janitor ────────┘
```

| Component | Source | Role |
| --- | --- | --- |
| Integrations | `plugins/`, `agentsmon hook` | Report what the agent does. The Claude Code plugin sends canonical events; the Codex plugin, limited to command hooks, forwards their payloads unchanged as `raw` events |
| Adapters | `src/adapters/` | The only agent-specific code in the daemon: map raw hook payloads and OpenTelemetry records to canonical form, and define each agent's liveness `Profile` |
| Reducer | `src/reducer.rs` | Pure function `apply(session, ts, event)`: no clock, no I/O, fully unit-tested |
| Hub | `src/hub.rs` | Owns sessions, quotas and the event sequence behind one lock. Each batch serializes every changed session once and broadcasts it |
| Store | `src/store.rs` | SQLite in WAL mode. One thread batches writes into transactions; reads use a second connection |
| Janitor | `src/janitor.rs` | Follows deletion and archiving in Claude, applies retention, compacts the database |
| Claude | `src/claude.rs` | Reads token counts from Claude Code's transcripts, and the account's usage limits from the reading Claude Code keeps, fetching it again with `claude -p /usage` while Claude Code is at work |
| OTLP | `src/otlp.rs` | Reads token counts from OTLP/HTTP JSON log exports (optional) |
| Codex | `src/codex.rs` | Reads token counts, context and rate limits from Codex's rollout logs, and titles, archiving and deletion from its thread records |
| Tail | `src/tail.rs` | Reads on in a log file another program appends to, line by line through a 64 KB buffer, and knows it again by its first bytes, so a replaced file is read from the top. Keeps folder listings until a folder's modification time moves |
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

| | Claude Code (heartbeat every 20 s) | Codex and other agents |
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

## Codex plugin

`plugins/codex/` holds `.codex-plugin/plugin.json` and `hooks/hooks.json`, which runs `hooks/forward.sh` for all twelve lifecycle events. The script finds `agentsmon` (on `PATH`, in `~/.cargo/bin`, `/opt/homebrew/bin` or `/usr/local/bin`, or at `$AGENTSMON_BIN`) and runs `agentsmon hook --agent codex`, which forwards the payload and answers `{}`: Codex's `Stop` hook expects JSON. The hooks are synchronous, which keeps events in order; Codex runs `async: true` hooks concurrently. `.agents/plugins/marketplace.json` is the Codex marketplace entry, beside Claude Code's `.claude-plugin/marketplace.json`.

`adapters/codex.rs` maps the payloads:

| Codex hook | Canonical events |
| --- | --- |
| `SessionStart`, `SessionEnd` | `session.start` with `source`, `session.end` |
| `UserPromptSubmit` | `prompt`, `turn.start` |
| `PreToolUse`, `PostToolUse` | `tool.start`, `tool.end`; `request_user_input` also raises `attention` (input) |
| `PermissionRequest` | `attention` (permission) |
| `Stop`, `Interrupt` | `turn.end` (answer, aborted) |
| `SubagentStart`, `SubagentStop` | `turn.start`, `turn.end` with the subagent id |
| `PreCompact`, `PostCompact` | `compact.start`, `compact.end` |

`request_user_input_async` returns before the person answers and the turn ends with the question open. The adapter remembers the question and raises `attention` (input) after that turn's `turn.end`; the next prompt clears it. Every payload carries `cwd`, `model` and `permission_mode`. `transcript_path` is left out: Codex calls its format unstable and moves the file on archiving or removes it once history moves into its database, so the janitor follows the thread records instead.

**Known limitations.** Codex has no hook for a failed turn, an idle reminder or a granted approval, and command hooks cannot beat: a Codex session that crashes keeps its last status until 12 h of silence end it as `lost`. Hosted tools such as web search run no hooks.

## Token accounting

Token counts are the only figures that must be exact, so they are kept in a ledger of their own rather than derived from status events.

- **Source.** Claude Code's transcripts: `<config>/projects/<project>/<session id>.jsonl`, and `<session id>/subagents/agent-<id>.jsonl` for each subagent, under `~/.claude` or `$CLAUDE_CONFIG_DIR`. Every reply of the model is an `assistant` line per content block, each repeating the reply's `usage` (the four token counts), its model and the request's `requestId`; the collector counts a reply once, from its first line. `isSidechain` marks a subagent's (`source`: `subagent`, else `main`). Lines from Claude Code itself (model `<synthetic>`: an API error, an interruption) are no request. `claude.rs` reads what each file gained every 2 s and parses only lines that name `"type":"assistant"`; files not written to for a day are looked at on the full walk, once a minute. A folder is listed again only when its modification time moved, or within 2 s of a change (a coarse clock may not move for an entry added in the same second): listing the project folders and every session's `subagents` folder on each pass was most of what the collector cost. The format is internal to Claude Code: read-only and best effort.
- **What transcripts miss.** Requests whose reply is not part of the conversation write nothing there: context compaction, and the small model's web search, web fetch and session titles. Claude Code's OpenTelemetry `api_request` event covers every request, these too, with the same request id and `query_source` (`compact`, `web_search_tool`, ...). The daemon accepts OTLP JSON at `POST /v1/logs` and discards every other record; a record is recognised when its `service.name` starts with `claude-code` (Claude Desktop reports `claude-code-desktop`) or its scope is `com.anthropic.claude_code.events`. Exporting is optional: it adds those requests, and a request both sources report counts once. The `cost-state` line Claude Code writes on exit is not used to fill the gap: its totals belong to the process, which may have run several sessions, name models as they were asked for (an alias, a gateway's route) and start over on some resumes.
- **Exactly once.** The ledger's primary key is `request_id` (`INSERT OR IGNORE`). An in-memory window of up to 50,000 ids from the last 6 h, seeded from the last 2 h of the ledger on start, keeps session totals exact across exporter retries, duplicate deliveries and restarts. The transcript collector also drops what the ledger holds already before handing requests on: a reply whose first line was read before a restart, the history a forked session copies into its new transcript, and requests the OpenTelemetry export brought first.
- **Cursors.** Both log collectors keep in `log_cursors` how far each file is counted, and a hash of its first kilobyte (`tail.rs`). A file that shrank or starts with other bytes was replaced, not appended to, and is read from the top; its requests already counted are dropped by their ids. The first run reads every transcript and rollout there is, so the history before the daemon was installed is counted too.
- **Attribution.** Each request is added to its session's `stats`, which sets `exact_tokens`; from then on `turn.end` usage is no longer added. Requests that arrive after the session ended (the final export at exit) still count. Requests for a session not yet known wait until it appears.
- **Retention.** Requests are kept individually for `token_detail_days`, then folded into `token_hourly` by whole hours, then into `token_daily` by whole days, and dropped after `token_daily_days`. Every query reads all three tables, so totals do not change when data is folded. A window is exact to the request, hour or day, depending on how far back it reaches.
- **Verification.** For real sessions, the transcripts held the same main-loop and subagent requests as the OpenTelemetry export, request by request (207 of 207, 112 of 112, 237 of 237, all four counts); what the export had beyond them was compaction and the small model's tool requests. After reading every transcript on a machine with OpenTelemetry on, each session's totals in memory matched the ledger.
- **Codex.** Codex writes a `token_usage_record` after every model request to the thread's rollout log (`~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl`), keyed by its `response_id`. `codex.rs` reads what each file gained every 2 s and keeps in `log_cursors` how far each file is counted, so a request counts once across restarts and requests made while the daemon was down are counted when it is back. A subagent's thread logs to a file of its own under its root session's id, and its requests count there with its kind as the source (`guardian`, `subagent`). OpenAI counts cached input inside the input, so the ledger's input is the uncached part. The same files' `token_count` events give the session's context fill and the account's windows (`five_hour`, `seven_day`). What the logs say about a session the plugin has not reported yet waits for it for 10 min; files never create sessions. Codex's OpenTelemetry events carry no request id, so they are not used.
- **Fallback.** Until a session's first request is read, its token figures are summed from turn ends. That misses interrupted turns and requests outside turns, and the dashboard marks those figures `≈`.

## Usage limits

Each agent's account windows (`five_hour`, `seven_day`, and the weekly windows of a model) are kept as its `Quota`, with a history of readings. A reading older than the one held only goes to the history.

- **Claude Code.** The plugin reports the windows after every turn (`session.measure`), from the replies' rate-limit headers. Without it, `claude.rs` reads `cachedUsageUtilization` in Claude Code's global config (`~/.claude.json`, or `$CLAUDE_CONFIG_DIR/.claude.json`): the windows as Claude Code last fetched them, with exact reset times and `fetchedAtMs`. Only the file's change time is looked at until it changes. Claude Code fetches that reading for `/usage` only, so when a transcript grew within the last 10 min and the reading held is older than that, the daemon runs `claude -p /usage --output-format json --tools "" --strict-mcp-config --setting-sources "" --no-session-persistence` from a folder of its own: no model is asked, no user setting or plugin loads, no session is kept, and telemetry is off. At most one run is in flight, at most one per interval, and one that takes more than 60 s is stopped. `claude` is found on `PATH`, in `~/.local/bin`, `~/.claude/local`, the npm, Volta and Bun folders, Homebrew's, or the newest nvm Node; its folder goes first on the run's `PATH`, so a Node install finds its `node` under a login agent. `[claude] usage_refresh_minutes` sets the interval (`0`: never run), `[claude] binary` the program. Claude Code records the folder it ran in as a project, with an empty `memory` folder: one, the same each time.
- **Codex.** The `token_count` events in its rollout logs carry the windows after every request.

## Retention and cleanup

The janitor runs on two schedules.

**Every 15 s**, over the sessions in memory (what the dashboard shows):

- An ended session whose transcript no longer exists was deleted in Claude Code, or never kept one. A running session whose transcript existed and is now gone was deleted while running. Both are forgotten at once. A 24 h tombstone stops a late heartbeat or end event from recreating them; a new `session.start` may.
- A session archived in Claude Desktop leaves memory, and therefore the live view, but stays in the store. `desktop.rs` reads `archived-sessions.idx` only when it changes and maps each local id to the Claude Code session id through `cliSessionId` in `local_<id>.json`. The format is internal to Claude Desktop: it is read-only and best effort, and anything unreadable is skipped.
- A Codex session follows its row in Codex's thread records (`~/.codex/state_<n>.sqlite`, read-only): archived there, it leaves the live view; gone from there after it was found, it was deleted and is forgotten. Its title is the thread's `name`, the one Codex shows.
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

Neither the protocol nor the reducer changes; Codex was added this way.

1. **Adapter.** Add `src/adapters/<agent>.rs` with `normalize(&Value) -> Result<Normalized, AdapterError>` and register it, with a liveness `Profile`, in `adapters/mod.rs`. `claude.rs` and `codex.rs` are the models.
2. **Integration.** A plugin or settings entry whose hooks run `agentsmon hook --agent <agent>` for each event, as `plugins/codex/` does.
3. **Tokens and rate limits.** Read the agent's own logs as `claude.rs` and `codex.rs` do (`tail::Position` and `tail::Head` do the reading on) and hand the hub `UsageRecord`s and `Observed` readings, or rate limits with `Hub::note_rate_limits`. Map its OpenTelemetry events in `adapters::usage_from_otel` when they carry a request id.
4. **Dashboard.** Name it in `web/src/agents.ts`; agents not listed there appear under their own name.

## Display device

The ESP32 display (1.75" AMOLED) is the next phase. The data it needs is already served:

- `GET /v1/summary` returns a compact summary: the overall `mood`, counts per status, the first eight sessions in the Live view's order (waiting for you, longest wait first; working, newest turn first; idle, latest to finish first; no signal last) and each agent's rate limits. The WebSocket sends a `summary` message only when the summary changes.
- `mood` is a single signal for the animation: `attention` (someone waits for you) > `working` > `error` (a turn failed in the last 5 min) > `idle` > `offline`.
- For access over the LAN, set `http.listen = "0.0.0.0:7788"` and `http.token`.
