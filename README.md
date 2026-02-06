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
- 新增AutoPlay,IgnoreSV(原创),Hidden,FadeIn Mod
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
- 新增Death(原创),Sudden Death Mod
- 新增支持读取谱面key音并播放
- 新增osu storyboard读取并基本显示
- 新增DJMAX RESPECT谱面，key音读取
- 新增O2Jam谱面，key音读取



### Mania Player v0.0.3a

- 重构皮肤读取系统，现在不会出现keyimage图片偏移的问题
- 修复FallBack皮肤显示异常的问题
- 修复轨道之间始终有无视skin.ini内设置的黑线的问题
- 修复miss越多游戏越卡的问题
- 修复key音同时播放过多爆音的问题
- 修复note，combo，打击动画效果播放，现在skin.ini可以像原版osu!那样控制部分动画速率了
- 修复设置中无法输入自定义下落速度的问题
- 修复设置中启用的Mod错误地对回放模式起作用的问题
- 新增设置界面上下滚动，为以后增加设置留足空间
- 新增背景暗化功能
- 新增信息缓存系统
- 新增回放与谱面自动匹配功能
- 新增osu!同款星数算法(现在实现了b20260101和b20220101的逻辑，以后还能拓展)
- 新增实时PP计算(算法来自rosu-pp)
- 为了防止回放作弊，现在强制AutoPlay模式导出的Replay玩家名为"Mr.AutoPlay"

