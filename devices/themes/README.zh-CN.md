# 🎨 主题

主题就是显示设备的样子和手感：画什么、怎么动。每个主题都放在这里的一个目录里，能跑在任何开发板上，也能在浏览器里跑。每个固件镜像只装一个主题，构建时选择。

[English](README.md) | 简体中文

<table>
  <tr>
    <td align="center"><img src="../../docs/assets/halo-home.png" width="170" alt="halo"><br><b><code>halo</code></b><br><sub>一颗光球，颜色代表所有会话的总体状态</sub></td>
    <td align="center"><img src="../../docs/assets/pets.png" width="170" alt="pets"><br><b><code>pets</code></b><br><sub>每个会话一只像素小动物，用动作表演状态</sub></td>
    <td align="center"><img src="../../docs/assets/state-her-done.png" width="170" alt="her"><br><b><a href="her/README.zh-CN.md"><code>her</code></a></b><br><sub>你已读不回，她绝对会发现的女朋友</sub></td>
    <td align="center"><img src="../../docs/assets/theme-starter.png" width="170" alt="starter"><br><b><code>_template</code></b><br><sub>起步模板，每个新主题都从它复制而来</sub></td>
  </tr>
</table>

## ⚡ 五分钟做一个新主题

```bash
devices/tools/new_theme.sh aurora          # 把 _template 复制成 themes/aurora
THEME=aurora devices/sim/build.sh          # 为预览页构建它
python3 -m http.server 8466 --bind 127.0.0.1 --directory devices
```

打开 http://127.0.0.1:8466/sim/?theme=aurora，然后编辑 `devices/themes/aurora/ui.cpp`，重新构建、刷新页面就能看到效果。满意了就刷到开发板上：

```bash
THEME=aurora devices/boards/waveshare_amoled_175c/build.sh --upload
```

## 🤖 或者说给你的 Agent 听

主题很适合交给 Agent，因为它看得见自己做出来的东西：`snap.mjs png` 能给它任意时刻的画面，`snap.mjs check` 告诉它画得对不对。这样你只需要当美术总监。

把你的点子和[主题菜谱](../../docs/RECIPES.zh-CN.md#想一个新主题)一起贴给它，然后用大白话说哪里不对：“光球太亮了”“等待的感觉要更急一点”。`her` 的 25 个表情就是这样调出来的。

## 🧩 主题在哪一层

```
   board                       host                          theme
 ┌─────────────────┐  ui.h  ┌──────────────────────┐ theme.h ┌──────────────────┐
 │ panel, touch,   │ ─────▶ │ reads the summaries  │ ──────▶ │ what it draws,   │
 │ buttons, Wi-Fi, │        │ keeps the clocks     │         │ how it moves     │
 │ speaker         │ ◀───── │ finds what happened  │ ◀────── │                  │
 └─────────────────┘ frames │ plays shared chimes  │         └──────────────────┘
   devices/boards/  chimes  │ one press from two   │           devices/themes/
                            └──────────────────────┘
                              devices/core/host.cpp
```

开发板看不到主题，主题也看不到开发板。中间的 host 把所有主题本来都要各做一遍的事做好了，所以每个主题的行为都一致：同样的时间响起同样的提醒，同样的事情发出同样的提示音，双击侧键的判断也一样。

| host 负责 | 主题负责 |
| --- | --- |
| 把每份汇总解析成 `model::Summary`，并保留上一份 | 用自己的风格画出会话 |
| 维护稳定时钟和真实时间（`now`、`wall`、`dt`） | 让东西动起来，通常用 `Spring` |
| 算出发生了什么：会话开始等你、一轮结束、等待一直没人处理 | 在屏幕上做出反应 |
| 为这些事件播放共享的提示音 | 需要的话，加上自己的提示音 |
| 区分侧键按一下和按两下，手指在屏幕上时忽略按键 | 决定每次按键去哪里 |
| 连接断开时忘掉汇总和等待 | 在 `reset()` 里清空自己的画面 |

## 📐 接口

全部定义在 [`core/theme.h`](../core/theme.h)。主题要定义 `theme::instance()`，以及一个实现 `theme::Theme` 的类：

| 方法 | host 何时调用 | 必需？ |
| --- | --- | --- |
| `init()` | 核心初始化之后，调用一次 | ✅ |
| `frame()` | 每帧一次，在输入之后，推进动画并排好这一帧 | ✅ |
| `draw(band, y0, y1)` | 对这一帧的每个行带调用，可能同时在两个核心上 | ✅ |
| `event(e, next)` | 新汇总里发生的每件事，在它成为当前汇总之前 | 可选 |
| `summary()` | 新汇总成为当前汇总之后，让主题自己的对象跟上它 | 可选 |
| `touch(kind, x, y, ms)` | 每次手指按下、移动、抬起 | 可选 |
| `press(times, ms)` | 侧键：`times` 先是 1，450 ms 内再按一次就是 2 | 可选 |
| `summon(ms)` | 另一个按键被按住 | 可选 |
| `reset()` | 和守护进程的连接断开了 | 可选 |
| `screen()`、`timings()` | 给日志和预览页用 | 可选 |

host 提供给主题的：

| 调用 | 得到 |
| --- | --- |
| `theme::state()` | 连接状态、最新的汇总、`now`、`wall`、`dt`，以及正在等你的会话 |
| `theme::chime(cue)` | 播放主题自己的提示音 |
| `theme::ack()` | 表示用户已经看过在等的事，提醒停止 |
| `theme::seen(key, endedAt)` | 表示用户在屏幕上看过某个会话刚结束的那一轮；开发板会告诉 daemon，让它在所有地方都不再算新的 |
| `theme::reminders(out, max)` | 这一帧到期的提醒，在 `frame()` 里主题想做出反应的地方取 |
| `theme::data()` | 放不进固件的大数据，比如 [Her 的照片](her/README.zh-CN.md)：开发板从闪存里分段读取，预览页直接下载 |

## 🧰 `core/` 里的工具箱

| 头文件 | 用途 |
| --- | --- |
| `model.h` | 汇总：会话、状态、在等什么、上一轮怎么结束的、token、上下文 |
| `events.h` | 从两份汇总的差异里读出等待、完成和提醒 |
| `gfx.h` | 在 466 × 466 圆屏上绘图：圆环、圆弧、圆点、光晕、圆角矩形、暗角。每帧录制一次，按行带回放 |
| `text.h` | 点阵文字：三种拉丁字体和一套中文点阵字体，支持截断、换行和数字滚动 |
| `spring.h` | 所有动画共用的阻尼弹簧 |
| `gesture.h` | 一根手指：轻点、拖动和速度，所有主题共用同样的阈值 |
| `synth.h` | 提示音 |
| `jpeg.h` | 解码照片，开发板上由芯片完成，预览页上由浏览器完成 |

`halo` 示范了用 `gfx` 做平滑的矢量绘图；`pets` 示范了自己的像素点阵（`board.cpp`）和像素画（`sprites.h`）；`her` 示范了播放数据包里的照片，文字也烘在包里；起步模板是最小的完整主题。

## ✅ 好主题的标准

- **一个目录**：`themes/<名字>/` 里放 `ui.cpp`、它自己的其他文件，以及一个 `theme.json`，写上中英文的名字和一句话简介。目录名用小写，同时也是 C++ 命名空间。
- **不写平台代码**：不引用 Arduino 或 ESP-IDF 的头文件，同一份文件要能为浏览器和每块开发板编译。
- **每种状态都要显示**：等待授权或回答、思考、运行工具、压缩上下文、完成、出错、空闲、无信号、没有会话、连接中和断线。预览页的演示剧本会把它们都走一遍。
- **按行带绘制结果一致**：`draw()` 只读取 `frame()` 留下的东西，所以在两个核心上按 16 行一带绘制，和整帧绘制的像素完全相同。`snap.mjs check` 可以验证。
- **足够快**：在参考开发板上至少 30 fps；halo 和 pets 都接近 60 fps，`her` 要解码照片，约 36 fps。开发板的串口日志每秒打印帧率和每帧耗时。大的数据表放 PSRAM（`gfx::bigAlloc`），内部 RAM 留给 Wi-Fi。
- **爱护 AMOLED**：以黑色为主，不要让亮的东西几个小时停在同一个位置。`halo` 和 `pets` 每隔几分钟会把画面挪几个像素，`her` 则一直在动。
- **暂时只有一种屏幕形状**：主题绘制的是 466 × 466 的圆形画面（`gfx::W`、`gfx::H`）。见[其他屏幕尺寸](../README.zh-CN.md#路线-b换一种屏幕尺寸或形状)。

## 🧪 测试

`snap.mjs` 在 Node 里用预览页的剧本驱动主题，不需要浏览器。先构建主题（`THEME=<名字> devices/sim/build.sh`）。

```bash
node devices/sim/snap.mjs check aurora                    # 每个剧本里，按行带绘制都和整帧完全一致
node devices/sim/snap.mjs png aurora day 25 aurora.png    # 截一张某个时刻的图，放进 Pull Request
```

重构主题时，可以逐帧证明屏幕上什么都没变：

```bash
node devices/sim/snap.mjs record aurora before.json       # 改动之前
node devices/sim/snap.mjs record aurora after.json        # 改动并重新构建之后
node devices/sim/snap.mjs compare before.json after.json  # 每一帧、每个提示音、每个画面都一样吗？
```

## 🐾 分享

提交一个 Pull Request，包含主题目录、一两张 `snap.mjs png` 截图，以及它显示什么（见 [CONTRIBUTING](../../CONTRIBUTING.md#adding-a-theme)）。只给自己桌上做的？也欢迎[来晒一晒](https://github.com/1vxx/AgentsMonitor/issues/new?template=show_your_build.yml)。
