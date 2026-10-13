# 参考播放器静态检查

用户提供 `tv.nro`，SHA256 `82e47adc810750dfa42f1f6af5fe7b87227f5dedaef0c8e31f2c167cc24013b0`。未执行该文件，未复制其代码或资源。NACP 显示名称“电玩管家·影院”、作者“电玩管家”、版本“2.2.0”。只读段包含 `mpv 0.39.0`、`mpv_render_context_create` 和 FFmpeg 网络重连选项。由此判断属于 mpv/FFmpeg 引擎体系；二进制字符串不足以确定其具体运行参数。未找到公开源码链接。

当前程序使用 libmpv，但 `saf://` 自定义流由客户端下载，所以 FFmpeg 的 HTTP 重连选项不会自动重试该传输。0.1.5 将一次分段重试放在实际的 HTTP Range 下载层，保留已有每集最多三次刷新地址重试。

代码检查另发现明文 MP4 被错误要求 moov 紧挨 ftyp。现通过顶层 box 长度跳过 mdat，在合法位置读取 moov（限制 8 MiB、128 个顶层 box），不重写未加密视频。加密路径保持原有 CENC 处理。

检查命令：`node tests/media_loading.cjs`、`node tests/adapter_refresh.cjs`。测试包括尾部 moov、free box、短尾段、同段重试、错误 Content-Range 偏移拒绝、主地址失败改用备用地址、分集精确匹配和官网加密资源拒绝。它们不能替代真实内容源和 Switch 实机验证。`video_model failed` 是播放地址请求失败，无法通过替换解码引擎消除。

## 官网首集对照

2026-10-13 直接检查用户给出的官网 `/player/7686713195246930968`。页面首集分集 ID 为 `7686715217652223038`，VideoObject.about/@id 为整剧 canonical URL 加 `#episode`，contentUrl 为官网 `/video/public/7686715217652223038/360p.mp4`。浏览器 video 元素 readyState=4，duration=174.68 秒。此前匹配要求 schema 中包含分集 ID，在首集 canonical 路由下会误拒绝公开源。现以官网公开媒体路径中的精确分集 ID 同样允许匹配，仍拒绝其他分集和任意推荐地址。官网媒体请求使用浏览器 User-Agent 和该播放页 Referer。模拟回归用例通过，但浏览器加载成功不等同于 Switch 新版本播放通过。

新增 `scripts/probe-official-player.py` 从 CI 网络执行只读公开页面与两个实际 Range 检查，保存可用性结果；内容源暂不可用时报告失败类型，不影响软件编译。
