# Mania Player
### A cross-platform(in planned) rhythm game. Written in C++/SDL3.

# Features
- Read other rhythm game's charts
- Play keysounds
- Support osu's storyboard
- Capable with osu's skin

# Support Chart formats:
- osu!mania [*.osu](support standard converted maps)
- DJMAX RESPECT [*.bytes]
- O2Jam [*.ojn]

### Formats planned to support:
- EZ2DJ/EZ2ON
- Stepmania
- MUSYNX/MUSYNC
- Be-music script
- Beatmania IIDX

# Changelog
### Mania Player v0.0.1b
- 新增支持读取osu!的谱面
- 新增支持使用osu!的皮肤
- 新增自定义判定区间功能
- 新增osu!的回放导入导出功能



### Mania Player v0.0.2

- 修复部分特殊BGM的歌曲，歌曲和谱面播放速度不一致的问题。
- 修复当miss长条时，note不会变灰的问题
- 修复当miss note时，note不会变灰继续下落而是直接消失的问题
- 调整打击偏移条显示为osu!stable同款
- 调整打歌界面判定显示，combo显示更接近osu!stable的风格。
- 新增选歌界面
- 新增血量系统
- 新增支持读取谱面key音并播放
- 新增osu storyboard读取并基本显示
- 新增DJMAX RESPECT谱面，key音读取
- 新增O2Jam谱面，key音读取
