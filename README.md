# Mania Player
### A cross-platform(in planned) rhythm game. Written in C++/SDL3.

## Features
- Read other rhythm game's charts
- Play keysounds
- Support osu's storyboard
- Capable with osu's skin
- Can Read / Write / Edit osu! replay files [*.osr]

## Support Chart formats
- Be-Music Script [*.bms]
- DJMAX RESPECT [*.bytes]
- DJMAX ONLINE [*.pt]
- osu!mania [*.osu] ([EXPERIMENTAL]support standard converted maps)
- O2Jam [*.ojn]

### Formats planned to support
- Malody
- EZ2DJ/EZ2ON
- Stepmania
- MUSYNX/MUSYNC
- Beatmania IIDX

## Dependencies

This project uses the following third-party libraries:

### Precompiled Libraries

| Library | Version | License | Description |
|---------|---------|---------|-------------|
| [SDL3](https://libsdl.org/) | 3.2.30 | zlib | Window, graphics, input handling |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | 3.2.0 | zlib | TrueType font rendering |
| [BASS](https://www.un4seen.com/) | 2.4 | Commercial/Free | Audio playback |
| [BASS_FX](https://www.un4seen.com/) | 2.4 | Commercial/Free | Audio effects (tempo change) |
| [ICU](https://icu.unicode.org/) | 78.2 | Unicode License | Unicode collation |
| [FFmpeg](https://ffmpeg.org/) | 8.0.1 | LGPL | Video decoding |

### Source Libraries

| Library | License | Description |
|---------|---------|-------------|
| [LZMA SDK](https://7-zip.org/sdk.html) | Public Domain | LZMA compression |
| [minilzo](http://www.oberhumer.com/opensource/lzo/) | GPL | LZO compression |

### Header-only Libraries

| Library | License | Description |
|---------|---------|-------------|
| [stb_image](https://github.com/nothings/stb) | MIT/Public Domain | Image loading |
| [stb_vorbis](https://github.com/nothings/stb) | MIT/Public Domain | Vorbis audio decoding |

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



### Mania Player v0.0.3

- 修复由于延迟导致暂停前后时间不一致的问题
- 修复空按时不会出现key音的问题
- 新增Storyboard T(rigger)命令支持，同时也引入了新bug，(不)一定会修复
- 新增DJMAX ONLINE谱面，key音读取(由于版权原因，我不会告诉你如何下载谱面文件)
- 新增Be-Music Script (BMS)谱面，key音读取
- 新增视频BGA支持
- 新增性能Debug输出



### Mania Player v0.0.4b

- 修复了导出回放到osu!时，一些mod启用状态信息没有被包含进回放文件的问题
- 修复了启用Double Time时key音会被错误升调
- 新增 Replay Factory 功能
- 新增 Replay Mod 信息修改
- 新增 Replay 元信息修改
- 新增 Replay Analyze 功能 (实现了 Press Distribution 和 Realtime Press 图表显示，基于 [Transcendence](https://github.com/adgjl7777777) 的 [VSRG_Total_Analyzer](https://github.com/adgjl7777777/VSRG_Total_Analyzer) )
- 新增 Replay 可视化视频输出 (基于 [kuit](https://github.com/Keytoyze) 的 [Mania-Replay-Master](https://github.com/Keytoyze/Mania-Replay-Master) )



### Mania Player v0.0.4

- 修复了长条断掉时 tick 也仍然在计算的问题
- 修复了长条即使不按也会出现按住的特效的问题
- 修复了设置中 Username 无法输入的问题
- 修复了 Replay Factory 读取 KeyCount 异常的问题
- 修复了启用 Mirror Input 多次进行回放导出后回放出现异常的问题
- 修复了每次打开程序后第一次游戏开头会出现幽灵按键的问题
- 修复了准备时间时暂停游戏后判定出现异常的问题
- 新增 Malody 谱面读取
- 新增 O2Jam 判定模式 (overlap-based)



## Building

1. Download BASS and BASS_FX from https://www.un4seen.com/
2. Place `bass.lib` and `bass_fx.lib` in `lib/x64/`
3. Run build.bat.



## Disclaimer

This project is for educational and personal use only.
The developers are not responsible for any misuse of this software.
All game assets and charts belong to their respective owners.



## License
````
This project is licensed under the GPL-3.0 License - see the LICENSE file for details.

Note: BASS/BASS_FX are commercial libraries and must be downloaded separately.
````
