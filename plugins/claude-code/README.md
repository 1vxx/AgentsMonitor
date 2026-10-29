# agentsmon for Claude Code

Reports what a Claude Code session is doing to the local [AgentsMonitor](../../README.md) daemon: turns, tool calls, subagents, approvals and questions, compaction, context fill and the account's five-hour and seven-day rate limits.

## How it works

- **In process.** The plugin is a Claude Code hooks module, so no process starts per event.
- **Never blocking.** Each hook only queues an event and returns. A timer sends ordered batches over the daemon's Unix socket after the hook completes.
- **Resilient.** While the daemon is down, up to 1,000 events wait and are sent in order once it is back. A heartbeat every 20 s keeps the session marked live and corrects work whose end event was lost.
- **No phantom sessions.** Terminal sessions appear at launch. Claude Desktop and SDK sessions appear on first use, so the spare session Claude Desktop keeps warm stays out of the monitor.

Token counts don't depend on the plugin: the daemon reads every request's exact counts from Claude Code's transcripts. The usage the plugin reports at each turn's end is only an estimate to go on until the first request is read, or where a transcript can't be read. See [Token counts and usage limits](../../docs/USAGE.md#token-counts-and-usage-limits).

## Setup

Start the daemon (`agentsmon serve`), then install the plugin from the repository's marketplace:

```bash
claude plugin marketplace add /path/to/AgentsMonitor
```

```bash
claude plugin install agentsmon@agentsmon
```

To switch it off and on again, use `/plugin`, or `claude plugin disable agentsmon@agentsmon` and `claude plugin enable agentsmon@agentsmon`; sessions started afterwards follow.

To try it without installing, start a session with `claude --plugin-dir /path/to/AgentsMonitor/plugins/claude-code`.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `detail` | `preview` | `preview` sends the start of prompts and answers and what each tool works on; `none` sends statuses and numbers only |
| `socket` | empty | The daemon's Unix socket; empty uses `$AGENTSMON_SOCKET`, then `~/.agentsmon/agentsmon.sock` |

## Development

```bash
claude plugin validate plugins/claude-code
```

```bash
claude plugin test plugins/claude-code
```
