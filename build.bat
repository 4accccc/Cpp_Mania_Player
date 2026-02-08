@echo off
setlocal

:: Kill running instance before rebuild
taskkill /F /IM mania_player.exe 2>nul

set "VS2022=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "VS2026=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat"

if exist "%VS2022%" (
    call "%VS2022%" x64
) else (
    call "%VS2026%" x64
)

echo Compiling...
cl /EHsc /std:c++17 /O2 /utf-8 /wd4819 ^
    /I"src\core" ^
    /I"src\parsers" ^
    /I"src\graphics" ^
    /I"src\audio" ^
    /I"src\systems" ^
    /I"include" ^
    /I"include\bass" ^
    /I"include\icu" ^
    /I"include\ffmpeg" ^
    /I"third_party\lzma" ^
    /I"third_party\minilzo" ^
    src\core\*.cpp ^
    src\parsers\*.cpp ^
    src\graphics\*.cpp ^
    src\audio\*.cpp ^
    src\systems\*.cpp ^
    third_party\lzma\LzmaEnc.c ^
    third_party\lzma\LzmaDec.c ^
    third_party\lzma\LzFind.c ^
    third_party\lzma\CpuArch.c ^
    third_party\lzma\LzFindMt.c ^
    third_party\lzma\Threads.c ^
    third_party\lzma\LzFindOpt.c ^
    third_party\minilzo\minilzo.c ^
    /Fe:mania_player.exe ^
    /link ^
    /LIBPATH:"lib\x64" ^
    SDL3.lib SDL3_ttf.lib bass.lib bass_fx.lib ^
    icuuc.lib icuin.lib ^
    avcodec.lib avformat.lib avutil.lib swscale.lib ^
    shell32.lib Advapi32.lib comdlg32.lib ^
    /SUBSYSTEM:WINDOWS

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    del *.obj 2>nul
) else (
    echo Build failed!
)
