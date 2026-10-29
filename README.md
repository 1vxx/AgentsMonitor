<p align="center">
  <img src="docs/assets/banner.svg" alt="AgentsMonitor: every coding agent, at a glance" width="100%">
</p>

<p align="center">
  <b>Know what every coding agent is doing, without switching windows.</b><br>
  A tiny local daemon that watches your Claude Code and Codex sessions, and shows them<br>
  in your browser, in your terminal, or on a little screen on your desk.
</p>

<p align="center">
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-7c6cf0?style=flat-square"></a>
  <img alt="Rust 1.95+" src="https://img.shields.io/badge/rust-1.95%2B-e9774f?style=flat-square&logo=rust&logoColor=white">
  <img alt="Platform: macOS | Linux" src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-5a6bb8?style=flat-square">
  <img alt="Works with Claude Code and Codex" src="https://img.shields.io/badge/works%20with-Claude%20Code%20%C2%B7%20Codex-f0aa2e?style=flat-square">
</p>

<p align="center"><b>English</b> · <a href="README.zh-CN.md">简体中文</a></p>

---

## ✨ Why

You have three agents running. One has been waiting for your approval for ten minutes, one finished a while ago, and one hit an error you haven't seen. AgentsMonitor watches every session on your machine, so you can tell at a glance who is thinking, who is running a tool, who needs you and who is done.

- 🛰️ **Live status.** See whether each session is thinking, running a tool (and which one), waiting for your approval or answer, compacting, or idle.
- 🔢 **Exact token counts.** Every model request is counted from the agents' own logs, along with context fill and your five-hour and weekly limits.
- 🖥️ **Web dashboard.** Live session cards, a timeline for each session, history, the event stream and usage charts.
- 📟 **A screen on your desk.** Optional. Firmware for a round AMOLED board is included, and [porting it to your own hardware](devices/README.md#-make-it-yours) is part of the design.
- 🪶 **Featherweight.** Hooks never make your agent wait, and the daemon uses about 7 MB of memory.
- 🔒 **Local only.** Nothing leaves your machine, and every kind of data has a retention limit.

## 🔭 A look

<p align="center"><img src="docs/assets/dashboard-live.jpg" width="100%" alt="The dashboard's Live view: two sessions wait for you, two are working, usage limits for Claude and Codex"></p>

<table>
  <tr>
    <td align="center"><img src="docs/assets/halo-home.png" width="190" alt="halo: the mood orb with sessions orbiting it"><br><sub><b>halo</b>: the mood orb</sub></td>
    <td align="center"><img src="docs/assets/halo-approve.png" width="190" alt="halo: a session asks to run a command"><br><sub>who needs you, and for what</sub></td>
    <td align="center"><img src="docs/assets/pets.png" width="190" alt="pets: six pixel animals, one per session"><br><sub><b>pets</b>: one animal per session</sub></td>
    <td align="center"><img src="docs/assets/halo-list.png" width="190" alt="halo: every session in a list"><br><sub>every session at a glance</sub></td>
  </tr>
</table>

<sub>Every session here is made up: the dashboard ran on <a href="docs/USAGE.md#demo-mode">demo mode</a>, and the round screens come from the real display code. Try the display in your browser, no hardware needed: <a href="devices/README.md#-try-it-in-your-browser">Try it in your browser</a>.</sub>

## 🚀 Quick start

**You need** macOS or Linux, [Rust](https://rustup.rs) 1.95+, and Claude Code and/or Codex. For the web dashboard you also need Node.js 20.19+ and [pnpm](https://pnpm.io).

**1 · Install the daemon**

```bash
git clone https://github.com/1vxx/AgentsMonitor.git && cd AgentsMonitor
cargo install --locked --path crates/agentsmon
agentsmon service install    # macOS: runs it at login. On Linux, run `agentsmon serve`
```

**2 · Connect your agents.** This repository is also a plugin marketplace. Run the lines for the agents you use:

```bash
# Claude Code
claude plugin marketplace add "$PWD" && claude plugin install agentsmon@agentsmon

# Codex: afterwards, restart Codex and trust the plugin's hooks with /hooks
codex plugin marketplace add "$PWD" && codex plugin add agentsmon@agentsmon
```

**3 · Watch.** Start a new agent session and it appears within a second 🎉

```bash
agentsmon status                                 # in the terminal
pnpm --dir web install && pnpm --dir web dev     # dashboard at http://127.0.0.1:5173
```

> [!TIP]
> If you get `agentsmon: command not found`, `~/.cargo/bin` is not on your `PATH` yet: [here's the fix](docs/USAGE.md#agentsmon-is-not-found-after-cargo-install). Want to look around before connecting an agent? `python3 scripts/demo.py` runs it all on [made-up sessions](docs/USAGE.md#demo-mode). Running from source, updating, uninstalling and configuration are all in the [usage guide](docs/USAGE.md).

## 📟 Put it on your desk

<img align="right" src="docs/assets/pets-approve.png" width="170" alt="pets: a cat asks to approve a command">

A screen beside your keyboard that lights up when an agent needs you, chimes when a turn finishes, and lets you swipe through every session.

- **Optional.** Everything above works without it.
- **Not tied to one board.** The firmware targets the [Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm) only because that is the board the author has. The display code is split into a portable core, swappable themes and a thin layer per board, so another ESP32 with another screen needs only its own drivers.
- **Bring anything.** A Raspberry Pi, an e-ink panel, an M5Stack, an old phone: anything that can make an HTTP request can be a display. It reads one small JSON document.
- **Pick a look, or make one.** Themes change what the screen draws and how it moves: the glowing `halo`, the pixel `pets`, or [your own](devices/themes/README.md), started from a template in five minutes.

👉 The [devices guide](devices/README.md) shows how to try it in your browser, flash the reference board, or port it to yours.

<br clear="right">

## 🧩 How it works

```
 Claude Code ── plugin (in-process hooks) ──┐
 Claude Code ── transcripts, usage limits ──┤
 Codex ──────── plugin (agentsmon hook) ────┤   Unix socket / 127.0.0.1:7788
 Codex ──────── rollout logs ───────────────┤
                                            ▼
                        ┌──────────────────────────────────────┐
                        │  agentsmon daemon (Rust, ~7 MB)      │
                        │  reducer → sessions, quotas, tokens  │
                        │  SQLite (WAL), bounded retention     │
                        └───────────────────┬──────────────────┘
                                            │  REST + WebSocket
               ┌────────────────────┬───────┴────────────┬──────────────────────┐
               ▼                    ▼                    ▼                      ▼
         Web dashboard       agentsmon status     ESP32 display        your own device
```

Each agent reports what it does through a plugin, and the daemon reads the agents' own logs for exact token counts. One small daemon turns all of it into live state and serves it to whatever wants to show it. Other agents need only an adapter and an integration: see [Adding an agent](docs/ARCHITECTURE.md#adding-an-agent).

## 📚 Documentation

| Guide | What's inside |
| --- | --- |
| 🧭 [Usage guide](docs/USAGE.md) | CLI, configuration, token counting, session states, retention, privacy, troubleshooting |
| 📟 [Devices](devices/README.md) | Review page, flashing the reference board, porting to your own hardware |
| 🎨 [Themes](devices/themes/README.md) | Making a new look for the display: the interface, the toolbox, the checks |
| 🏗️ [Architecture](docs/ARCHITECTURE.md) | How the daemon works inside, and how to add another agent |
| 🔌 [Protocol and API](docs/PROTOCOL.md) | Events, REST routes and WebSocket messages, for building your own client |
| 🧩 Plugins | [Claude Code](plugins/claude-code/README.md) · [Codex](plugins/codex/README.md) |

## 🗂️ Repository

| Path | Contents |
| --- | --- |
| `crates/agentsmon` | The daemon, hook forwarder and CLI, in one binary |
| `crates/agentsmon-protocol` | Wire types: events, sessions, quotas, summaries, WebSocket messages |
| `plugins/` | The Claude Code plugin (a hooks module) and the Codex plugin (command hooks) |
| `web/` | The web dashboard (React, Vite), started only when you want it |
| `devices/` | Display firmware: shared core, themes, boards, and the browser review page |
| `scripts/` | `demo.py`, the daemon on made-up sessions |
| `docs/` | Guides and reference |

## 🗺️ Roadmap

- [x] Daemon, protocol, storage, REST and WebSocket API
- [x] Claude Code plugin: status, tools, subagents, approvals and questions, compaction, context, rate limits
- [x] Exact token accounting from the agents' own logs, optionally with Claude Code's per-request telemetry
- [x] Web dashboard
- [x] Bounded retention that follows deletion and archiving in Claude Code and Codex
- [x] Codex adapter and plugin
- [x] Desk display: the reference board, the browser review page, and themes on a shared host (halo, pets, and a starter)
- [ ] More themes ([make one](devices/themes/README.md))
- [ ] More boards and screen sizes ([help wanted](CONTRIBUTING.md))
- [ ] More agents ([help wanted](docs/ARCHITECTURE.md#adding-an-agent))

## 🤝 Contributing

Issues and pull requests are welcome, especially new themes, boards and agent adapters. [CONTRIBUTING.md](CONTRIBUTING.md) explains how to build, test and send a change. Built a display of your own? [Show it off](https://github.com/1vxx/AgentsMonitor/issues/new?template=show_your_build.yml) 🐾

## 📄 License

[MIT](LICENSE). AgentsMonitor is an independent project, not affiliated with or endorsed by Anthropic or OpenAI. Claude, Claude Code and Codex are trademarks of their respective owners.
