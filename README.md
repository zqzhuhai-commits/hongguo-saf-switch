# 红果短剧 Switch 0.1.9 测试版

此版本以原仓库客户端架构为基础，适配用户提供的 build158 单视频请求形式，未修改 build158 NSP 本体。视频播放成功率及 Switch 实机操作等待测试，不宣称已完整复刻 build158 的网络签名或播放链路。

- 默认优先使用 `/novel/player/video_model/v1/` 与 `video_id` 请求体，包含 build158 的 `disable_video_relate_book` 字段；支持直接返回的 `video_model`。
- 首页最顶部“一键清除视频缓存”清理播放源、视频信息和后台活动流的内存缓存，保留收藏、观看历史及目录缓存；本架构不在 SD 卡保存视频下载缓存。
- 播放菜单可在 0° 与 90° 间旋转视频，保持比例。
- 点击或拖动进度条跳转；长按播放画面 400ms 临时 2 倍速，松开、取消、暂停或切集恢复原倍速。
- 名称为“红果短剧”，安装图标使用用户提供图片；界面按钮及操作说明为中文。
- RomFS 内置 `hongguo.cfg`，SD 卡 `switch/hongguo.cfg` 可按项目覆盖。支持 `hwdec=auto/no/nvdec/nvdec-copy` 与 `source=app158/web`。

**本架构使用原仓库的 GPU OpenGL 渲染。build158 专用的 `renderer=deko3d` 不兼容，不能直接套用其渲染实现。原 158 的完整网络签名和响应处理尚未全部恢复，单视频接口适配需实机验证。**

Title ID 沿用仓库 `0100F00D53414600`，与上传的 build158 不同。源码许可证与来源声明沿用 LICENSE、NOTICE.md 和 licenses/。

## 构建

```sh
sh scripts/fetch-deps.sh
cmake -S . -B build -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package-input -j2
```

输出 NRO、ExeFS、NACP 和 RomFS。NSP 使用本地打包工具与本地密钥生成，不上传密钥。

## 回归检查

```sh
node tests/build158_source.cjs
node tests/cache_clear.cjs
node tests/adapter_refresh.cjs
node tests/media_loading.cjs
node tests/official_first_episode.cjs
g++ -std=c++17 -Inative tests/playback_touch.cpp -o /tmp/touch && /tmp/touch
g++ -std=c++17 -Inative tests/playback_policy.cpp -o /tmp/policy && /tmp/policy
g++ -std=c++17 -Inative tests/app_config.cpp -o /tmp/config && /tmp/config
```

检查通过仅证明本地逻辑和数据格式处理符合测试样例，不证明内容源在线可用。
