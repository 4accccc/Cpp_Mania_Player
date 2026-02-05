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
cl /EHsc /std:c++17 /O2 ^
    /I"D:\work\SDL3-3.2.30\include" ^
    /I"D:\work\SDL3_ttf-3.2.0\include" ^
    /I"D:\work\SDL3_mixer-3.1.2\include" ^
    /I"D:\work\lzma2501\C\C" ^
    src\*.cpp ^
    "D:\work\lzma2501\C\C\LzmaEnc.c" ^
    "D:\work\lzma2501\C\C\LzmaDec.c" ^
    "D:\work\lzma2501\C\C\LzFind.c" ^
    "D:\work\lzma2501\C\C\CpuArch.c" ^
    "D:\work\lzma2501\C\C\LzFindMt.c" ^
    "D:\work\lzma2501\C\C\Threads.c" ^
    "D:\work\lzma2501\C\C\LzFindOpt.c" ^
    /Fe:D:\work\Release\mania_player.exe ^
    /link ^
    /LIBPATH:"D:\work\SDL3-3.2.30\lib\x64" ^
    /LIBPATH:"D:\work\SDL3_ttf-3.2.0\lib\x64" ^
    /LIBPATH:"D:\work\SDL3_mixer-3.1.2\lib\x64" ^
    SDL3.lib SDL3_ttf.lib SDL3_mixer.lib ^
    shell32.lib Advapi32.lib comdlg32.lib ^
    /SUBSYSTEM:CONSOLE

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    del *.obj 2>nul
) else (
    echo Build failed!
)
