# build158 资源与中文补丁（未实机测试，功能未完成）

此分支针对用户上传的「红果switch--158版.zip」中的 NSP，不是 main 分支 0.1.8 客户端的升级，也不是完整功能版本。

已修改：
- 新增 66 处硬编码设置、网络测试和播放自检文案中文覆盖；固定槽内替换，格式参数顺序与代码段保持不变。
- Switch 主页安装名称：红果短剧（全部 16 个 NACP 语言槽）。
- 安装图标：用户上传的图片，转成 256×256 JPEG。
- RomFS 内置 hongguo.cfg，保留 renderer=deko3d、hwdec=auto，增加 language=zh。
- 原生配置备用路径由 sdmc:/hongguo.cfg 改为 romfs:/hongguo.cfg；首选路径 sdmc:/switch/hongguo.cfg 保留。原来位于 SD 卡根目录的配置不再读取。

**内置配置是只读的。需要在设置中保存修改时，先把本目录的 hongguo.cfg 放到 SD 卡 switch/hongguo.cfg。仅增加 language=zh 不等于所有硬编码英文按钮、说明已翻译。**

未完成：
- 首页顶部一键清除视频缓存。
- 点击进度条按点击位置跳转。
- 横竖屏切换。
- 长按画面临时 2 倍速及松开恢复。
- 全部操作按钮和说明中文化。

## 原生检查

原 NSP 有 Program、Control、Meta 三个 NCA。Program ExeFS 是原生 ARM64 NSO 程序和 NPDM；RomFS 仅有三个 deko3d 着色器，没有 HTML、JavaScript、界面 XML 或可编辑的原生源码。NSO 头的 dynstr/dynsym 描述均为空，没有可用函数符号表。

已通过字符串交叉引用和 ARM64 反汇编定位配置读取、倍速修改、触屏处理、绝对进度跳转的候选入口，记录见 verification.json。原触屏逻辑点击播放/进度区域调用暂停函数；发现绝对跳转函数不等于已经增加点击进度条功能。libmpv 中存在 video-rotate 字符串也不等于客户端提供横竖切换入口。

静态检查只发现配置、日志、收藏及历史的 SD 路径，未发现应用专用的视频磁盘缓存目录。不能把删除观看历史、收藏或日志冒充清视频缓存。

资源补丁保持 .text 机器指令完全不变，只利用备用配置路径字符串后的空白改写 .rodata，重新生成 NSO 段哈希并压缩，然后重新打包 NSP。NCA 内容哈希、NSO 三段 SHA-256 及重新解包后的 ExeFS/配置/名称/图标逐字节核对通过。未进行 Switch 启动、渲染、配置读取或播放实机验证。

安装包以两个 .part 文件保存于 dist/build158/；运行 python patches/build158/assemble_nsp.py 可合成 Hongguo-build158-resources-only-untested.nsp 并校验 SHA-256。分支的 Actions 构建也会生成完整 NSP 下载附件。Title ID 沿用原包 0101000000000000，与 main 分支客户端不同。原包版本字段 1.0.0 保留。

## 复现

使用 hactool 解出 exefs、romfs、control，并将 NSO main 解压。执行：

```sh
python patches/build158/patch_resources.py extracted main-uncompressed user-icon.png original-hongguo.cfg patched
```

再使用本地 hacBrewPack 与本地打包密钥对 patched/exefs、patched/romfs、patched/control 打包。此分支不包含任何密钥。

本目录没有恢复 build158 源码；其来源和许可证不能通过二进制检查确定，不宣称其为 main 分支 GPL 源码的编译结果。

## 本轮中文修复

复现上述补丁时额外指定 `--translations patches/build158/zh_overrides.json`。审核过的 66 个字符串、原偏移和槽大小记录在 verification.json 中。普通界面沿用 build158 自带中文文案，内置配置选择 language=zh；外部配置优先，外部配置如指定英文或自动语言，普通界面仍可能显示英文。接口地址、请求体、签名及媒体处理函数均未改动，原生 .text 段逐字节保持一致。未声称已实现点击进度跳转、长按倍速、旋转或清缓存。
