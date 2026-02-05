#pragma once
#include <cstdint>
#include <cmath>

// osu! LegacyRandom implementation (Xorshift algorithm)
// Used for deterministic beatmap conversion
class LegacyRandom {
private:
    static constexpr double INT_TO_REAL = 1.0 / 2147483648.0;  // 1.0 / (int.MaxValue + 1.0)
    static constexpr uint32_t INT_MASK = 0x7FFFFFFF;

    uint32_t x, y, z, w;

public:
    LegacyRandom(int seed = 0) {
        x = static_cast<uint32_t>(seed);
        y = 842502087U;
        z = 3579807591U;
        w = 273326509U;
    }

    uint32_t nextUInt() {
        uint32_t t = x ^ (x << 11);
        x = y;
        y = z;
        z = w;
        return w = w ^ (w >> 19) ^ t ^ (t >> 8);
    }

    int next() {
        return static_cast<int>(INT_MASK & nextUInt());
    }

    int next(int upperBound) {
        return static_cast<int>(nextDouble() * upperBound);
    }

    int next(int lowerBound, int upperBound) {
        return static_cast<int>(lowerBound + nextDouble() * (upperBound - lowerBound));
    }

    double nextDouble() {
        return INT_TO_REAL * next();
    }
};
