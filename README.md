# 红果短剧Switch客户端

Switch 第一代 / Lite / OLED 的原生自制应用。版本 0.1.2 为试验版。
仓库名称为 `hongguo-switch`，软件内名称为「红果短剧」。图标沿用用户提供的图片。无需电脑端服务，使用 libnx、Borealis、QuickJS 和 libmpv。

**0.1.2 试验版：修复进入列表项目时的页面栈崩溃。**
0.1.0 的 App 视频接口返回“未知异常”，本版优先读取官网指定分集的公开 MP4，保留 App 入口作为备用。
用户已确认 0.1.0 在其大气层环境能够安装启动、获取短剧列表及分集。
0.1.1 的短剧、漫剧封面均通过原生下载解码测试；两类各前两集通过分段读取及首秒音视频解码测试。
0.1.1 在部分设备上点击列表项目会闪退；0.1.2 修正了 AppletFrame 页面栈初始化。0.1.2 尚待实机复测。

## 下载

[NSP 直接下载](https://github.com/zqzhuhai-commits/hongguo-switch/releases/download/v0.1.2-alpha.1/Hongguo-Switch-0.1.2-alpha.1.nsp)
· [发布说明及校验文件](https://github.com/zqzhuhai-commits/hongguo-switch/releases/tag/v0.1.2-alpha.1)

软件内名称为「红果短剧」。NSP 的 Title ID 为 `0100F00D53414600`。
验证记录见 `docs/Switch-verification.json`。NSP 在本地打包，密钥未上传；GitHub 工作流仅发布完成的安装包。

## 功能与操作

- 短剧、漫剧浏览，异步封面加载，短剧搜索，分页和选集。
- 漫剧优先读取官网榜单，兼容网页延迟数据与直接渲染列表。
- 播放器已集成播放 / 暂停、前后跳转、选集、倍速及自动下一集；已接入官网 MP4，实机播放待验收。
- 本机收藏和最近观看；选择同一分集时恢复保存的进度。
- A 确认 / 播放暂停，B 返回，方向键选择；播放时 ← / → 跳转 10 秒。
- L / R 上一集 / 下一集，Y 切换倍速，+ 退出应用。

接口暂不可用、视频资源不兼容和网络错误会明确显示。界面为手柄列表布局，
尚未移植桌面版海报墙、定时停止、可视化备份导入和清晰度设置。App 备用入口默认请求 480P；官网入口使用官网公开画质，
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
