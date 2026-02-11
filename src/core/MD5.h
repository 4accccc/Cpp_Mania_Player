#pragma once
#include <string>
#include <cstdint>

// Cross-platform MD5 implementation
class MD5 {
public:
    MD5();
    void update(const uint8_t* data, size_t length);
    void finalize();
    std::string hexdigest() const;

    // Convenience function
    static std::string hash(const uint8_t* data, size_t length);
    static std::string hashFile(const std::string& filepath);

private:
    void transform(const uint8_t block[64]);

    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
    uint8_t digest[16];
    bool finalized;
};
