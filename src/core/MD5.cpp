#include "MD5.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <iomanip>

// MD5 constants
static const uint32_t S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

MD5::MD5() : count(0), finalized(false) {
    state[0] = 0x67452301;
    state[1] = 0xefcdab89;
    state[2] = 0x98badcfe;
    state[3] = 0x10325476;
    memset(buffer, 0, 64);
    memset(digest, 0, 16);
}

void MD5::transform(const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];

    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
    }

    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5*i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3*i + 5) % 16;
        } else {
            f = c ^ (b | (~d));
            g = (7*i) % 16;
        }
        uint32_t temp = d;
        d = c;
        c = b;
        b = b + ROTL((a + f + K[i] + M[g]), S[i]);
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void MD5::update(const uint8_t* data, size_t length) {
    size_t index = (count / 8) % 64;
    count += length * 8;

    size_t firstPart = 64 - index;
    size_t i = 0;

    if (length >= firstPart) {
        memcpy(&buffer[index], data, firstPart);
        transform(buffer);

        for (i = firstPart; i + 63 < length; i += 64) {
            transform(&data[i]);
        }
        index = 0;
    }

    memcpy(&buffer[index], &data[i], length - i);
}

void MD5::finalize() {
    if (finalized) return;

    uint8_t padding[64] = {0x80};
    uint8_t bits[8];

    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)(count >> (i * 8));
    }

    size_t index = (count / 8) % 64;
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);

    update(padding, padLen);
    update(bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)(state[i]);
        digest[i*4+1] = (uint8_t)(state[i] >> 8);
        digest[i*4+2] = (uint8_t)(state[i] >> 16);
        digest[i*4+3] = (uint8_t)(state[i] >> 24);
    }

    finalized = true;
}

std::string MD5::hexdigest() const {
    if (!finalized) return "";
    std::ostringstream oss;
    for (int i = 0; i < 16; i++) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];
    }
    return oss.str();
}

std::string MD5::hash(const uint8_t* data, size_t length) {
    MD5 md5;
    md5.update(data, length);
    md5.finalize();
    return md5.hexdigest();
}

std::string MD5::hashFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return "";

    MD5 md5;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        md5.update((uint8_t*)buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        md5.update((uint8_t*)buffer, file.gcount());
    }
    md5.finalize();
    return md5.hexdigest();
}
