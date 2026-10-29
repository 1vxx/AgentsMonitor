# AgentsMonitor

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Rust 1.95+](https://img.shields.io/badge/rust-1.95%2B-orange.svg)
![Platform: macOS | Linux](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)

English | [简体中文](README.zh-CN.md)

A local monitor for coding-agent sessions. It tracks every Claude Code and Codex session on the machine and shows them on a web dashboard; a small AMOLED display will show them in a later phase.

## Features

- **Live status** of every session: thinking, running a tool (and which), waiting for your approval or answer, compacting, idle
- **Exact token counts** for every model request, read from the agents' own logs, with context fill and the account's five-hour and weekly limits
- **Web dashboard**: live session cards, a timeline per session, history, the event stream and usage charts
- **Out of the way**: hooks only queue events and return; the daemon uses about 7 MB of memory
- **Local and bounded**: nothing leaves the machine, and every kind of data has a retention limit

Claude Code and Codex are supported. Other agents each need only an adapter and an integration (see [Adding an agent](docs/ARCHITECTURE.md#adding-an-agent)).

```
Claude Code ──(plugin, in-process hooks)──┐
Claude Code ──(transcripts, usage limits)──┤   read by the daemon
Claude Code ──(OpenTelemetry, optional)────┤   Unix socket / 127.0.0.1:7788
Codex ──(plugin, agentsmon hook)───────────┤
Codex ──(rollout logs, read by the daemon)─┤
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
| `plugins/codex` | Codex plugin (command hooks that run `agentsmon hook`) |
| `web` | Web dashboard (React, Vite), started only when needed |
| `docs` | [Architecture](docs/ARCHITECTURE.md), [Protocol and API](docs/PROTOCOL.md) |

## Requirements

- Rust 1.95 or newer
- Node.js 20.19+ or 22.12+, and pnpm, for the dashboard
- Claude Code with hooks-module support (tested with 2.1.308)
- Codex with plugins and lifecycle hooks (tested with 0.160.1), for Codex sessions
- macOS for `agentsmon service` (launchd) and for Claude Desktop archive tracking; the daemon itself also runs on Linux

## Quick start

Get the code; the commands below run from its folder:

```bash
git clone https://github.com/1vxx/AgentsMonitor.git
```

```bash
cd AgentsMonitor
```

### 1. Install and run the daemon

```bash
cargo install --locked --path crates/agentsmon
```

This puts `agentsmon` in `~/.cargo/bin`.

> [!NOTE]
> rustup's own installer adds `~/.cargo/bin` to `PATH`. Homebrew's `rustup` does not (nor its own `bin`, where `cargo` is: see `brew info rustup`), so with it add the folder yourself:
>
> ```bash
> echo 'export PATH="$HOME/.cargo/bin:$PATH"' >> ~/.zshrc
> ```
>
> Then load it in the open terminal (new terminals read it on their own):
>
> ```bash
> source ~/.zshrc
> ```

```bash
agentsmon serve
```

`agentsmon serve` runs in the foreground. Ctrl+C, SIGTERM and closing the terminal (SIGHUP) all shut it down cleanly: it removes its socket and commits pending writes first. To start it at login instead:

```bash
agentsmon service install
```

To run it without installing anything, see [Running from source](#running-from-source).

### 2. Load the plugins

This repository is also a plugin marketplace, for Claude Code and for Codex.

#### Claude Code

Install the plugin:

```bash
claude plugin marketplace add "$PWD"
```

```bash
claude plugin install agentsmon@agentsmon
```

Installed this way it loads in every session, Claude Desktop ones included, and can be switched off and on again like any other plugin: in `/plugin`, or with `claude plugin disable agentsmon@agentsmon` and `claude plugin enable agentsmon@agentsmon`. The change applies to sessions started afterwards. A local marketplace is read straight from this folder, so edits to the plugin need no reinstall.

To try it without installing, load it for a single session with `claude --plugin-dir "$PWD/plugins/claude-code"`. Avoid loading it through `CLAUDE_CODE_PLUGIN_DIRS` as well as installing it: each session would then report everything twice.

Plugin options:

| Option | Default | Meaning |
| --- | --- | --- |
| `detail` | `preview` | `preview` sends the start of prompts and answers and what each tool works on; `none` sends statuses and numbers only |
| `socket` | `$AGENTSMON_SOCKET` or `~/.agentsmon/agentsmon.sock` | The daemon's Unix socket |

#### Codex

Install it from the same marketplace:

```bash
codex plugin marketplace add "$PWD"
```

```bash
codex plugin add agentsmon@agentsmon
```

Restart Codex, then trust the plugin's hooks with `/hooks` in the CLI or the hook review controls in the Codex app; Codex skips hooks that are not trusted. The hooks run `agentsmon`, so it must be in `~/.cargo/bin` (where `cargo install` puts it), `/opt/homebrew/bin`, `/usr/local/bin` or on Codex's `PATH`. See [plugins/codex](plugins/codex/README.md).

### 3. Open the dashboard

```bash
pnpm --dir web install
```

```bash
pnpm --dir web dev
```

Then open http://127.0.0.1:5173. The dashboard reaches the daemon through the Vite proxy: set `AGENTSMON_URL` to use another daemon, and `AGENTSMON_TOKEN` if it requires a token. `pnpm --dir web start` serves a production build instead.

## Running from source

To run the daemon from this folder without installing anything, each run building the code as it is:

```bash
cargo run --release -p agentsmon -- serve
```

The Claude Code plugin talks to the daemon's socket and needs nothing more. Codex's hooks run the `agentsmon` binary itself (see [Codex](#codex)), so link the build where they look, once it has been built:

```bash
ln -s "$PWD/target/release/agentsmon" ~/.cargo/bin/agentsmon
```

## Updating

`cargo install` copies the build, so after changing or pulling the code install it again:

```bash
git pull
```

```bash
cargo install --locked --path crates/agentsmon
```

A running daemon keeps the old binary until it restarts. For the login service, `agentsmon service install` restarts it on the new one:

```bash
agentsmon service install
```

The Claude Code plugin is read from this folder and the dashboard reloads on its own under `pnpm --dir web dev`, so neither needs anything more.

## Uninstalling

Remove the service first, while `agentsmon` is still installed:

```bash
agentsmon service uninstall
```

```bash
cargo uninstall agentsmon
```

```bash
claude plugin uninstall agentsmon@agentsmon
```

```bash
codex plugin remove agentsmon@agentsmon
```

`claude plugin marketplace remove agentsmon` and `codex plugin marketplace remove agentsmon` forget the marketplace too. The data stays in `~/.agentsmon`; delete that folder to remove it.

## Token counts and usage limits

Nothing to set up. The daemon reads every model request's token counts from the agents' own files, including requests made while it was down:

- **Claude Code**: its transcripts (`~/.claude/projects`, or `$CLAUDE_CONFIG_DIR/projects`), where every reply of the main session and of its subagents is written with its request id and token counts. The account's five-hour and weekly limits come from the reading Claude Code keeps in `~/.claude.json`. Claude Code fetches that reading only for `/usage`, so while it is at work and no reading is newer than 10 min, the daemon runs `claude -p /usage`, which asks no model and keeps no session. The plugin reports fresher limits after every turn; with it installed, the daemon has nothing to fetch. `[claude] usage_refresh_minutes` in the config file changes the interval, and `0` turns it off.
- **Codex**: its session logs (`~/.codex/sessions`, or `$CODEX_HOME/sessions`), which also give the context fill and the rate limits.

Some of Claude Code's requests write nothing to the transcript: context compaction, and the small model's web search, web fetch and session titles. To count those as well, have Claude Code export its telemetry to the daemon, in the `env` block of `~/.claude/settings.json`:

```json
"CLAUDE_CODE_ENABLE_TELEMETRY": "1",
"OTEL_LOGS_EXPORTER": "otlp",
"OTEL_EXPORTER_OTLP_LOGS_PROTOCOL": "http/json",
"OTEL_EXPORTER_OTLP_LOGS_ENDPOINT": "http://127.0.0.1:7788/v1/logs",
"OTEL_LOGS_EXPORT_INTERVAL": "2000"
```

The exporter sends only to the local daemon, and applies to sessions started after the change (quit and reopen Claude Desktop). A request both sources report counts once, by its request id. Where a session's transcript cannot be read, its token figures are estimates summed from turn ends, marked `≈`.

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
| Live | Overall state, session counts, today's tokens, rate-limit meters; session cards in bands (waiting for you, working, idle, no signal): the longest wait first, then the newest turn, the latest to finish; the cards hold still while the pointer is over them; recently ended sessions; live event feed |
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

The Claude Code plugin sends a heartbeat every 20 s: a Claude Code session silent for 65 s is marked `stale`, and after 5 min it ends as `lost`. Codex's command hooks cannot send heartbeats, so a Codex session is never marked `stale` for silence and ends as `lost` after 12 h. After the machine wakes from sleep, sessions get 75 s to report in before silence counts.

The live view is for the sessions in use. A session still open but idle for 2 h (no prompt, tool or other work; heartbeats do not count) leaves it, and an ended one leaves it after 30 min. Both stay in History, and an idle one comes back with its statistics the moment it is used again. Both periods are under `[sessions]` in the config file.

## Data retention

Storage stays bounded. Deleted space returns to the file system right away (incremental vacuum, WAL truncation).

| Data | Kept |
| --- | --- |
| Ended sessions | 7 days after they end |
| Sessions deleted in Claude Code (in the app, or by its `cleanupPeriodDays` cleanup) | Removed within 15 s, running or ended |
| Sessions archived in Claude Desktop | Removed from the live view within 15 s; kept in History until retention ends. Unarchived and resumed, they return with their statistics |
| Codex sessions deleted or archived in Codex | The same, read from Codex's thread records (`~/.codex/state_<n>.sqlite`) |
| Event history | 3 days |
| Rate-limit readings | 30 days |
| Token counts | Per request for 90 days, then per hour until 400 days, then per day until 5 years. Totals stay exact at every step |

Removing a session never removes its token counts, and neither does Claude Code deleting its transcript: what was counted stays in the ledger.

The daemon checks for deletions and archiving every 15 s and applies retention at start and every 10 min. The Daemon view can run a pass on demand, or remove every ended session at once; token totals are never reduced. All periods can be changed under `[retention]` in the config file.

Archive state is read from Claude Desktop's own session records (`~/Library/Application Support/Claude/claude-code-sessions/`). That format is not public: the daemon only reads it, and if it cannot, archived sessions simply follow the retention period. Claude Code's transcripts and `~/.claude.json`, and Codex's thread records and session logs, are not public either and are handled the same way.

## Security and privacy

- Integrations write to an owner-only Unix socket. The HTTP listener binds to `127.0.0.1` by default.
- Binding to any other address requires `http.token`; the daemon refuses to start without one.
- Requests from browsers are accepted only from loopback origins and `http.allowed_origins`, so other web pages cannot read sessions or inject events.
- With `detail = none`, the plugin sends no prompt, answer or command text.
- The Codex plugin forwards each hook payload whole over the socket; the daemon keeps only the start of prompts and answers and what each tool works on.
- The daemon reads the agents' transcripts and logs only for token counts, titles and limits, and parses only the lines that hold them. Of `~/.claude.json` it keeps only the usage-limit reading.
- Telemetry, when turned on, goes only to the local daemon, which keeps the per-request token counts and discards every other record.

## Performance

The monitor must never slow an agent down:

- Plugin hooks only queue an event and return. A timer sends events in ordered batches after the hook completes; one round trip over the socket takes about 70 µs. While the daemon is down, events wait in a bounded queue (1,000) and go out in order once it is back.
- The daemon runs on one thread. Each event is a microsecond-scale reducer step. Every change is serialized once and shared by all WebSocket clients, and SQLite writes are batched on a separate thread. Measured: 7.4 MB resident memory and 0.27 s total CPU after 2,300 batches.
- Memory is bounded: sessions in the live view (running ones not idle for 2 h, and those ended in the last 30 min), and at most 50,000 request ids (6 h) for deduplication.
- `agentsmon hook`, which the Codex plugin and other command-hook agents use, forwards stdin without parsing it or starting an async runtime: about 5 ms per event, mostly process start-up.
- Claude Code's transcripts and Codex's session logs are read every 2 s, only what each file gained, and only lines that name a reply, a token or a session record are parsed. Files not written to for a day are looked at once a minute, and a folder is listed again only when it changed. Files are read through a 64 KB buffer, so memory does not grow with them. Measured with 730 transcripts (845 MB): the first run counts their 12,000 requests in 0.9 s of CPU with a 26 MB peak; after that, reading them costs about 20 ms of CPU a minute.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| `agentsmon: command not found` after `cargo install` | `~/.cargo/bin` is not on `PATH`: see the note under [Install and run the daemon](#1-install-and-run-the-daemon) |
| The dashboard says "Daemon not connected" | Start the daemon (`agentsmon serve`, or `agentsmon service install`); the page reconnects on its own |
| Claude Code sessions do not appear | The plugin applies to sessions started after it was installed; check it with `claude plugin list` |
| Codex sessions do not appear | Trust the plugin's hooks with `/hooks`, and make sure `agentsmon` is where they look (see [Codex](#codex)) |
| Every session shows "No signal" after the daemon restarts | Expected: each one returns when it next reports, a Claude Code session within 20 s |
| Anything else | Run `AGENTSMON_LOG=debug agentsmon serve`; the login service logs to `~/.agentsmon/agentsmon.log` |

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

Issues and pull requests are welcome. Please run the checks above before opening a pull request.

## Roadmap

- [x] Daemon, protocol, storage, REST and WebSocket API
- [x] Claude Code plugin: status, tools, subagents, approvals and questions, compaction, context, rate limits
- [x] Exact token accounting from Claude Code's transcripts, and optionally its per-request telemetry
- [x] Web dashboard
- [x] Bounded retention, following deletion and archiving in Claude
- [x] Codex adapter and plugin; tokens, context and rate limits from its session logs
- [ ] ESP32 display with a 1.75" AMOLED screen, driven by `/v1/summary`

## License

[MIT](LICENSE)

AgentsMonitor is an independent project, not affiliated with or endorsed by Anthropic or OpenAI. Claude, Claude Code and Codex are trademarks of their respective owners.
