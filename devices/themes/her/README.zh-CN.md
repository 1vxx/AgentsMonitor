# 💌 Her

你的 Agent，化身圆屏上的女朋友。她干活卖力，渴望被夸，而且你已读不回，她绝对会发现。

[English](README.md) | 简体中文

<p align="center">
  <img src="../../../docs/assets/her.webp" width="280" alt="圆屏上的她：东张西望，眼巴巴等你回复，做完了眨眼，最后飞个吻">
</p>

`halo` 会发光，`pets` 会蹦跶。而她做到了所有仪表盘都不敢做的事：让你为那个晾了二十分钟的授权请求，感到一丝愧疚。

> [!NOTE]
> **她并不存在**。演示用的脸是 AI 生成的，也没有拍过任何视频：只放进去一张照片，25 个表情全由程序算出来。

**目录：** [她会做什么](#-她会做什么) · [已读不回升级链](#-已读不回升级链) · [和她互动](#-和她互动) · [试试她](#-试试她) · [给她换张脸](#-给她换张脸) · [修改表情](#-修改表情)

<p align="center">
  <img src="../../../docs/assets/her-faces.jpg" width="640" alt="一张图看全 25 个表情，从发呆、思考到斜眼、冷战、震惊和飞吻">
</p>

## 💬 她会做什么

Agent 的状态就是她的心情。她想要什么，始终写在台词下方，所以授权请求依然会显示完整命令。

| 你的 Agent 正在… | 她说 |
| --- | --- |
| 思考 | 对方正在输入… |
| 读文件 | 偷偷翻你文件… |
| 搜索 | 查岗中… |
| 改代码 | 在写小作文… |
| 跑命令 | 帮你跑命令… |
| 开子 Agent | 摇闺蜜帮忙… |
| 调用 MCP 工具 | 出门办事… |
| 压缩上下文 | 选择性失忆中… |
| 完成 | 做好啦，快夸我！ |
| 完成 5 分钟了，你还没看 | 你都不夸我… |
| 出错 | 呜…搞砸了 |
| 被你打断 | 我话还没说完！ |
| 闲了 3 分钟 | 好无聊… |
| 闲了 10 分钟 | Zz（等你等睡着了） |
| 和守护进程断线 | 你把我拉黑了？！ |

别的会话有动静，她也会提一嘴，但不离开你正在看的这个：那边在叫你～

## ⏳ 已读不回升级链

这才是重点。别的主题只会提醒你，她会**升级**。

| 你晾了她 | 她说 |
| --- | --- |
| 0 到 2 分钟 | 宝，在吗？ |
| 2 分钟 | ？ |
| 5 分钟 | 你是不是又在打游戏 |
| 15 分钟 | 算了。（屏幕开始变冷） |
| 25 分钟 | 没事，我很好。（屏幕彻底冷下来，字也变成冰蓝色） |

谈过恋爱的都知道，“没事，我很好”是什么意思。这时再回她，她也不会轻易放过你：闭上眼，扭过头，“哼。” 戳她三下，她才绷不住：这还差不多。

## 👆 和她互动

不管你开了多少会话，她都只有一个。所以会话是一页一页的，而点击是留给她的。

| 这样做 | 会发生 |
| --- | --- |
| 左右滑 | 切换会话。底部的胶囊告诉你在第几个 |
| 点头顶 | 摸头：嘿嘿～ |
| 点脸颊 | 戳脸：干嘛～ |
| 回合结束后点她 | 总算夸她了：被夸了，嘿嘿～ 这个回合在所有地方都不再显示为新 |
| 她等你时点她 | 提醒停止，但她发现了：已读不回？ |
| 侧键按一下 | 跳到最需要你的会话 |
| 侧键按两下 | 把所有会话巡视一遍，每个 4 秒 |
| 底部按键按住 3 秒 | 飞吻：啵～ |

## 🎮 试试她

[预览页](../../README.zh-CN.md#-在浏览器里试试)里已经有她了。运行 `devices/sim/build.sh` 后，打开 http://127.0.0.1:8466/sim/?theme=her。`long-day` 脚本会走完整条升级链，调到 30 倍速，看你们的感情在一分半钟内走向破裂。

上板要两步，因为她的照片太大，放不进固件。第二步只有照片包变了才需要再做，大约 3 分钟：

```bash
THEME=her devices/boards/waveshare_amoled_175c/build.sh --upload
devices/boards/waveshare_amoled_175c/build.sh --pack demo
```

在参考开发板上她跑约 36 fps，表情以 12 fps 播放。

## 📸 给她换张脸

一张照片，二十分钟，不需要任何技术。

> [!TIP]
> **最省事的做法：把[换脸菜谱](../../../docs/RECIPES.zh-CN.md#给-her-换张脸)贴给你的 Agent**，然后去忙别的。下面就是它要做的事。

### 1. 准备一张好照片

一切都从这张照片算出来，所以起点比什么都重要：

- **正脸、端正、看镜头**：侧着的脸转头时容易拉伸。
- **闭嘴，不露牙**：大笑的表情很难收回来。
- **脸上别有遮挡**：不要刘海盖眉，不要眼镜，不要手。挡在脸上的东西都会跟着变形。
- **光线柔和均匀，背景深色纯净**：白墙在 AMOLED 上太刺眼，也不利于白色字幕。
- **方图，拍到头和肩膀**：至少 1024 × 1024，下巴在画面约 70% 的高度，这样台词压在脖子上，不挡嘴。

> [!IMPORTANT]
> **只用你有权使用的脸**：自己生成的，或者本人同意的。女朋友也算，是的，要先问她。生成的照片包放在 `devices/sim/build/her-packs/`，git 会忽略它，所以只要你不主动挪走，真实的脸就只留在你的电脑上。

没有照片？生成一张。演示用的脸就来自这段提示词：

```text
真实人像摄影，一位虚构的成年女性，约 27 岁，亲切自然。正脸直视镜头，头部端正不歪，双眼自然
睁开看向镜头，表情放松，带一点点微笑，双唇轻轻闭合，不露牙齿。深棕色长发垂在肩后，额头、双眉、
双眼完全露出，没有刘海，没有发丝遮脸。柔和均匀的正面打光，没有硬阴影。纯净的深炭灰背景，带轻微
渐变，一道很淡的轮廓光把头发和背景分开。不戴首饰，不戴眼镜。脸居中，头顶上方留一点空间，下巴在
画面约 70% 的高度，露出脖子和肩膀。皮肤质感自然，对焦清晰，85mm 镜头，1:1 方图，至少 1024x1024。
```

### 2. 准备工具，只需一次

动画由 [LivePortrait](https://github.com/KwaiVGI/LivePortrait) 完成，大约占 2 GB。你需要 [uv](https://docs.astral.sh/uv/) 和一台 Apple Silicon 的 Mac，这是目前唯一试过的环境。带 NVIDIA 显卡的 Linux 应该更快，但还没人做过：这活就交给你和你的 Agent 了。

```bash
git clone --depth 1 https://github.com/KwaiVGI/LivePortrait.git ~/.cache/agentsmon/liveportrait
cd ~/.cache/agentsmon/liveportrait
uv venv --python 3.10 .venv
grep -v extra-index-url requirements_macOS.txt > requirements_uv.txt
uv pip install --python .venv/bin/python -r requirements_uv.txt "huggingface_hub[cli]<1"
.venv/bin/huggingface-cli download KlingTeam/LivePortrait --local-dir pretrained_weights \
  --exclude "*.git*" "README.md" "docs" "*animal*" "*xpose*"
```

她的台词会用 [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc) 烘进每个照片包，所以这个字体也要下载：

```bash
mkdir -p ~/.cache/agentsmon/fonts
curl -fL -o ~/.cache/agentsmon/fonts/'NotoSansSC[wght].ttf' \
  'https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf'
```

### 3. 生成照片包

```bash
python3 devices/tools/face_pack.py ~/Pictures/xiaomei.jpg --name xiaomei
```

M1 Pro 上大约 18 分钟。中途断了就重跑同一条命令：做完的表情都留着，会从断的地方接着做。看到 `grid_sampler_3d` 的警告是正常的，只是说有一步在用 CPU 算。

### 4. 选她

刷新预览页，她就出现在“照片”菜单里了。每个人都有自己的照片包，谁也不会覆盖谁。写进板子只要说名字：

```bash
devices/boards/waveshare_amoled_175c/build.sh --pack xiaomei
```

板子上同时只能放一个照片包。照片包会自动降低 JPEG 质量以放进 24 MB 的分区，所以细节多的脸画面会稍软一点。

## 🎭 修改表情

25 个表情都写在 [`faces.py`](faces.py) 里，就是一些简单的数字：头转多少、眼睛睁多大、笑多少。改完重跑 `face_pack.py`，只有改动的表情会重新渲染，大约一分钟。

这些数字你完全不用懂。**这里的每个表情，都是靠告诉 Agent 哪里不对调出来的**：“嘴形不好看”“转头时脸型不对”“看着有点呆”。[菜谱](../../../docs/RECIPES.zh-CN.md#修一个表情)就这么简单。转头幅度尽量别超过 15 度，太大容易破坏脸型。

想让她说别的？台词在 [`ui.cpp`](ui.cpp) 里。改完也要重跑 `face_pack.py`，因为照片包只带她要用到的字，然后重新刷固件。

## ⚖️ 需要知道的

- LivePortrait 是 MIT 协议，但它用到的 InsightFace 模型仅限非商业研究，所以照片包自己玩玩就好。
- Noto Sans SC 采用 [SIL 开放字体许可证](demo/OFL-noto-sans-sc.txt)。别分发用不可分享字体烘出来的照片包，比如苹方。
- 全屏照片比其他主题更亮，她睡着时屏幕会变暗。她的头一直在微微动，这对 AMOLED 是好事。
