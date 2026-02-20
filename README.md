# Mania Player
### A cross-platform(half-way there!) rhythm game. Written in C++/SDL3.

## Features

- Read other rhythm game's charts (see below)
- Play keysounds
- Support osu's storyboard
- Capable with osu's skin
- osu! replay (.osr) read / write / edit
- Replay Analyze (analyze, generate video)
- Star rating & PP calculation
- Low-latency audio: DirectSound, WASAPI Shared/Exclusive, ASIO

## Support Chart formats

- Beatmania IIDX [*.1]
- Be-Music Script [*.bms, *.bme, *.bml]
- DJMAX RESPECT / DJMAX RESPECT V [*.bytes]
- DJMAX ONLINE [*.pt]
- EZ2AC FNEX [*.ez]
- EZ2ON REBOOT:R [*.ezi]
- Malody [*.mc]
- MUSYNX/MUSYNC [*.txt]
- osu!mania [*.osu] ([EXPERIMENTAL]support standard converted maps)
- O2Jam [*.ojn]
- SoundVoltex [*.vox]
- StepMania [*.sm, *.ssc]

### Formats planned to support

- Virtual Orchestra System
- More...

## Tutorial: [How to add songs?](https://github.com/4accccc/Cpp_Mania_Player/wiki/Songs-folder-structure)

## Dependencies

### Precompiled Libraries (download required)

| Library | Version | License | Description |
|---------|---------|---------|-------------|
| [SDL3](https://libsdl.org/) | 3.2.30 | zlib | Window, graphics, input |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | 3.2.0 | zlib | TrueType font rendering |
| [BASS](https://www.un4seen.com/) | 2.4 | Commercial/Free | Audio playback |
| [BASS_FX](https://www.un4seen.com/) | 2.4 | Commercial/Free | Audio effects (tempo) |
| [ICU](https://icu.unicode.org/) | 78.2 | Unicode License | Unicode collation |
| [FFmpeg](https://ffmpeg.org/) | 8.0.1 | LGPL | Video decoding |

### Optional Libraries (for low-latency audio)

| Library | License | Description |
|---------|---------|-------------|
| [BASSmix](https://www.un4seen.com/) | Commercial/Free | Audio mixer (required for WASAPI/ASIO) |
| [BASSWASAPI](https://www.un4seen.com/) | Commercial/Free | WASAPI output |
| [BASSASIO](https://www.un4seen.com/) | Commercial/Free | ASIO output |
| [BASSFLAC](https://www.un4seen.com/) | Commercial/Free | FLAC decoding |

### Source Libraries (included)

| Library | License | Description |
|---------|---------|-------------|
| [LZMA SDK](https://7-zip.org/sdk.html) | Public Domain | LZMA compression |
| [minilzo](http://www.oberhumer.com/opensource/lzo/) | GPL | LZO compression |

### Header-only Libraries (included)

| Library | License | Description |
|---------|---------|-------------|
| [stb_image](https://github.com/nothings/stb) | MIT/Public Domain | Image loading |
| [stb_vorbis](https://github.com/nothings/stb) | MIT/Public Domain | Vorbis audio decoding |

## Changelog

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



### Mania Player v0.0.5b

- 修复了osu!皮肤部分元件的错误拉伸，显示异常问题
- 修复了自定义判定窗口无法保存的问题
- 修复了自定义判定窗口无法禁用某一个判定等级的问题
- 修复了自定义判定窗口使用过于离谱的值的时候无法正确应用的问题
- 修复了长条断开时头尾落速不一致的问题
- 修复了当游玩 DJMAX RESPECT 谱面时，点击跳过键会导致背景音乐消失的问题
- 修复了歌曲选择界面谱面来源标签在一些特殊情况下会被窗口截断的问题
- 修复了一些谱面打开DT/NC/HT效果对背景音乐无效的问题
- 修复了 BMS 谱面元信息解析异常的问题
- 修复了 BMS 图片 BGA 播放异常的问题
- 新增加载界面，支持在加载过程中使用esc打断加载
- 新增 osu! 皮肤高分辨率图片资源(*@2x)读取
- 新增 MUSYNX 谱面，key音读取



### Mania Player v0.0.5

- 修复特殊情况下KeyImage被按下无法自动恢复的问题
- 修复 DJMAX RESPECT, DJMAX ONLINE, O2Jam, MUSYNX, BMS key 音无法正常加载的问题
- 修复 osu! 皮肤高分辨率图片资源读取时比例出现异常的问题
- 修复 osu! 皮肤部分元件动画速率异常的问题
- 修复 Replay Factory 回放导出功能失效的问题
- 修复对无背景音乐的谱面进行 Replay 可视化视频输出时报错的问题
- 新增 Beatmania IIDX 谱面，key音读取 (由于版权原因，我不会告诉你如何下载谱面文件)



### Mania Player v0.0.6a

- 构建系统转向cmake，现在支持linux平台。
- 修改了 Fix Beatmap Hash 的逻辑。现在对于包含多难度的单文件，会弹出难度选择窗口。
- 修复选歌界面更换难度时背景图片和背景音乐不会跟着换的问题
- 修复部分谱面导出回放后再用游戏播放提示无法正确找到谱面的问题
- 修复部分谱面游玩时没有背景图片的问题
- 修复 Generate Video 无法读取除了 *.osu 以外的谱面的问题
- 修复打击偏移条上方三角形不会移动的问题



### Mania Player v0.0.6

- 重构歌曲选择界面，加入元数据展示，osu!同款搜索功能
- 重构音频系统，降低DirectSound音频延迟，加入WASAPI Shared/Exclusive, ASIO 支持
- 修复处理部分osu!谱面时游戏闪退的问题
- 修复 osu! 早期格式谱面 key 音无法读取的问题
- 修复 osu! 谱面无法播放视频BGA的问题
- 修复 osu! storyboard 显示异常的问题
- 修复 osu! 谱面 key 音异常播放的问题
- 修复 O2Jam SV变化没有被正确处理导致 note 密度异常变大的问题
- 修复 O2Jam 使用 skip 功能导致 key 音和背景音乐错位的问题
- 修复 MUSYNX key 音转码过慢的问题
- 新增 Cinema Mod
- 新增 StepMania 谱面读取



### Mania Player v0.0.7a

- 修复大量稳定性问题:
    - 损坏的config.ini可以阻止游戏运行
    - 异步加载创建新线程时没有先 join 旧线程
    - 关闭DT时不会同步关闭NC
    - 设置界面下拉框无法遮挡后方控件
    - StepMania 谱面预览时间无效
    - BMS/O2Jam/... 等谱面的key音被加载了2次
    - 部分特别大的 BGA 图片可能导致OOM
    - 部分写的不规范的 BMS 谱面可能导致解析器出现异常
    - 在 Linux/... 系统中，字体不存在会导致崩溃
    - 当使用b20220101星数计算逻辑，开启DT时星数计算出错
    - 死亡时note会瞬移
    - ...
- 修复反复暂停时时间异常的问题
- 修复多K轨道分隔线异常显示的问题
- 修复音量调节100和50人耳几乎听不出区别的问题
- 新增暂停后恢复游戏的倒计时
- 新增11K-18K支持，设置内的Lane Color设置弃用，改为配置文件内手动修改颜色 hex 值
- 新增 SoundVoltex 谱面读取
- 新增 EZ2AC FNEX 谱面, key音读取
- 新增谱面导出为 *.osz 格式



### Mania Player v0.0.7

- 修复设置界面部分元件显示在滚动时会超出窗口的问题
- 修复设置界面往下可以无限滚动的问题
- 修复 DJMAX RESPECT 谱面变速错误处理的问题
- 修复 DJMAX RESPECT 部分BGM音频不遵守命名规范时无法识别的问题
- 修复 EZ2ON REBOOT:R 谱面导出的 Flac 音频文件 osu! 无法读取的问题(会转码为wav)
- 新增 DJMAX RESPECT V 谱面读取支持(谱面文件不公开提供！)
- 新增 EZ2ON REBOOT:R 谱面，key音读取(谱面文件不公开提供！)
- 新增 DJMAX RESPECT 谱面导出时支持去除 FX/SIDE note



## Building

### 1. Download Dependencies

The following libraries are **not included** in the repository and must be downloaded manually.

#### BASS Audio Library (required)

BASS is a commercial library, free for non-commercial use. Download from [un4seen.com](https://www.un4seen.com/):

| Library | Download | Files Needed |
|---------|----------|--------------|
| BASS | [bass24](https://www.un4seen.com/download.php?bass24) | `bass.h`, `bass.lib`, `bass.dll` |
| BASS_FX | [bass_fx24](https://www.un4seen.com/download.php?bass_fx24) | `bass_fx.h`, `bass_fx.lib`, `bass_fx.dll` |

Optional (for low-latency WASAPI/ASIO output):

| Library | Download | Files Needed |
|---------|----------|--------------|
| BASSmix | [bassmix24](https://www.un4seen.com/download.php?bassmix24) | `bassmix.h`, `bassmix.lib`, `bassmix.dll` |
| BASSWASAPI | [basswasapi24](https://www.un4seen.com/download.php?basswasapi24) | `basswasapi.h`, `basswasapi.lib`, `basswasapi.dll` |
| BASSASIO | [bassasio](https://www.un4seen.com/download.php?bassasio) | `bassasio.h`, `bassasio.lib`, `bassasio.dll` |
| BASSASIO | [bassflac](https://www.un4seen.com/download.php?bassflac) | `bassflac.h`, `bassflac.lib`, `bassflac.dll` |

Place files as follows:
```
include/bass/        <- All .h header files
lib/x64/             <- All .lib files (Windows x64)
```
Place `.dll` files next to the built executable.

#### Other Libraries (Windows)

These are also needed in `lib/x64/`:

| Library | Download | Files Needed |
|---------|----------|--------------|
| SDL3 | [GitHub Releases](https://github.com/libsdl-org/SDL/releases) | `SDL3.lib`, `SDL3.dll` |
| SDL3_ttf | [GitHub Releases](https://github.com/libsdl-org/SDL_ttf/releases) | `SDL3_ttf.lib`, `SDL3_ttf.dll` |
| ICU | [GitHub Releases](https://github.com/unicode-org/icu/releases) | `icuuc.lib`, `icuin.lib` + DLLs |
| FFmpeg | [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (shared build) | `avcodec.lib`, `avformat.lib`, `avutil.lib`, `swscale.lib`, `swresample.lib` + DLLs |

### 2. Build

#### Windows (MSVC, Visual Studio 2022+)

```batch
# Using build.bat (direct MSVC compilation)
build.bat

# Or using CMake
mkdir build && cd build
cmake .. -G "Visual Studio xx xxxx" -A x64
cmake --build . --config Release
```

Output: `build/bin/mania_player.exe`

#### Linux

Install dependencies:
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake pkg-config
sudo apt install libicu-dev
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev

# SDL3 and SDL3_ttf must be built from source
# https://github.com/libsdl-org/SDL
# https://github.com/libsdl-org/SDL_ttf

# BASS and BASS_FX: download Linux versions from un4seen.com
# Place libbass.so and libbass_fx.so in /usr/local/lib
```

Build:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Output: `build/bin/mania_player`

#### macOS

macOS support is experimental. You'll need to install dependencies via Homebrew or build from source.

### 3. Run

Run from the project root directory (where `Songs/` folder is located).

Place songs in the `Songs/` folder. See the [wiki](https://github.com/4accccc/Cpp_Mania_Player/wiki/Songs-folder-structure) for folder structure.



## Disclaimer

This project is for educational and personal use only.
The developers are not responsible for any misuse of this software.
All game assets and charts belong to their respective owners.



## License
````
This project is licensed under the GPL-3.0 License - see the LICENSE file for details.

Note: BASS/BASS_FX are commercial libraries free for non-commercial use and must be downloaded separately from https://www.un4seen.com/
````
