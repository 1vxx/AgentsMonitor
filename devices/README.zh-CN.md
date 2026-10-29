# 📟 显示设备

一块摆在桌上的小屏幕，显示你的编码代理在干什么：有代理等你时会亮起来，一轮完成时会响一声，滑一滑就能翻看每个会话。

[English](README.md) | 简体中文

<p align="center">
  <img src="../docs/assets/halo-home.png" width="180" alt="halo：情绪光球">
  <img src="../docs/assets/halo-approve.png" width="180" alt="halo：一个会话请求运行命令">
  <img src="../docs/assets/pets.png" width="180" alt="pets：每个会话一只像素小动物">
  <img src="../docs/assets/halo-session.png" width="180" alt="halo：一个会话的 token 和上下文">
</p>

> [!NOTE]
> **这里的开发板只是一个例子，不是必需品。** 固件目前适配微雪 ESP32-S3-Touch-AMOLED-1.75C，只是因为作者桌上正好是这块。显示设备需要的一切，都来自守护进程提供的一份很小的 JSON；显示代码也把硬件相关的部分单独放在薄薄的一层里。换一块 ESP32、换一块屏幕，甚至用一个根本不是单片机的东西都可以：[改成你自己的](#-改成你自己的)告诉你怎么做。

**目录：** [选一条路](#-选一条路) · [在浏览器里试试](#-在浏览器里试试) · [主题](#-主题) · [制作参考设备](#-制作参考设备) · [改成你自己的](#-改成你自己的)

## 🧭 选一条路

| 你手上有 | 这样做 | 工作量 |
| --- | --- | --- |
| 还没有硬件 | [在浏览器里试试](#-在浏览器里试试) | ☕ 几分钟 |
| 微雪 1.75C | [制作参考设备](#-制作参考设备) | 🔧 约 15 分钟 |
| 别的 ESP32 和一块屏幕 | [移植到你的开发板](#路线-a换一块-esp32-开发板) | 🛠️ 一个下午 |
| 树莓派、墨水屏、M5Stack、旧手机…… | [直接读 API](#路线-c任何能发-http-请求的东西) | 🐍 约 20 行代码 |
| 一个新外观的点子 | [做一个主题](themes/README.zh-CN.md) | 🎨 五分钟起步 |

## 🎮 在浏览器里试试

预览页运行的是和固件同一份 C++ 显示代码，编译成 WebAssembly，画在 466 × 466 的画布上，与真机像素一一对应。它可以播放预设的演示剧本，也可以显示你自己的实时会话；能在所有主题之间切换，中英文都有。

需要 [Emscripten](https://emscripten.org)（`brew install emscripten`）和 Python 3。在仓库根目录运行：

```bash
devices/sim/build.sh                       # 构建所有主题
python3 -m http.server 8466 --bind 127.0.0.1 --directory devices
```

打开 http://127.0.0.1:8466，在顶部选择主题。在圆屏上拖动就是滑动，点击就是轻点，按键盘 `B` 相当于按侧键，按 `?` 查看全部快捷键。页面跟随系统的浅色或深色模式，也可以在顶部手动切换。想看自己的会话，把数据来源切换到实时 daemon（`ws://127.0.0.1:7788/v1/ws`，设置了令牌的话再填上令牌）。手边没有在跑的代理？[`scripts/demo.py`](../docs/USAGE.zh-CN.md#演示模式) 会启动一个装满虚构会话的守护进程，让预览页连上它。

> 在 Claude Desktop 中，`.claude/launch.json` 里的 `device` 配置会启动这个页面。

## 🎨 主题

**主题**决定画什么、怎么动。每个固件镜像只装一个主题，构建时用 `THEME=` 选择；预览页里所有主题都有。

| | 主题 | 是什么 |
| --- | --- | --- |
| <img src="../docs/assets/halo-home.png" width="96" alt=""> | **`halo`**（默认） | 一颗发光的球，颜色代表总体状态，每个会话是外圈轨道上的一颗卫星。有会话开始等你时，它会占满屏幕，告诉你它想运行什么。上滑打开列表，点一行就能看它的 token 和上下文。 |
| <img src="../docs/assets/pets-approve.png" width="96" alt=""> | **`pets`** | 每个会话是 LED 点阵上的一只像素小动物，用动作表演自己的状态：思考时点头，跑工具时抖动、冒火花，等你时带着 `!` 蹦跳，空闲时打盹。上滑可以看到全体成员。据说里面藏着一条龙 🐉 |

想要自己的外观？[主题指南](themes/README.zh-CN.md)带你从复制模板开始，一直到把自己的主题刷到开发板上，还介绍了接口、工具箱和检查方法。

**操作**（参考开发板上如此，预览页也一样）：

| 操作 | 作用 |
| --- | --- |
| 左右滑动 | 上一个或下一个会话 |
| 上下滑动 | 打开所有会话的列表，或返回 |
| 轻点 | 打开一个会话，或关掉提醒 |
| 侧键（上方）按一下 | 跳到最需要你的会话 |
| 侧键连按两下 | 同时看所有会话 |
| 下方按键按住 3 秒 | 召唤主题为稀有时刻准备的东西 |

**声音**：有会话开始等你时响一声，一轮完成时轻轻响一声，一轮出错时低沉地响一声。等待一直没人处理的话，会在 2 分钟、5 分钟时再提醒，之后每 10 分钟一次，直到你轻点屏幕。

## 🔧 制作参考设备

**硬件：** 一块[微雪 ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm) 和一根 USB-C 线。注意要是 **1.75C**：普通的 1.75 接线不一样。

| 部件 | 1.75C 上是 | 驱动 |
| --- | --- | --- |
| 主控 | ESP32-S3R8，8 MB PSRAM，32 MB flash | |
| 屏幕 | 1.75 英寸圆形 AMOLED，466 × 466，CO5300，QSPI 接口 | `panel.cpp` |
| 触摸 | CST9217 | `touch.cpp` |
| 按键 | PWR 经 AXP2101 电源芯片读取，BOOT 在 GPIO0 | `button.cpp` |
| 扬声器 | ES8311 编解码芯片，I2S 接口 | `audio.cpp` |
| 网络 | Wi-Fi，2.4 GHz | `net.cpp` |

### 1. 让设备能连上守护进程

守护进程默认只监听 `127.0.0.1`。在 `~/.agentsmon/config.toml` 里把它开放到局域网，并用令牌保护：

```toml
[http]
listen = "0.0.0.0:7788"
token = "paste-a-long-random-string-here"    # 可以用 openssl rand -hex 16 生成
```

重启守护进程（`agentsmon service install` 会重启登录自启的服务），然后记下电脑的局域网地址：macOS 上用 `ipconfig getifaddr en0`，Linux 上用 `hostname -I`。如果防火墙弹出询问，允许 `agentsmon` 接受传入连接。

### 2. 安装工具链

```bash
brew install arduino-cli      # 其他系统见 https://arduino.github.io/arduino-cli/
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli lib install SensorLib
```

已在 arduino-cli 1.5、esp32 core 3.3.12 和 SensorLib 0.5.1 上测试。

### 3. 告诉固件连到哪里

```bash
cd devices/boards/waveshare_amoled_175c
cp secrets.example.h secrets.h
```

编辑 `secrets.h`：填上 Wi-Fi 名称和密码（必须是 2.4 GHz 网络）、第 1 步记下的地址（`DAEMON_HOST`）和令牌。git 会忽略这个文件。

### 4. 编译并刷写

插上开发板，用 `arduino-cli board list` 找到它的端口，然后：

```bash
PORT=/dev/cu.usbmodem1101 ./build.sh --upload             # halo
THEME=pets PORT=/dev/cu.usbmodem1101 ./build.sh --upload  # 或者其他任何主题
```

不加 `--upload` 就只编译。开发板启动时会轻轻响一声，连上 Wi-Fi 后几秒内就会显示你的会话。🎉

### 故障排查

| 现象 | 检查什么 |
| --- | --- |
| 一直停在"连接中" | Wi-Fi 必须是 2.4 GHz；`DAEMON_HOST` 必须是电脑的局域网地址，不能是 `127.0.0.1` |
| 连上之后又断开 | 电脑可能睡眠了，或者它的地址变了。在路由器上给它分配固定地址 |
| 什么都不显示 | 从另一台机器测试：`curl -H "Authorization: Bearer <token>" http://<host>:7788/v1/summary` 应该输出 JSON |
| 串口日志里有 `401` | `secrets.h` 里的令牌和 `http.token` 不一致 |
| 想看日志 | `arduino-cli monitor -p <port> -c baudrate=115200` 每秒打印连接状态、帧率和剩余内存 |
| 上传时找不到开发板 | 按住 BOOT 再插上数据线，然后重新上传 |

## 🧰 改成你自己的

### 代码是怎么分层的

```
devices/
├── themes/    🎨 画什么、怎么动：halo、pets，以及新主题的起步模板
│                 _template；每个都实现 core/theme.h
├── core/      🧮 可移植的 C++17，不依赖任何平台：
│                 host.cpp   开发板看到的显示设备（ui.h），由主题驱动
│                 汇总数据模型、等待和完成的识别、绘图、文字、
│                 提示音、弹簧动画和手势，所有主题共用
├── boards/    🔌 每块开发板一个目录：驱动 + 一个 Arduino sketch，对接 ui.h
│   └── waveshare_amoled_175c/       ← 唯一和硬件相关的代码
├── sim/       🎮 预览页（同一份代码的 WebAssembly 版）和 snap.mjs
├── fonts/     🔤 中文标题用的点阵字体（拉丁字母是内置的）
└── tools/     🧪 new_theme.sh，以及各开发板共用的构建工具
```

主题和开发板可以自由组合：任何主题都能跑在屏幕合适的任何开发板上。你只需要改你想改的那一层。

### 路线 A：换一块 ESP32 开发板

把 `boards/waveshare_amoled_175c/` 复制成 `boards/<你的开发板>/`，把 `.ino` 改成和目录同名，然后替换驱动。sketch 只需要做这些事：

| 任务 | 怎么做 | 必需？ |
| --- | --- | --- |
| 启动界面 | `ui::init(CJK10, sizeof CJK10)`，字体由 `build.sh` 嵌入 | ✅ |
| 获取汇总 | 大约每秒一次 `GET /v1/summary`，带上 `Authorization: Bearer <token>`，把响应交给 `ui::summary(json)`。用 `ui::link(CONNECTING / ONLINE / LOST)` 报告连接状态 | ✅ |
| 绘制 | 每帧调用 `ui::frame(ms)`，再按行带调用 `ui::draw(band, y0, y1)`，把每一带发给屏幕：大端 RGB565，每行 466 像素。按行带绘制，所以不需要整帧缓冲 | ✅ |
| 触摸 | `ui::touch(DOWN / MOVE / UP, x, y, ms)` | 可选 |
| 按键 | 侧键用 `ui::press(ms)`，长按用 `ui::summon(ms)` | 可选 |
| 声音 | 用 `core/synth` 把每个 `ui::cue()` 从扬声器播出来 | 可选 |

`net.cpp`（通过 Wi-Fi 轮询）和 `.ino` 里的帧循环通常可以原样沿用。**核心代码的要求：** ESP32-S3 级别、带 PSRAM 的芯片，核心会在 PSRAM 里放 1 MB 以上的缓冲。第二个核心是可选的：有两个核心时绘制会分摊到两个核心上（`gfx::split`），只有一个时就先画一半再画另一半。

`build.sh` 会把 `core/` 和选中的主题复制到 sketch 的 `src/ui/` 再编译，把其中的 `FQBN` 一行改成你的开发板即可。不用 Arduino 的开发板也保持同样的结构：自己的构建脚本，把核心和主题一起编译。

### 路线 B：换一种屏幕尺寸或形状

目前核心绘制的是 466 × 466 的画面（`core/gfx.h` 里的 `gfx::W` 和 `gfx::H`），主题也都是按这个尺寸的圆屏排版的：

- **466 × 466 的方屏**：直接能用，四个角保持黑色。
- **更大的屏幕**：在屏幕驱动里偏移显示窗口，让画面居中。
- **更小或不是方形的屏幕**：需要先把画面尺寸改成参数，排版也要跟着调整。这是目前最需要的贡献，欢迎[开个 Issue](https://github.com/1vxx/AgentsMonitor/issues/new/choose) 一起设计。

### 路线 C：任何能发 HTTP 请求的东西

不需要 C++。显示设备要显示的一切，守护进程都放在一份很小的 JSON 里：

```bash
curl -H "Authorization: Bearer <token>" http://<host>:7788/v1/summary
```

```jsonc
{
  "ts": 1793087903676,
  "mood": "attention",           // attention > working > error > idle > offline：整块屏幕用一种颜色
  "counts": { "live": 3, "waiting": 1, "thinking": 1, "tool": 0, "idle": 1, "compacting": 0, "stale": 0 },
  "sessions": [                  // 最紧急的在前，最多 8 个
    {
      "key": "claude-code:4f0c…", "agent": "claude-code",
      "project": "docs-site", "title": "Write the migration guide",
      "status": "waiting_permission", "since": 1793087900000,
      "attention": { "kind": "permission", "tool": "Bash", "subject": "pnpm build && pnpm test" },
      "tokens": 182000, "context": { "percent": 41 }
    }
  ],
  "quotas": [ { "agent": "claude-code", "windows": [["five_hour", 21.0], ["seven_day", 13.0]] } ]
}
```

用 Python 写一个完整的"显示设备"，跑在终端、树莓派或者别的什么上：

```python
import json, time, urllib.request

DAEMON = "http://192.168.1.18:7788"   # 运行 agentsmon 的那台电脑
TOKEN = "the-token-from-config.toml"

ICONS = {"waiting_permission": "✋", "waiting_input": "❓", "tool": "🔧",
         "thinking": "💭", "compacting": "🗜 ", "idle": "💤"}

while True:
    req = urllib.request.Request(f"{DAEMON}/v1/summary",
                                 headers={"Authorization": f"Bearer {TOKEN}"})
    summary = json.load(urllib.request.urlopen(req, timeout=3))
    print(f"\033[2J\033[H mood: {summary['mood']}")
    for s in summary["sessions"]:
        icon = "📡" if s.get("stale") else ICONS.get(s["status"], "·")
        print(f" {icon} {s['project']:<16} {s.get('title', '')[:40]:<40} {s.get('tool', '')}")
    time.sleep(1)
```

每秒轮询一次就足够了。想要即时更新，可以连接 `/v1/ws` 这个 WebSocket（带上 `?token=`），监听 `summary` 消息。每个字段的说明见[协议与 API](../docs/PROTOCOL.md)（英文）。

### 做一个主题

换一种外观不需要新硬件，也不需要了解任何开发板：见[主题指南](themes/README.zh-CN.md)。

### 晒一晒

做了自己的显示设备，或者移植到了新的开发板？[来分享一下](https://github.com/1vxx/AgentsMonitor/issues/new?template=show_your_build.yml)，也非常欢迎为新开发板和新主题提交 Pull Request，见 [CONTRIBUTING.md](../CONTRIBUTING.md)。🐾
