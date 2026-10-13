# 红果短剧Switch客户端

Switch 第一代 / Lite / OLED 的原生自制应用。当前正式版为 **0.1.8**，用户已确认可以正式发布。
仓库名称为 `hongguo-switch`，软件内名称为「红果短剧」。无需电脑端服务，使用 libnx、Borealis、QuickJS 和 libmpv。

正式版沿用已交付的 0.1.8 安装包，程序内容未重新编译或修改。支持封面宫格、横向选集、官网公开视频播放、控制菜单与可拖动进度条、长按临时 2 倍速、自动连播、失败重试和视频方向切换。操作提示保持原字号并置于屏幕底边。
内容源不返回可用地址的分集仍可能无法播放。开发者未进行 Switch 实机测试；本次正式发布基于用户的发布确认及已有构建、安装包校验结果。

## 下载

[NSP 直接下载](https://github.com/zqzhuhai-commits/hongguo-switch/releases/download/v0.1.8/Hongguo-Switch-0.1.8.nsp)
· [发布说明及校验文件](https://github.com/zqzhuhai-commits/hongguo-switch/releases/tag/v0.1.8)

软件内名称为「红果短剧」。NSP 的 Title ID 为 `0100F00D53414600`。
验证记录见 `docs/Switch-verification.json`。NSP 在本地打包，密钥未上传；GitHub 工作流仅发布完成的安装包。

## 功能与操作

- 短剧、漫剧及搜索结果按每行 6 张封面宫格排列；触屏点封面或剧名直接进入分集页，手柄 A 确认。
- 剧名多行显示，X 查看完整剧名；播放器使用带中文扩展字形回退的系统字体。
- 分集按钮每行 10 集横向排列，上下方向键保持列位置。
- 首页加入精选区、接着看、热播短剧与人气漫剧横向海报栏，入口集中在首页上方。
- 漫剧优先读取官网榜单，兼容网页延迟数据与直接渲染列表。
- 点播放画面或按 X 呼出控制菜单：播放/暂停、跳转、选集、倍速、重试及横竖切换。
- 自动连播默认开启，菜单可关闭并保存设置；最后一集结束时停止。
- 横竖切换在 0° / 90° 间旋转视频，保留视频比例。
- 本机收藏和最近观看；选择同一分集时恢复保存的进度。
- A 确认 / 播放暂停，B 返回，方向键选择；播放时 ← / → 跳转 10 秒。
- L / R 上一集 / 下一集，Y 切换倍速，X 播放菜单；B 关闭菜单或返回，+ 退出应用。

接口暂不可用、视频资源不兼容和网络错误会明确显示。界面支持手柄和触屏。
尚未移植定时停止、可视化备份导入和清晰度设置。App 备用入口默认请求 480P；官网入口使用官网公开画质，
优先使用 AVC / H.264，硬件解码由播放器自动选择。

数据位于 `sdmc:/switch/hongguo-saf/library.json`。本地文件与程序二进制分离。

## 编译

使用 devkitPro 的 devkita64 构建环境及 Switch portlibs：

```sh
sh scripts/fetch-deps.sh
cmake -S . -B build -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package-input -j2
```

输出 `.nro` 和 `build/package/`：后者包含独立 NSP 所需的 ExeFS、控制数据和 RomFS。
NSP 需要在本地使用自有密钥和 hacBrewPack 打包；密钥从不提交到本仓库、公开构建日志或发布包。
`title.keys` 不参与自制应用构建。

## 回归检查

```sh
g++ -std=c++17 -Inative tests/playback_policy.cpp -o /tmp/playback-policy
/tmp/playback-policy
node tests/adapter_refresh.cjs
```

## 来源与许可证

本项目按 GPL-3.0-only 发布。协议代码来自既有 hongguo-saf-mac 项目并保持原始 vendor 文件。
它的来源声明见 NOTICE.md；Switch 图形集成参考 NX Torrent Player 的 MPV 集成。
Borealis（Apache-2.0）、QuickJS（MIT）、Buffer（MIT）及 sm-crypto（MIT）保留各自许可证。
MPV / FFmpeg 和其他 portlibs 的许可证依其实际构建配置提供；构建会固定源代码提交。

### 0.1.5 媒体加载修正

允许明文 MP4 的 moov 位于文件尾或 free/mdat 之后；媒体 Range 校验起点、终点和总长，允许最后一段短于请求长度。分段下载失败先重试同一段一次；APP 资源主地址不可用时尝试接口提供的备用地址和其他兼容清晰度（最多三档）。官网视频严格匹配分集 ID，避免把推荐视频当作本集。测试覆盖上述传输与解析场景；内容源不返回播放地址时仍可能失败。

参考文件 tv.nro 的静态检查记录见 [播放器检查](docs/player-reference.md)。

0.1.5 另修正官网首集的 canonical schema 匹配：首集页面 ID 可以是剧 ID，而分集身份从官网 `/video/public/<分集ID>/...mp4` 验证；首集使用 `/player/<剧ID>`，后续集使用双 ID 路由。官网媒体使用浏览器 User-Agent 和对应播放页 Referer。`tests/official_first_episode.cjs` 专门覆盖用户反馈的《首辅娇娘》第一集。

### 0.1.6 播放交互

时间与进度条随控制菜单显示，进入播放时隐藏，点击画面或按 X 显示，再次点击画面、按 B 或选择“隐藏菜单”后隐藏。触摸任意位置或拖动后松开跳转，拖动中显示目标时间，松开只提交一次跳转；进度条获得焦点后左右键每次前后 5 秒，上键打开菜单。未知时长或播放失败时禁用跳转。长按播放画面 400ms 临时使用 2 倍速，松开、取消、切集、重试、退出或暂停时恢复长按前的实际倍速；拖动进度条和点击菜单按钮不会触发倍速，短点仍开关菜单。

### 0.1.7 进度条显示调整

进度条不再常驻，与控制菜单同时显示或隐藏。隐藏后其原位置可正常点击画面或长按倍速，不会被不可见的进度条占用。

### 0.1.8 操作提示位置

播放操作提示保持原有 16 号字号与颜色，使用底部对齐，放在屏幕下边缘上方 4 像素，不再占用原来距离底部约 100 像素的位置；菜单、进度条和触屏手势维持原有操作方式。
