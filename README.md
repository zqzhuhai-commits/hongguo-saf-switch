# 红果短剧Switch客户端

Switch 第一代 / Lite / OLED 的原生自制应用。版本 0.1.0 为试验版。
仓库目标名称为 `hongguo-switch`，软件内名称为「红果短剧」。图标沿用用户提供的图片。无需电脑端服务，使用 libnx、Borealis、QuickJS 和 libmpv。

**当前不宣称已在 Switch 实机播放验收，也不宣称已验证系统 22.5.0 的兼容性。**

## 功能与操作

- 短剧、漫剧浏览，短剧搜索，分页和选集。
- 视频播放 / 暂停、前后跳转、选集、倍速及自动下一集。
- 本机收藏和最近观看；选择同一分集时恢复保存的进度。
- A 确认 / 播放暂停，B 返回，方向键选择；播放时 ← / → 跳转 10 秒。
- L / R 上一集 / 下一集，Y 切换倍速，+ 退出应用。

接口暂不可用、视频资源不兼容和网络错误会明确显示。界面为手柄列表布局，
尚未移植桌面版海报墙、定时停止、可视化备份导入和清晰度设置。默认请求 480P，
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

## 来源与许可证

本项目按 GPL-3.0-only 发布。协议代码来自既有 hongguo-saf-mac 项目并保持原始 vendor 文件。
它的来源声明见 NOTICE.md；Switch 图形集成参考 NX Torrent Player 的 MPV 集成。
Borealis（Apache-2.0）、QuickJS（MIT）、Buffer（MIT）及 sm-crypto（MIT）保留各自许可证。
MPV / FFmpeg 和其他 portlibs 的许可证依其实际构建配置提供；构建会固定源代码提交。
