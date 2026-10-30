# 🤖 菜谱：让你的 Agent 来

你的终端里本来就开着一位不知疲倦的工程师，而你装这个项目，就是为了不用再盯着他。那就让他来干这活吧。

下面每道菜谱都是一段提示词：贴进 Claude Code 或 Codex，把 `<这样的地方>` 填好就行。仓库已经在 [AGENTS.md](../AGENTS.md) 里告诉 Agent 这里怎么运作，所以提示词可以很短。然后你就去追剧吧，等它需要你的时候，桌上的小屏幕会告诉你。想想还挺有诗意的。

[English](RECIPES.md) | 简体中文

> [!IMPORTANT]
> **有三件事得你亲自来**：自己把 Wi-Fi 密码写进 `secrets.h`，自己插上开发板（需要时按住 BOOT），自己看清楚要批准的是什么。用到这些的菜谱都写明了，Agent 应该停下来等你。

**目录：** [开始使用](#-开始使用) · [改成你的](#-改成你的) · [出问题的时候](#-出问题的时候) · [写一道自己的](#-写一道自己的)

## 🚀 开始使用

### 安装

```text
克隆 https://github.com/1vxx/AgentsMonitor 并阅读它的 AGENTS.md。按 README 的快速开始操作：
安装守护进程，并连接我装了的 Claude Code 和 Codex。如果缺 Rust 或其他东西，先告诉我再装。
用 `agentsmon status` 证明它能用，并告诉我哪些步骤只能我自己做，比如信任 Codex 的钩子。
```

### 没有硬件，先看看

```text
在 AgentsMonitor 里构建显示设备的预览页，启动服务并用 Her 主题打开。再运行 scripts/demo.py，
把页面连过去，让我看看各个主题跑虚构会话的样子。告诉我该点哪里。
```

### 摆到我桌上

```text
在 AgentsMonitor 里，按 devices/README.zh-CN.md 的“制作参考设备”，帮我做好微雪 1.75C。
令牌你来生成，配置和 secrets.h 也由你设置。写到 Wi-Fi 名称和密码时停下来，让我自己填进
secrets.h，不要在对话里问我要。找到开发板，刷入 THEME=<halo、pets 或 her>，然后读 15 秒串口
日志，告诉我是否连上了、帧率多少。
```

### 问问现在什么情况

```text
运行 `agentsmon status --json`，用两句话告诉我哪些会话在等我，各自想要什么。
```

## 🎨 改成你的

### 想一个新主题

```text
给 AgentsMonitor 的显示设备做一个新主题：<你的点子，越怪越好>。
先用 devices/tools/new_theme.sh 创建，再按 devices/themes/README.zh-CN.md 来做，那里列出的
所有状态都要有画面。用 `snap.mjs png` 看看自己画出来的效果，有问题先修好再给我看。
最后跑 `snap.mjs check`，并在预览页打开。
```

好的需求描述的是屏幕该有的感觉，而不是怎么画。“一个小水族箱，每个会话是一条鱼，等你的那条会敲玻璃”，比一大段坐标管用多了。

### 给 Her 换张脸

```text
按 devices/themes/her/README.zh-CN.md 准备好生成照片包需要的东西（已经有就跳过），用
<照片路径> 生成一个名叫 <名字> 的照片包。先对照那份指南检查照片，有问题提前告诉我。
做完在预览页打开。不要把它放进 git。
```

手头没有合适的脸？同一份指南里有生成人脸的提示词。

### 修一个表情

```text
Her 主题里的 <表情> 不对劲：<用大白话说你的感觉>。
在 devices/themes/her/faces.py 里改好，再用 face_pack.py 重新生成照片包（只会渲染改动的部分），
然后在预览页给我看。
```

“嘴形不好看”“看着有点呆”都是完全合格的 bug 报告。她的每个表情都是这样调出来的。

### 移植到别的板子

```text
把 AgentsMonitor 的显示程序移植到 <你的开发板和屏幕>。按 devices/README.zh-CN.md 的“路线 A”来做，
引脚定义去查开发板的资料，不要猜。没法验证的地方停下来告诉我。
```

### 用什么都能做个显示器

```text
用 docs/PROTOCOL.md 里描述的 /v1/summary，让 <树莓派加墨水屏 / 菜单栏小图标 / 给旧手机的网页>
显示我的 Agent 在干什么。尽量小巧。用 scripts/demo.py 测试。
```

### 支持另一个 Agent

```text
按 docs/ARCHITECTURE.md 的“Adding an agent”，给 AgentsMonitor 加上 <Agent 名称> 的支持。
写代码之前，先告诉我你打算怎么读取它的状态。
```

## 🩺 出问题的时候

这里的问题大多看看日志就能查出来，而 Agent 读日志比咱俩都强。告诉它现象就好，不用先猜原因。

### 会话不显示

```text
AgentsMonitor 没显示我的会话。按 docs/USAGE.zh-CN.md 的故障排查走一遍：检查守护进程、插件和日志，
找出原因。能安全修好的就修，只能我做的告诉我。
```

### 显示设备连不上

```text
我的 AgentsMonitor 显示设备一直卡在“connecting”。按 devices/README.zh-CN.md 的故障排查来：
读开发板的串口日志，再从这台电脑测试守护进程的地址和令牌。不要把令牌打印出来。
```

### 屏幕上有东西不对

```text
<主题> 主题上，<你看到了什么，什么时候出现的>。在预览页或用 snap.mjs 复现，找到原因并修好，
然后给我看修改前后的对比。
```

### 数字不对

```text
AgentsMonitor 显示 <它显示的内容>，而我以为是 <你以为的>。读 docs/USAGE.zh-CN.md 的“Token 数和用量限制”，
再对照守护进程的数据，告诉我哪个是对的、为什么。
```

## ✍️ 写一道自己的

一段好提示词有三部分：你想要什么、照哪份指南做、怎么知道做成了。如果你的效果不错，[欢迎发过来](../CONTRIBUTING.md)。菜谱是这个仓库里最容易提的 pull request，而且这也可以让 Agent 帮你写。
