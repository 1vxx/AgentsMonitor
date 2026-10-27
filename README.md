# AgentsMonitor

A local monitor for coding-agent sessions. It tracks every Claude Code session on the machine (what it is doing, which tool is running, whether it is waiting for you), along with context usage, exact token counts and account rate limits. A web dashboard shows the data, and a small AMOLED display will show it in a later phase.

Claude Code is supported today. Codex and other agents each need only an adapter and an integration (see [Adding an agent](docs/ARCHITECTURE.md#adding-an-agent)).

```
Claude Code ──(plugin, in-process hooks)──┐
Claude Code ──(OpenTelemetry token counts)─┤   Unix socket / 127.0.0.1:7788
Codex (planned) ──(agentsmon hook)─────────┤
                                           ▼
                        ┌────────────────────────────────────┐
                        │ agentsmon daemon (Rust)            │
                        │ reducer → sessions, quotas, tokens │
                        │ SQLite (WAL), bounded retention    │
                        └─────────────────┬──────────────────┘
                                          │ REST + WebSocket
                  ┌───────────────────────┼───────────────────────┐
                  ▼                       ▼                       ▼
         Web dashboard (on demand)   agentsmon status    ESP32 display (planned)
```

## Repository

| Path | Contents |
| --- | --- |
| `crates/agentsmon` | The daemon, hook forwarder and CLI, in one binary |
| `crates/agentsmon-protocol` | Wire types: events, sessions, quotas, summaries, WebSocket messages |
| `plugins/claude-code` | Claude Code plugin (hooks module, runs inside Claude Code) |
| `web` | Web dashboard (React, Vite), started only when needed |
| `docs` | [Architecture](docs/ARCHITECTURE.md), [Protocol and API](docs/PROTOCOL.md) |

## Requirements

- Rust 1.85 or newer (2024 edition)
- Node.js 20+ and pnpm, for the dashboard
- Claude Code with hooks-module support (tested with 2.1.308)
- macOS for `agentsmon service` (launchd) and for Claude Desktop archive tracking; the daemon itself also runs on Linux

## Quick start

### 1. Run the daemon

```bash
cargo install --path crates/agentsmon
```

```bash
agentsmon serve
```

`agentsmon serve` runs in the foreground. Ctrl+C, SIGTERM and closing the terminal (SIGHUP) all shut it down cleanly: it removes its socket and commits pending writes first. To start it at login instead:

```bash
agentsmon service install
```

### 2. Load the Claude Code plugin

Install it from this repository, which is also a plugin marketplace:

```bash
claude plugin marketplace add /path/to/AgentsMonitor
```

```bash
claude plugin install agentsmon@agentsmon
```

Installed this way it loads in every session, Claude Desktop ones included, and can be switched off and on again like any other plugin: in `/plugin`, or with `claude plugin disable agentsmon@agentsmon` and `claude plugin enable agentsmon@agentsmon`. The change applies to sessions started afterwards. A local marketplace is read straight from this folder, so edits to the plugin need no reinstall.

To try it without installing, load it for a single session with `claude --plugin-dir /path/to/AgentsMonitor/plugins/claude-code`. Avoid loading it through `CLAUDE_CODE_PLUGIN_DIRS` as well as installing it: each session would then report everything twice.

Plugin options:

| Option | Default | Meaning |
| --- | --- | --- |
| `detail` | `preview` | `preview` sends the start of prompts and answers and what each tool works on; `none` sends statuses and numbers only |
| `socket` | `$AGENTSMON_SOCKET` or `~/.agentsmon/agentsmon.sock` | The daemon's Unix socket |

### 3. Enable exact token counts

Token counts come from Claude Code's own per-request telemetry. Add the following to the `env` block of `~/.claude/settings.json`:

```json
"CLAUDE_CODE_ENABLE_TELEMETRY": "1",
"OTEL_LOGS_EXPORTER": "otlp",
"OTEL_EXPORTER_OTLP_LOGS_PROTOCOL": "http/json",
"OTEL_EXPORTER_OTLP_LOGS_ENDPOINT": "http://127.0.0.1:7788/v1/logs",
"OTEL_LOGS_EXPORT_INTERVAL": "2000"
```

The exporter sends only to the local daemon. Counting starts with sessions started after the change; quit and reopen Claude Desktop for it to apply there. Requests made while the daemon is down cannot be counted later, so keep it running (`agentsmon service install`). Without this setting, token figures are estimates summed from turn ends and are marked `≈`.

### 4. Open the dashboard

```bash
pnpm --dir web install
```

```bash
pnpm --dir web dev
```

Then open http://127.0.0.1:5173. The dashboard reaches the daemon through the Vite proxy: set `AGENTSMON_URL` to use another daemon, and `AGENTSMON_TOKEN` if it requires a token. `pnpm --dir web start` serves a production build instead.

## CLI

| Command | Purpose |
| --- | --- |
| `agentsmon serve [--config <path>]` | Run the daemon in the foreground |
| `agentsmon status [--json]` | Print live sessions and quotas |
| `agentsmon hook --agent <name>` | Forward an agent's hook payload (stdin) to the daemon; prints nothing, always exits 0 |
| `agentsmon service install \| uninstall \| status` | Manage the macOS launchd agent (log: `~/.agentsmon/agentsmon.log`) |
| `agentsmon config` | Print an annotated example configuration |

## Configuration

The daemon reads `~/.agentsmon/config.toml`. Every field is optional; `agentsmon config` prints all of them with their defaults.

| Environment variable | Effect |
| --- | --- |
| `AGENTSMON_HOME` | Data directory (default `~/.agentsmon`) |
| `AGENTSMON_SOCKET` | Socket path for the CLI, the hook forwarder and the plugin |
| `AGENTSMON_LOG` | Log filter, for example `debug` (default `info`) |

## Dashboard

| View | Shows |
| --- | --- |
| Live | Overall state, session counts, today's tokens, rate-limit meters; session cards ordered by urgency, with sessions waiting for you highlighted; recently ended sessions; live event feed |
| Session details | Context use, running tools and subagents, last prompt and answer, statistics, metadata, full timeline |
| History | All stored sessions, with filter, search and paging |
| Events | Global event stream, filterable by type |
| Usage | Tokens for today, 24 h, 7 d, 30 d or all time, broken down by type, model and source, with an hourly or daily chart; rate-limit history |
| Daemon | Uptime, memory, CPU, throughput, database size, retention policy, manual cleanup |

The round indicator at the top of the Live view previews how the display device will show the overall state.

## Session states

The status is derived from what is in flight, never set directly, so a lost or repeated event cannot leave a contradictory state.

| Status | Meaning |
| --- | --- |
| `waiting_permission` | A tool call waits for approval |
| `waiting_input` | The agent asked a question (AskUserQuestion, MCP elicitation) |
| `tool` | A tool is running |
| `thinking` | A turn is in progress, or a background subagent is working |
| `compacting` | The context is being compacted |
| `idle` | Waiting for the next prompt; `last_turn` records how the last turn ended |
| `ended` | The session has ended |

A session that sends heartbeats and goes silent for 65 s is marked `stale`; after 30 min it ends as `lost`. After the machine wakes from sleep, sessions get 75 s to report in before silence counts.

## Data retention

Storage stays bounded. Deleted space returns to the file system right away (incremental vacuum, WAL truncation).

| Data | Kept |
| --- | --- |
| Ended sessions and their events | 30 days after they end |
| Sessions deleted in Claude Code (in the app, or by its `cleanupPeriodDays` cleanup) | Removed within 15 s, running or ended |
| Sessions archived in Claude Desktop | Removed from the live view within 15 s; kept in History until retention ends. Unarchived and resumed, they return with their statistics |
| Event history, rate-limit readings | 30 days |
| Token counts | Per request for 90 days, then per hour until 400 days, then per day until 5 years. Totals stay exact at every step |

The daemon checks for deletions and archiving every 15 s and applies retention at start and every 10 min. The Daemon view can run a pass on demand, or remove every ended session at once; token totals are never reduced. All periods can be changed under `[retention]` in the config file.

Archive state is read from Claude Desktop's own session records (`~/Library/Application Support/Claude/claude-code-sessions/`). That format is not public: the daemon only reads it, and if it cannot, archived sessions simply follow the retention period.

## Security and privacy

- Integrations write to an owner-only Unix socket. The HTTP listener binds to `127.0.0.1` by default.
- Binding to any other address requires `http.token`; the daemon refuses to start without one.
- Requests from browsers are accepted only from loopback origins and `http.allowed_origins`, so other web pages cannot read sessions or inject events.
- With `detail = none`, the plugin sends no prompt, answer or command text.
- Telemetry goes only to the local daemon, which keeps the per-request token counts and discards every other record.

## Performance

The monitor must never slow an agent down:

- Plugin hooks only queue an event and return. A timer sends events in ordered batches after the hook completes; one round trip over the socket takes about 70 µs. While the daemon is down, events wait in a bounded queue (1,000) and go out in order once it is back.
- The daemon runs on one thread. Each event is a microsecond-scale reducer step. Every change is serialized once and shared by all WebSocket clients, and SQLite writes are batched on a separate thread. Measured: 7.4 MB resident memory and 0.27 s total CPU after 2,300 batches.
- Memory is bounded: live sessions plus those ended in the last 24 h, and at most 50,000 request ids (6 h) for deduplication.
- `agentsmon hook`, the fallback for command-hook agents, forwards stdin without parsing it or starting an async runtime: about 5 ms per event, mostly process start-up.

## Development

```bash
cargo test
```

```bash
claude plugin test plugins/claude-code
```

```bash
pnpm --dir web typecheck
```

In Claude Desktop, the `dashboard` entry in `.claude/launch.json` previews the dashboard in the built-in browser.

## Roadmap

- [x] Daemon, protocol, storage, REST and WebSocket API
- [x] Claude Code plugin: status, tools, subagents, approvals and questions, compaction, context, rate limits
- [x] Exact token accounting from Claude Code's per-request telemetry
- [x] Web dashboard
- [x] Bounded retention, following deletion and archiving in Claude
- [ ] Codex adapter and integration
- [ ] ESP32 display with a 1.75" AMOLED screen, driven by `/v1/summary`
