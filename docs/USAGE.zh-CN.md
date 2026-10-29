# 🧭 使用指南

[快速开始](../README.zh-CN.md#-快速开始)之后的一切：日常命令、配置、数字是怎么统计的，以及哪里不对劲时该怎么办。

[English](USAGE.md) | 简体中文

**目录**

- [安装细节](#安装细节) · [从源码运行](#从源码运行) · [演示模式](#演示模式) · [更新](#更新) · [卸载](#卸载)
- [命令行](#命令行) · [配置](#配置) · [插件选项](#插件选项)
- [仪表盘](#仪表盘) · [会话状态](#会话状态) · [Token 数和用量限制](#token-数和用量限制)
- [数据保留](#数据保留) · [安全与隐私](#安全与隐私) · [性能](#性能)
- [故障排查](#故障排查)

---

## 安装细节

### 环境要求

- Rust 1.95 或更新版本
- Node.js 20.19+ 或 22.12+，以及 pnpm，仪表盘需要
- 支持钩子模块的 Claude Code（已在 2.1.308 上测试）
- 支持插件和生命周期钩子的 Codex（已在 0.160.1 上测试），监控 Codex 会话时需要
- `agentsmon service`（launchd）和跟随 Claude Desktop 归档需要 macOS；守护进程本身也能在 Linux 上运行

### `cargo install` 之后找不到 `agentsmon`

`cargo install` 会把 `agentsmon` 装到 `~/.cargo/bin`。rustup 官方安装器会把这个目录加进 `PATH`；Homebrew 装的 `rustup` 不会（它自己的 `bin` 目录，也就是 `cargo` 所在的位置，同样不在 `PATH` 里，见 `brew info rustup`），这时需要自己加上：

```bash
echo 'export PATH="$HOME/.cargo/bin:$PATH"' >> ~/.zshrc && source ~/.zshrc
```

### 前台运行，还是登录自启

`agentsmon serve` 在前台运行。按 Ctrl+C、收到 SIGTERM 或关闭终端（SIGHUP）都会让它正常退出：先删除 socket，再写完待写入的数据。

`agentsmon service install`（macOS）改为用 launchd 在登录时启动它，日志写到 `~/.agentsmon/agentsmon.log`。在 Linux 上，用你平时用的服务管理器运行 `agentsmon serve` 即可（例如 systemd 用户单元）。

### Claude Code 插件

从插件市场安装后，它会在每个会话里加载，包括 Claude Desktop 中的会话；也可以像其他插件一样停用和重新启用：在 `/plugin` 里操作，或者运行 `claude plugin disable agentsmon@agentsmon` 和 `claude plugin enable agentsmon@agentsmon`。改动对之后新开的会话生效。本地插件市场直接读取克隆下来的目录，所以修改插件后不用重装。

想先试用而不安装，可以用 `claude --plugin-dir "$PWD/plugins/claude-code"` 只在单个会话里加载。不要在安装插件的同时又通过 `CLAUDE_CODE_PLUGIN_DIRS` 加载它，否则每个会话的所有数据都会上报两次。

### Codex 插件

`codex plugin add` 之后，重启 Codex，然后在 CLI 里用 `/hooks`，或在 Codex 应用的钩子审核界面里信任这个插件的钩子；Codex 会跳过未被信任的钩子。这些钩子会运行 `agentsmon` 程序，所以它必须在 `~/.cargo/bin`、`/opt/homebrew/bin`、`/usr/local/bin` 中，或者在 Codex 的 `PATH` 里（也可以设置 `AGENTSMON_BIN`）。详见 [plugins/codex](../plugins/codex/README.md)（英文）。

### 仪表盘

`pnpm --dir web dev` 在 http://127.0.0.1:5173 提供仪表盘，改代码后自动刷新；`pnpm --dir web start` 则会构建并运行生产版本。仪表盘通过 Vite 代理访问守护进程：设置 `AGENTSMON_URL` 可以连接其他守护进程，如果对方要求令牌，再设置 `AGENTSMON_TOKEN`（写在环境变量里，或写进 `web/.env.local`）。

## 从源码运行

如果不想安装任何东西，可以直接在克隆的目录里运行守护进程，每次都按当前代码编译：

```bash
cargo run --release -p agentsmon -- serve
```

Claude Code 插件通过 socket 连接守护进程，不需要其他设置。Codex 的钩子要直接运行 `agentsmon` 程序，所以在编译过一次之后，把编译结果链接到钩子会查找的位置：

```bash
ln -s "$PWD/target/release/agentsmon" ~/.cargo/bin/agentsmon
```

## 演示模式

想在连上任何代理之前先看看，或者截图时不想露出任何人的真实工作，可以让守护进程跑在虚构的会话上：

```bash
python3 scripts/demo.py
```

它会在 7790 端口另起一个守护进程，数据放在一个用完即删的临时目录里，Claude Code 和 Codex 的目录也都指向空目录，所以你的真实会话绝不会出现。它会写入三周的 token 历史，然后演出几个会话，直到你按 Ctrl+C。用 `AGENTSMON_URL=http://127.0.0.1:7790 pnpm --dir web dev` 让仪表盘连上它，显示设备的预览页则连 `ws://127.0.0.1:7790/v1/ws`。在 Claude Desktop 中，`.claude/launch.json` 里的 `demo` 和 `dashboard-demo` 配置会启动这两者。

## 更新

`cargo install` 复制的是编译好的程序，所以拉取代码后要重新安装。如果用的是登录自启的服务，`agentsmon service install` 会让它换用新程序重启（手动运行的守护进程要你自己重启才会换）：

```bash
git pull && cargo install --locked --path crates/agentsmon && agentsmon service install
```

Claude Code 插件直接从克隆的目录读取，仪表盘在 `pnpm --dir web dev` 下会自动刷新，这两者都不用额外处理。

## 卸载

先卸载服务（这时 `agentsmon` 还必须在），再卸载程序和插件：

```bash
agentsmon service uninstall
cargo uninstall agentsmon
claude plugin uninstall agentsmon@agentsmon
codex plugin remove agentsmon@agentsmon
```

运行 `claude plugin marketplace remove agentsmon` 和 `codex plugin marketplace remove agentsmon` 可以把插件市场也移除。数据保留在 `~/.agentsmon`，删除这个目录即可清除。

## 命令行

| 命令 | 用途 |
| --- | --- |
| `agentsmon serve [--config <path>]` | 在前台运行守护进程 |
| `agentsmon status [--json]` | 打印活动会话和配额 |
| `agentsmon hook --agent <name>` | 把代理的钩子数据（stdin）转发给守护进程；不输出任何内容，始终以 0 退出 |
| `agentsmon service install \| uninstall \| status` | 管理 macOS 的 launchd 服务（日志：`~/.agentsmon/agentsmon.log`） |
| `agentsmon config` | 打印带注释的示例配置 |

## 配置

守护进程读取 `~/.agentsmon/config.toml`。所有字段都是可选的，`agentsmon config` 会列出全部字段、默认值和说明：

```bash
agentsmon config > ~/.agentsmon/config.toml
```

最常改的几项：

| 字段 | 默认值 | 什么时候改 |
| --- | --- | --- |
| `http.listen` | `127.0.0.1:7788` | 改成 `0.0.0.0:7788`，局域网里的显示设备或其他电脑才能访问守护进程 |
| `http.token` | 空 | 只要 `listen` 不是回环地址，就必须设置 |
| `sessions.hide_idle_minutes` | `120` | 空闲会话在实时页面上停留多久 |
| `retention.*` | 见[数据保留](#数据保留) | 让历史保留得更久或更短 |
| `claude.usage_refresh_minutes` | `10` | 多久可以重新获取一次用量限制；`0` 表示从不 |

| 环境变量 | 作用 |
| --- | --- |
| `AGENTSMON_HOME` | 数据目录（默认 `~/.agentsmon`） |
| `AGENTSMON_SOCKET` | 命令行工具、钩子转发器和插件使用的 socket 路径 |
| `AGENTSMON_LOG` | 日志过滤级别，例如 `debug`（默认 `info`） |

## 插件选项

Claude Code 插件的选项在 `/plugin` 里设置：

| 选项 | 默认值 | 含义 |
| --- | --- | --- |
| `detail` | `preview` | `preview` 会发送提示和回答的开头，以及每个工具处理的对象；`none` 只发送状态和数字 |
| `socket` | `$AGENTSMON_SOCKET` 或 `~/.agentsmon/agentsmon.sock` | 守护进程的 Unix socket |

## 仪表盘

<p align="center"><img src="assets/dashboard-usage.jpg" width="100%" alt="用量页面：30 天的 token，按天、按模型、按来源"></p>

| 页面 | 显示内容 |
| --- | --- |
| 实时 | 总体状态、会话数、今日 token、用量限制仪表；按组排列的会话卡片（等待你：等得最久的在前；工作中：最新开始的回合在前；空闲：最近结束的在前；无信号的排在最后），指针停在卡片上时卡片不会移动；最近结束的会话；实时事件流 |
| 会话详情 | 上下文占用、正在运行的工具和子代理、最近的提示和回答、统计、元数据、完整时间线 |
| 历史 | 所有已保存的会话，可筛选、搜索、分页 |
| 事件 | 全局事件流，可按类型筛选 |
| 用量 | 今日、24 小时、7 天、30 天或全部时间的 token，按类型、模型和来源细分，附按小时或按天的图表；用量限制历史 |
| 守护进程 | 运行时长、内存、CPU、吞吐量、数据库大小、保留策略、手动清理 |

实时页面顶部的圆形指示器，就是 [halo 主题](../devices/README.zh-CN.md)呈现总体状态的样子。

## 会话状态

状态由正在进行的事情推导出来，从不直接设置，所以丢失或重复的事件不会留下自相矛盾的状态。

| 状态 | 含义 |
| --- | --- |
| `waiting_permission` | 有工具调用在等待授权 |
| `waiting_input` | 代理提出了问题（AskUserQuestion、MCP elicitation） |
| `tool` | 有工具正在运行 |
| `thinking` | 一轮对话正在进行，或有后台子代理在工作 |
| `compacting` | 正在压缩上下文 |
| `idle` | 等待下一条提示；`last_turn` 记录上一轮是如何结束的 |
| `ended` | 会话已结束 |

Claude Code 插件每 20 秒发送一次心跳：Claude Code 会话 65 秒没有动静会被标记为 `stale`，5 分钟后以 `lost` 结束。Codex 的命令钩子无法发送心跳，所以 Codex 会话不会因为沉默被标记为 `stale`，沉默 12 小时后以 `lost` 结束。电脑从睡眠中唤醒后，会话有 75 秒时间重新上报，之后的沉默才开始计算。

实时页面只放正在用的会话。还开着但空闲了 2 小时的会话（没有提示、工具或其他工作，心跳不算）会移出实时页面，已结束的会话 30 分钟后移出。两者都保留在历史中；空闲的会话一旦再被使用，就会连同统计一起回来。这两个时长都在配置文件的 `[sessions]` 下。

## Token 数和用量限制

无需设置。守护进程从代理自己的文件里读取每次模型请求的 token 数，包括守护进程没在运行时发生的请求：

- **Claude Code**：读取它的会话记录（`~/.claude/projects`，或 `$CLAUDE_CONFIG_DIR/projects`），主会话及其子代理的每条回复都带有请求 ID 和 token 数。账户的 5 小时和每周限额来自 Claude Code 保存在 `~/.claude.json` 里的读数。Claude Code 只在执行 `/usage` 时才获取这个读数，所以当它在工作、而最新读数已超过 10 分钟时，守护进程会运行 `claude -p /usage`，这条命令不调用模型，也不保留会话。插件在每轮结束后都会上报更新的限额，装了插件，守护进程就不用自己获取。
- **Codex**：读取它的会话日志（`~/.codex/sessions`，或 `$CODEX_HOME/sessions`），其中也包含上下文占用和用量限制。

### 可选：会话记录漏掉的请求

Claude Code 有些请求不会写进会话记录：上下文压缩，以及小模型执行的网页搜索、网页抓取和会话标题生成。如果也想统计这些请求，可以让 Claude Code 把遥测数据导出给守护进程，写在 `~/.claude/settings.json` 的 `env` 里：

```json
"CLAUDE_CODE_ENABLE_TELEMETRY": "1",
"OTEL_LOGS_EXPORTER": "otlp",
"OTEL_EXPORTER_OTLP_LOGS_PROTOCOL": "http/json",
"OTEL_EXPORTER_OTLP_LOGS_ENDPOINT": "http://127.0.0.1:7788/v1/logs",
"OTEL_LOGS_EXPORT_INTERVAL": "2000"
```

导出器只发往本地守护进程，对修改之后新开的会话生效（需要退出并重新打开 Claude Desktop）。两个来源都上报的同一个请求，按请求 ID 只计一次。如果某个会话的会话记录无法读取，它的 token 数是根据每轮结束时的数据累加出的估算值，标为 `≈`。

## 数据保留

存储空间有上限。删除的数据所占的空间会立即还给文件系统（增量 vacuum、WAL 截断）。

| 数据 | 保留期限 |
| --- | --- |
| 已结束的会话 | 结束后 7 天 |
| 在 Claude Code 中删除的会话（在应用里删除，或被它的 `cleanupPeriodDays` 清理） | 15 秒内移除，不论是否还在运行 |
| 在 Claude Desktop 中归档的会话 | 15 秒内从实时页面移除，在历史中保留到保留期结束。取消归档并恢复后，会连同统计一起回来 |
| 在 Codex 中删除或归档的会话 | 同上，依据 Codex 的线程记录（`~/.codex/state_<n>.sqlite`） |
| 事件历史 | 3 天 |
| 用量限制读数 | 30 天 |
| Token 数 | 按请求保存 90 天，之后按小时保存到 400 天，再按天保存到 5 年。每个阶段的总数都保持精确 |

移除会话不会移除它的 token 数，Claude Code 删除会话记录也不会：已经统计的数据会一直留在账本里。

守护进程每 15 秒检查一次删除和归档，在启动时和之后每 10 分钟执行一次保留策略。守护进程页面可以手动执行一次清理，或一次性移除所有已结束的会话；token 总数永远不会减少。所有期限都可以在配置文件的 `[retention]` 下修改。

归档状态读取自 Claude Desktop 自己的会话记录（`~/Library/Application Support/Claude/claude-code-sessions/`）。这个格式没有公开：守护进程只读取它，读不了时，归档的会话就只按保留期处理。Claude Code 的会话记录和 `~/.claude.json`，以及 Codex 的线程记录和会话日志，同样没有公开，处理方式相同。

## 安全与隐私

- 各集成写入一个只有所有者能访问的 Unix socket。HTTP 监听默认绑定 `127.0.0.1`。
- 绑定到其他地址时必须设置 `http.token`，否则守护进程拒绝启动。
- 来自浏览器的请求只接受回环地址和 `http.allowed_origins` 中的来源，所以其他网页无法读取会话，也无法注入事件。
- 设为 `detail = none` 时，插件不发送任何提示、回答或命令文本。
- Codex 插件会通过 socket 完整转发每次的钩子数据；守护进程只保留提示和回答的开头，以及每个工具处理的对象。
- 守护进程读取代理的会话记录和日志，只是为了获取 token 数、标题和限额，也只解析包含这些信息的行。对于 `~/.claude.json`，它只保留用量限制读数。
- 遥测开启时只发往本地守护进程，守护进程保留每个请求的 token 数，丢弃其他所有记录。

## 性能

监控器绝不能拖慢代理：

- 插件钩子只把事件放进队列就返回。钩子结束后，由定时器按顺序分批发送事件；通过 socket 往返一次约 70 µs。守护进程没在运行时，事件在一个有上限的队列（1000 条）里等待，等它恢复后按顺序发出。
- 守护进程单线程运行，每个事件只需一步微秒级的状态归约。每次变化只序列化一次，供所有 WebSocket 客户端共享；SQLite 写入在单独的线程上批量进行。实测：处理 2300 批事件后，常驻内存 7.4 MB，CPU 总耗时 0.27 秒。
- 内存有上限：只保存实时页面上的会话（没有空闲超过 2 小时的运行中会话，以及最近 30 分钟内结束的会话），以及最多 50000 个用于去重的请求 ID（6 小时）。
- Codex 插件和其他使用命令钩子的代理会调用 `agentsmon hook`，它直接转发 stdin，不解析内容，也不启动异步运行时：每个事件约 5 ms，大部分是进程启动时间。
- Claude Code 的会话记录和 Codex 的会话日志每 2 秒读取一次，只读每个文件新增的部分，并且只解析标明回复、token 或会话记录的行。一天没被写入的文件每分钟才看一次，目录只在有变化时才重新列出。文件通过 64 KB 缓冲区读取，内存不会随文件变大而增长。实测 730 个会话记录（845 MB）：首次运行统计其中 12000 个请求，用了 0.9 秒 CPU，内存峰值 26 MB；之后每分钟读取约耗 20 ms CPU。

## 故障排查

| 问题 | 处理方法 |
| --- | --- |
| `cargo install` 之后提示 `agentsmon: command not found` | `~/.cargo/bin` 不在 `PATH` 里：见[上文](#cargo-install-之后找不到-agentsmon) |
| `agentsmon status` 连不上守护进程 | 用 `agentsmon serve` 或 `agentsmon service install` 启动它 |
| 仪表盘显示"守护进程未连接" | 同上：启动守护进程，页面会自动重连 |
| Claude Code 会话没有出现 | 插件只对安装之后新开的会话生效；用 `claude plugin list` 检查是否已安装 |
| Codex 会话没有出现 | 用 `/hooks` 信任插件的钩子，并确认 `agentsmon` 在钩子会查找的位置（见 [Codex 插件](#codex-插件)） |
| 守护进程重启后所有会话都显示"无信号" | 正常现象：每个会话下次上报时就会恢复，Claude Code 会话在 20 秒内 |
| 显示设备一直停在"连接中" | 见[设备故障排查](../devices/README.zh-CN.md#故障排查) |
| 其他问题 | 运行 `AGENTSMON_LOG=debug agentsmon serve` 查看详细日志；登录自启的服务日志在 `~/.agentsmon/agentsmon.log`。还是没解决？[提一个 Issue](https://github.com/1vxx/AgentsMonitor/issues/new/choose) |
