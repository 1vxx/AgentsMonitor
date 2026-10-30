<p align="center">
  <img src="docs/assets/banner.svg" alt="AgentsMonitor：余光里的编码 Agent" width="100%">
</p>

<p align="center">
  <b>活交给 Agent，你去追剧。</b><br>
  桌上一块小圆屏，替你盯着每一个 Claude Code 和 Codex 会话，<br>
  哪个需要你了，第一时间告诉你。
</p>

<p align="center">
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-7c6cf0?style=flat-square"></a>
  <img alt="Works with Claude Code and Codex" src="https://img.shields.io/badge/works%20with-Claude%20Code%20%C2%B7%20Codex-f0aa2e?style=flat-square">
  <img alt="Display: ESP32-S3 or any screen" src="https://img.shields.io/badge/display-ESP32--S3%20%C2%B7%20any%20screen-e9774f?style=flat-square">
  <img alt="Platform: macOS | Linux" src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-5a6bb8?style=flat-square">
</p>

<p align="center"><a href="README.md">English</a> · <b>简体中文</b></p>

---

## 🎬 只在该抬头的时候抬头

你让 Claude Code 去重构，又让 Codex 跑一套测试，然后去干点别的：追一集剧、刷会儿 X、打一把排位。可总有个念头把你拽回终端：*跑完了没？还是一直在等我点确认？*

AgentsMonitor 用余光回答你。显示器旁边的一块小圆屏，同时显示所有 Agent 的状态：颜色不用细看就能读懂，提示音不用看就能听见。不用切窗口，不用翻终端，也不会回来才发现有个授权请求已经干等了二十分钟。

## 👀 一眼看懂

<table>
  <tr>
    <th></th>
    <th>🟠 工作中</th>
    <th>🟡 在等你</th>
    <th>🟢 完成</th>
    <th>🔴 出错</th>
  </tr>
  <tr>
    <td align="center"><b><code>halo</code></b></td>
    <td align="center"><img src="docs/assets/state-halo-working.png" width="150" alt="halo：橙色光球，会话在思考"></td>
    <td align="center"><img src="docs/assets/state-halo-waiting.png" width="150" alt="halo：琥珀色卡片，请你批准一条命令"></td>
    <td align="center"><img src="docs/assets/state-halo-done.png" width="150" alt="halo：光球变绿，刚刚完成"></td>
    <td align="center"><img src="docs/assets/state-halo-failed.png" width="150" alt="halo：光球变红，出错了"></td>
  </tr>
  <tr>
    <td align="center"><b><code>pets</code></b></td>
    <td align="center"><img src="docs/assets/state-pets-working.png" width="150" alt="pets：狐狸在思考"></td>
    <td align="center"><img src="docs/assets/state-pets-waiting.png" width="150" alt="pets：狐狸请你批准一条命令"></td>
    <td align="center"><img src="docs/assets/state-pets-done.png" width="150" alt="pets：狐狸完成了，旁边一颗小心心"></td>
    <td align="center"><img src="docs/assets/state-pets-failed.png" width="150" alt="pets：兔子头顶冒烟，出错了"></td>
  </tr>
  <tr>
    <td align="center"><b><a href="devices/themes/her/README.zh-CN.md"><code>her</code></a></b></td>
    <td align="center"><img src="docs/assets/state-her-working.png" width="150" alt="her：低着头，正在帮你跑命令"></td>
    <td align="center"><img src="docs/assets/state-her-waiting.png" width="150" alt="her：眼巴巴地问能不能运行台词下方的命令"></td>
    <td align="center"><img src="docs/assets/state-her-done.png" width="150" alt="her：眨眼笑着，等你夸她"></td>
    <td align="center"><img src="docs/assets/state-her-failed.png" width="150" alt="her：垂头丧气，她搞砸了"></td>
  </tr>
  <tr>
    <td align="center"><sub>你会听到</sub></td>
    <td align="center"><sub>什么都没有，接着看</sub></td>
    <td align="center"><sub>一声提示，之后 2 分钟、5 分钟各轻提醒一次，再往后每 10 分钟一次</sub></td>
    <td align="center"><sub>轻轻一声</sub></td>
    <td align="center"><sub>一声低沉的下行音</sub></td>
  </tr>
  <tr>
    <td align="center"><sub>你要做的</sub></td>
    <td align="center"><sub>什么都不用做</sub></td>
    <td align="center"><sub>抬头看一眼：要运行的命令、要问的问题都写在屏上</sub></td>
    <td align="center"><sub>这集看完再去看结果</sub></td>
    <td align="center"><sub>过去看看哪里坏了</sub></td>
  </tr>
</table>

<sub>以上都是真实显示代码配上虚构会话渲染出的画面。三种主题，传达同一件事：挑你喜欢的，或者<a href="devices/themes/README.zh-CN.md">自己做一个</a>。</sub>

## 📟 桌面显示设备

<img align="right" src="docs/assets/pets.png" width="200" alt="pets：六只像素小动物，每个会话一只，各自表演自己的状态">

- **一眼看全所有 Agent**：屏幕呈现的是所有会话合在一起的状态，最需要你的那个永远排第一。
- **它会告诉你要干什么**：授权请求直接显示要运行的命令，提问直接显示问题，不用先摸键盘。
- **滑一滑、点一点**：左右滑切换会话，上滑看列表，点一下看它的 token 和上下文。侧键一按直接跳到最需要你的会话，连按两下看全部。
- **换个样子**：发光的 `halo`、像素风的 `pets`（据说里面藏着一条龙 🐉）、你已读不回她绝对会发现的女朋友 [`her`](devices/themes/her/README.zh-CN.md)，或者用模板五分钟起步做一个你自己的主题。
- **什么屏幕都行**：固件适配的是 [微雪 ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm)，只是因为作者桌上正好是这块。换一块 ESP32 和屏幕只需要移植驱动；树莓派、墨水屏、一部旧手机也都能当显示设备，它只需要读取一份很小的 JSON。
- **还没有板子？** [在浏览器里试试](devices/README.zh-CN.md#-在浏览器里试试)：同一份代码，像素一一对应。

<br clear="right">

## 🚀 快速开始

> [!TIP]
> **这些都可以交给你的 Agent。** 把下面这段贴进 Claude Code 或 Codex，然后去泡杯咖啡：
>
> ```text
> 克隆 https://github.com/1vxx/AgentsMonitor，阅读它的 AGENTS.md，
> 然后按 README 的快速开始帮我装好。
> ```

**需要**：macOS 或 Linux、[Rust](https://rustup.rs) 1.95+，以及 Claude Code 或 Codex（或两者都有）。

**1 · 安装守护进程**。它在后台安静运行，只占约 7 MB 内存。

```bash
git clone https://github.com/1vxx/AgentsMonitor.git && cd AgentsMonitor
cargo install --locked --path crates/agentsmon
agentsmon service install    # macOS：登录时自动运行。Linux 上请运行 `agentsmon serve`
```

**2 · 连接你的 Agent**。这个仓库同时也是一个插件市场，运行你用的 Agent 对应的那一行：

```bash
# Claude Code
claude plugin marketplace add "$PWD" && claude plugin install agentsmon@agentsmon

# Codex：装好后重启 Codex，并用 /hooks 信任插件的钩子
codex plugin marketplace add "$PWD" && codex plugin add agentsmon@agentsmon
```

现在 `agentsmon status` 就能列出你开的每个会话了 🎉

**3 · 摆到桌上**。把守护进程开放到你的 Wi-Fi，告诉开发板你的网络名和守护进程的地址，然后刷写固件。大约十五分钟，每一步都在[设备指南](devices/README.zh-CN.md#-制作参考设备)里。最后一步只有一行：

```bash
THEME=halo devices/boards/waveshare_amoled_175c/build.sh --upload    # 或者 THEME=pets
```

> [!TIP]
> 想先看看效果？`python3 scripts/demo.py` 会用[虚构的会话](docs/USAGE.zh-CN.md#演示模式)把一切跑起来，不需要任何 Agent。如果提示 `agentsmon: command not found`，说明 `~/.cargo/bin` 还不在 `PATH` 里：[看这里解决](docs/USAGE.zh-CN.md#cargo-install-之后找不到-agentsmon)。

## 🤖 剩下的交给你的 Agent

你已经有一位不睡觉的工程师了，而这个仓库也是写给它看的：[AGENTS.md](AGENTS.md) 告诉它这里怎么运作，[菜谱](docs/RECIPES.zh-CN.md)是可以直接粘贴的提示词。没错，你可以让 Agent 亲手安装监视自己的东西。它们好像并不介意。

| 对 Agent 说 | 你会得到 |
| --- | --- |
| *“把这个刷到我的板子上，告诉我连上没有。”* | 桌上的显示设备，串口日志它也替你看过了 |
| *“做个主题：每个会话是一条鱼，等我的那条会敲玻璃。”* | 一个新主题，自己检查过，在预览页等你 |
| *“给 Her 换上这张脸。”* | 一张照片变 25 个表情，你吃完午饭就做好了 |
| *“显示设备一直在连接，查查为什么。”* | 一个答案，来自真正读过日志的人 |

## 📊 想看数字，还有仪表盘

显示设备是用来瞄一眼的。想看细节的时候（今天用了多少 token、离限额还有多远、你不在时都发生了什么），打开 Web 仪表盘：

```bash
pnpm --dir web install && pnpm --dir web dev    # 需要 Node.js 20.19+ 和 pnpm；地址是 http://127.0.0.1:5173
```

<p align="center"><img src="docs/assets/dashboard-live.jpg" width="100%" alt="仪表盘的实时页面：两个会话在等你，两个在工作，以及 Claude 和 Codex 的用量限制"></p>

实时会话卡片、每个会话的时间线、历史、事件流，以及按模型、按天、按来源的 token 图表。详见[使用指南](docs/USAGE.zh-CN.md#仪表盘)。

## ✨ 底层功夫

- 🎯 **精确的 token 数**：从 Agent 自己的日志里统计每一次模型请求，只算一次，旁边就是你的 5 小时和每周限额。
- 🪶 **从不碍事**：钩子只把事件放进队列就返回，Agent 从不等监控器。显示设备在一块小小的 ESP32-S3 开发板上跑到约 60 fps。
- 🔒 **只在本机**：数据不会离开你的电脑。显示设备通过你自己的网络、凭令牌连接你自己的电脑。
- 🧹 **自己收拾干净**：每类数据都有保留期限，你在 Claude Code 或 Codex 里删除或归档的会话，也会从监控里消失。

## 🧩 工作原理

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
                                            │  REST + WebSocket, over your Wi-Fi
               ┌────────────────────┬───────┴────────────┬──────────────────────┐
               ▼                    ▼                    ▼                      ▼
        📟 desk display      your own device       web dashboard         agentsmon status
```

每个 Agent 通过插件上报自己在做什么，守护进程再从 Agent 自己的日志里读出精确的 token 数。一个小小的守护进程把这些整理成实时状态，所有屏幕读的都是同一份汇总。接入其他 Agent 只需要一个适配器和一个集成：见 [Adding an agent](docs/ARCHITECTURE.md#adding-an-agent)（英文）。

## 🤔 常见问题

**能让 Agent 帮我装吗？** 能，而且这是最省事的办法。这里的一切都为它写好了，从[菜谱](docs/RECIPES.zh-CN.md)开始就行。只有 Wi-Fi 密码和那根 USB 线得你自己来。

**一定要买硬件吗？** 不用。[浏览器预览](devices/README.zh-CN.md#-在浏览器里试试)、仪表盘和 `agentsmon status` 都不需要硬件。不过，一块永远不用切过去看的屏幕，才是这个项目的意义所在；参考开发板大约十五分钟就能装好。

**会拖慢我的 Agent 吗？** 不会。钩子把事件放进队列就立即返回，和守护进程通信一次约 70 µs。

**显示设备能离电脑多远？** 同一个 Wi-Fi 下哪里都行。只有你明确配置时，守护进程才会监听局域网，而且必须带令牌。

**支持哪些 Agent？** 目前支持 Claude Code（终端和 Desktop）以及 Codex。其他 Agent 只需要写一个适配器：[欢迎参与](docs/ARCHITECTURE.md#adding-an-agent)。

**能换成别的样子吗？** 能，这正是主题的用途。见[主题指南](devices/themes/README.zh-CN.md)，或者直接跟 Agent 说你想要什么。

**`her` 适合放在办公室吗？** 她只是小屏幕上的一张脸，因为你没回授权请求而生闷气。同事肯定会问，但都是有趣的问题。她的脸也[可以随便换](devices/themes/her/README.zh-CN.md#-给她换张脸)。

## 📚 文档

| 指南 | 内容 |
| --- | --- |
| 🤖 [菜谱](docs/RECIPES.zh-CN.md) | 贴给 Agent 的提示词：安装、刷写、设计主题、修问题 |
| 📟 [设备](devices/README.zh-CN.md) | 在浏览器里试用、刷写参考开发板、移植到你自己的硬件 |
| 🎨 [主题](devices/themes/README.zh-CN.md) | 为显示设备做一个新外观：接口、工具箱和检查方法 |
| 💌 [Her](devices/themes/her/README.zh-CN.md) | 认识她，并用一张照片给她换上任何一张脸 |
| 🧭 [使用指南](docs/USAGE.zh-CN.md) | 命令行、配置、演示模式、仪表盘、token 统计、数据保留、隐私、故障排查 |
| 🏗️ [架构](docs/ARCHITECTURE.md)（英文） | 守护进程内部如何工作，以及如何接入其他 Agent |
| 🔌 [协议与 API](docs/PROTOCOL.md)（英文） | 事件、REST 接口和 WebSocket 消息，用来做你自己的显示设备 |
| 🧩 插件（英文） | [Claude Code](plugins/claude-code/README.md) · [Codex](plugins/codex/README.md) |

## 🗂️ 仓库结构

| 路径 | 内容 |
| --- | --- |
| `devices/` | 显示设备：共享核心、主题、开发板，以及浏览器预览 |
| `crates/agentsmon` | 守护进程、钩子转发器和命令行工具，同一个可执行文件 |
| `crates/agentsmon-protocol` | 传输用的数据类型：事件、会话、配额、汇总、WebSocket 消息 |
| `plugins/` | Claude Code 插件（钩子模块）和 Codex 插件（命令钩子） |
| `web/` | Web 仪表盘（React、Vite），只在需要时启动 |
| `scripts/` | `demo.py`：让守护进程跑在虚构的会话上 |
| `docs/` | 指南和参考文档 |
| `AGENTS.md` | 这个仓库怎么运作，写给你的编码 Agent 看 |

## 🗺️ 路线图

- [x] 守护进程、协议、存储、REST 和 WebSocket API
- [x] Claude Code 和 Codex：状态、工具、子 Agent、授权和提问、上下文压缩、上下文占用、用量限制
- [x] 根据 Agent 自己的日志精确统计 token
- [x] 桌面显示设备：参考开发板、浏览器预览，以及基于共享 host 的主题（halo、pets、her 和起步模板）
- [x] Web 仪表盘和有上限的数据保留
- [x] Agent 能照着做的文档：[AGENTS.md](AGENTS.md) 和[菜谱](docs/RECIPES.zh-CN.md)
- [ ] 更多主题（[做一个](devices/themes/README.zh-CN.md)）
- [ ] 更多开发板和屏幕尺寸（[欢迎参与](CONTRIBUTING.md)）
- [ ] 更多 Agent（[欢迎参与](docs/ARCHITECTURE.md#adding-an-agent)）

## 🤝 参与贡献

欢迎提交 Issue 和 Pull Request，尤其是新的主题、开发板和 Agent 适配器。[CONTRIBUTING.md](CONTRIBUTING.md) 说明了如何构建、测试和提交改动。做了自己的显示设备？[来晒一晒](https://github.com/1vxx/AgentsMonitor/issues/new?template=show_your_build.yml) 🐾

## 📄 许可证

[MIT](LICENSE)。AgentsMonitor 是独立项目，与 Anthropic 和 OpenAI 没有关联，也未获得其背书。Claude、Claude Code 和 Codex 是其各自所有者的商标。
