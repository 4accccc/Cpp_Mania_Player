#pragma once

// osu! Mods bit flags (from osu!stable source)
// These values can be combined using bitwise OR

namespace OsuMods {
    constexpr int None         = 0;
    constexpr int NoFail       = 1;
    constexpr int Easy         = 2;
    constexpr int TouchDevice  = 4;
    constexpr int Hidden       = 8;
    constexpr int HardRock     = 16;
    constexpr int SuddenDeath  = 32;
    constexpr int DoubleTime   = 64;
    constexpr int Relax        = 128;
    constexpr int HalfTime     = 256;
    constexpr int Nightcore    = 512;
    constexpr int Flashlight   = 1024;
    constexpr int Autoplay     = 2048;
    constexpr int SpunOut      = 4096;
    constexpr int Relax2       = 8192;      // Autopilot
    constexpr int Perfect      = 16384;
    constexpr int Key4         = 32768;
    constexpr int Key5         = 65536;
    constexpr int Key6         = 131072;
    constexpr int Key7         = 262144;
    constexpr int Key8         = 524288;
    constexpr int FadeIn       = 1048576;
    constexpr int Random       = 2097152;
    constexpr int Cinema       = 4194304;
    constexpr int Target       = 8388608;
    constexpr int Key9         = 16777216;
    constexpr int KeyCoop      = 33554432;
    constexpr int Key1         = 67108864;
    constexpr int Key3         = 134217728;
    constexpr int Key2         = 268435456;
    constexpr int ScoreV2      = 536870912;
    constexpr int Mirror       = 1073741824;

    // Combined masks
    constexpr int KeyMod           = 521109504;   // Key1|Key2|Key3|Key4|Key5|Key6|Key7|Key8|Key9|KeyCoop
    constexpr int FreeModAllowed   = 1595913403;
    constexpr int ScoreIncreaseMods = 1049688;    // HD|HR|DT|FL|FI
}
