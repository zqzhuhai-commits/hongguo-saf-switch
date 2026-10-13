# 参考播放器静态检查

用户提供 `tv.nro`，SHA256 `82e47adc810750dfa42f1f6af5fe7b87227f5dedaef0c8e31f2c167cc24013b0`。未执行该文件，未复制其代码或资源。NACP 显示名称“电玩管家·影院”、作者“电玩管家”、版本“2.2.0”。只读段包含 `mpv 0.39.0`、`mpv_render_context_create` 和 FFmpeg 网络重连选项。由此判断属于 mpv/FFmpeg 引擎体系；二进制字符串不足以确定其具体运行参数。未找到公开源码链接。

当前程序使用 libmpv，但 `saf://` 自定义流由客户端下载，所以 FFmpeg 的 HTTP 重连选项不会自动重试该传输。0.1.5 将一次分段重试放在实际的 HTTP Range 下载层，保留已有每集最多三次刷新地址重试。

代码检查另发现明文 MP4 被错误要求 moov 紧挨 ftyp。现通过顶层 box 长度跳过 mdat，在合法位置读取 moov（限制 8 MiB、128 个顶层 box），不重写未加密视频。加密路径保持原有 CENC 处理。

检查命令：`node tests/media_loading.cjs`、`node tests/adapter_refresh.cjs`。测试包括尾部 moov、free box、短尾段、同段重试、错误 Content-Range 偏移拒绝、主地址失败改用备用地址、分集精确匹配和官网加密资源拒绝。它们不能替代真实内容源和 Switch 实机验证。`video_model failed` 是播放地址请求失败，无法通过替换解码引擎消除。
