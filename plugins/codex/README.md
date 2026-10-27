# agentsmon for Codex

Reports what a Codex session is doing to the local [AgentsMonitor](../../README.md) daemon: turns, tool calls, subagents, approvals and questions, compaction and interrupts. The daemon reads token counts, context fill and the account's five-hour and weekly limits from Codex's own session logs, so those need nothing from the plugin.

## How it works

- **Command hooks.** Codex runs `hooks/forward.sh` for each of its twelve lifecycle events. It hands the payload to `agentsmon hook --agent codex`, which forwards it to the daemon's Unix socket unchanged and answers `{}`. The daemon's adapter maps it (`crates/agentsmon/src/adapters/codex.rs`). One event costs about 5 ms, mostly process start-up.
- **Never in the way.** The hooks never approve, deny or change anything. They run synchronously, so events reach the daemon in order, with a 1 s timeout; the forwarder gives up on the daemon after 300 ms. If `agentsmon` is not installed, the script answers `{}` and does nothing.
- **Exact tokens.** Every model request is counted once, by its `response_id`, from the session's rollout log. Requests made while the daemon was down are counted when it is back.

Codex's hooks have no event for a failed turn, no heartbeat and no idle reminder. A session that crashes stays as it was until 12 h of silence end it as `lost`. Codex itself ends a session left idle for 30 min that no client has open.

## Setup

1. Install the daemon and make sure `agentsmon` is in `~/.cargo/bin`, `/opt/homebrew/bin`, `/usr/local/bin` or on Codex's `PATH`. Set `AGENTSMON_BIN` in Codex's environment to use another path.
2. Add the repository's marketplace and install the plugin:

   ```bash
   codex plugin marketplace add /path/to/AgentsMonitor
   ```

   ```bash
   codex plugin add agentsmon@agentsmon
   ```

3. Restart Codex, then review and trust the plugin's hooks: `/hooks` in the CLI, or the hook review controls in the Codex app. Codex skips hooks that are not trusted, and asks again whenever they change. `features.hooks` must not be `false`.

To remove it:

```bash
codex plugin remove agentsmon@agentsmon
```

Cloud tasks do not run local hooks, so they do not appear.

## Privacy

The forwarder passes each payload to the daemon whole, over the owner-only socket. The daemon keeps the start of each prompt and answer and what each tool works on (a command, a file, a question), the same `preview` detail the Claude Code plugin sends by default, and nothing else from it.
