#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

struct PakFileEntry {
    std::string filename;
    uint32_t uncompressedSize;
    uint32_t crc32;
    uint8_t cryptKeyIndex;
    uint32_t fileBlockSize;
    size_t dataOffset;      // Offset in pak file to encrypted data
    uint32_t cryptChunkSize;
    uint32_t rsaDecryptedSize;
};

class PakExtractor {
public:
    PakExtractor();
    ~PakExtractor();

    // Load keys from directory
    bool loadKeys(const std::string& keyDir);

    // Open and parse a .pak file
    bool open(const std::string& pakPath);

    // Close current pak file
    void close();

    // Check if a file exists in the pak
    bool hasFile(const std::string& filename) const;

    // Extract a file to memory
    bool extractFile(const std::string& filename, std::vector<uint8_t>& outData);

    // Extract all files to a directory
    bool extractAll(const std::string& outDir);

    // Get list of files in pak
    const std::vector<PakFileEntry>& getFileList() const { return files_; }

    // Check if pak is open
    bool isOpen() const { return isOpen_; }

private:
    // RSA-like decryption
    uint32_t rsaDecrypt8to4(uint64_t input, uint64_t key1, uint64_t key2);
    std::vector<uint8_t> xipRsaDecrypt(const uint8_t* data, size_t size, uint8_t keyIndex);

    // Japanese text XOR decryption
    std::vector<uint8_t> japUnXor(const uint8_t* data, size_t size, int keyOffset);

    // Parse file header
    bool parseFileHeader(const uint8_t* data, size_t offset, int xorParam, PakFileEntry& entry);

    // Extract single file entry
    bool extractEntry(const PakFileEntry& entry, std::vector<uint8_t>& outData);

    // Key data
    std::vector<uint8_t> key1a_;
    std::vector<uint8_t> key1b_;
    bool keysLoaded_;

    // Pak file data
    std::vector<uint8_t> pakData_;
    std::vector<PakFileEntry> files_;
    std::unordered_map<std::string, size_t> fileIndex_;
    bool isOpen_;
    std::string currentPakPath_;

    // Constants
    static constexpr size_t FILE_HEADER_LENGTH = 0x11c;
    static constexpr size_t FILE_NAME_LENGTH = 0x104;
};
