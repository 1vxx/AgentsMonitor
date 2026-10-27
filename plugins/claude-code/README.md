# agentsmon for Claude Code

Streams what this Claude Code session is doing to the local agentsmon daemon: turns, tool calls, subagents, permission prompts and questions, compaction, context fill, cost, and the account's 5-hour and 7-day rate limits. The daemon feeds the AgentsMonitor web dashboard and, later, a small AMOLED display.

It runs as a hooks module inside Claude Code, so no process starts per event. Each hook only queues the event and returns; a timer sends batches over the daemon's Unix socket after the hook is done, so the session never waits on the monitor. While the daemon is down, events wait in a bounded queue and go out in order once it is back.

## Setup

Run the daemon (`agentsmon serve`, or `agentsmon service install` on macOS), then install the plugin from this repository's marketplace:

```bash
claude plugin marketplace add /path/to/AgentsMonitor
```

```bash
claude plugin install agentsmon@agentsmon
```

## Options

- `detail`: `preview` (default) sends the start of prompts and answers and what each tool works on; `none` sends statuses and numbers only.
- `socket`: the daemon's Unix socket; empty uses `$AGENTSMON_SOCKET` or `~/.agentsmon/agentsmon.sock`.

## Development

```bash
claude plugin validate plugins/claude-code
```

```bash
claude plugin test plugins/claude-code
```
