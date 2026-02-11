#include "SongIndex.h"
#include <fstream>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

// Index file version (increment when format changes)
static const int INDEX_VERSION = 6;

std::string SongIndex::getIndexDir() {
    return (fs::path("Data") / "Index").string();
}

std::string SongIndex::hashPath(const std::string& path) {
    // Simple hash using std::hash
    std::hash<std::string> hasher;
    size_t h = hasher(path);
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

std::string SongIndex::getIndexPath(const std::string& folderPath) {
    std::string indexDir = getIndexDir();
    if (!fs::exists(indexDir)) {
        fs::create_directories(indexDir);
    }
    return (fs::path(indexDir) / (hashPath(folderPath) + ".idx")).string();
}

int64_t SongIndex::getFolderModTime(const std::string& folderPath) {
    try {
        auto ftime = fs::last_write_time(folderPath);
        // C++17 compatible: use time_since_epoch directly
        auto duration = ftime.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    } catch (...) {
        return 0;
    }
}

void SongIndex::writeString(std::ofstream& f, const std::string& s) {
    uint32_t len = (uint32_t)s.size();
    f.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) {
        f.write(s.data(), len);
    }
}

std::string SongIndex::readString(std::ifstream& f) {
    uint32_t len = 0;
    f.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (len == 0 || len > 10000) return "";
    std::string s(len, '\0');
    f.read(&s[0], len);
    return s;
}

bool SongIndex::isIndexValid(const std::string& folderPath) {
    std::string indexPath = getIndexPath(folderPath);
    if (!fs::exists(indexPath)) return false;

    std::ifstream f(indexPath, std::ios::binary);
    if (!f) return false;

    // Read version
    int version = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != INDEX_VERSION) return false;

    // Read stored mod time
    int64_t storedModTime = 0;
    f.read(reinterpret_cast<char*>(&storedModTime), sizeof(storedModTime));

    // Compare with current mod time
    int64_t currentModTime = getFolderModTime(folderPath);
    return storedModTime == currentModTime;
}

bool SongIndex::loadIndex(const std::string& folderPath, CachedSong& song) {
    std::string indexPath = getIndexPath(folderPath);
    std::ifstream f(indexPath, std::ios::binary);
    if (!f) return false;

    // Read version
    int version = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != INDEX_VERSION) return false;

    // Read mod time
    f.read(reinterpret_cast<char*>(&song.lastModified), sizeof(song.lastModified));

    // Read song info
    song.folderPath = readString(f);
    song.folderName = readString(f);
    song.title = readString(f);
    song.artist = readString(f);
    song.backgroundPath = readString(f);
    song.audioPath = readString(f);
    f.read(reinterpret_cast<char*>(&song.previewTime), sizeof(song.previewTime));
    f.read(reinterpret_cast<char*>(&song.source), sizeof(song.source));

    // Read difficulties
    uint32_t diffCount = 0;
    f.read(reinterpret_cast<char*>(&diffCount), sizeof(diffCount));
    song.difficulties.clear();
    song.difficulties.reserve(diffCount);

    for (uint32_t i = 0; i < diffCount; i++) {
        CachedDifficulty diff;
        diff.path = readString(f);
        diff.version = readString(f);
        diff.creator = readString(f);
        diff.hash = readString(f);
        diff.backgroundPath = readString(f);
        diff.audioPath = readString(f);
        f.read(reinterpret_cast<char*>(&diff.keyCount), sizeof(diff.keyCount));
        f.read(reinterpret_cast<char*>(&diff.previewTime), sizeof(diff.previewTime));
        f.read(reinterpret_cast<char*>(diff.starRatings), sizeof(diff.starRatings));
        song.difficulties.push_back(diff);
    }

    return f.good();
}

bool SongIndex::saveIndex(const CachedSong& song) {
    std::string indexPath = getIndexPath(song.folderPath);
    std::ofstream f(indexPath, std::ios::binary);
    if (!f) return false;

    // Write version
    int version = INDEX_VERSION;
    f.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write mod time
    f.write(reinterpret_cast<const char*>(&song.lastModified), sizeof(song.lastModified));

    // Write song info
    writeString(f, song.folderPath);
    writeString(f, song.folderName);
    writeString(f, song.title);
    writeString(f, song.artist);
    writeString(f, song.backgroundPath);
    writeString(f, song.audioPath);
    f.write(reinterpret_cast<const char*>(&song.previewTime), sizeof(song.previewTime));
    f.write(reinterpret_cast<const char*>(&song.source), sizeof(song.source));

    // Write difficulties
    uint32_t diffCount = (uint32_t)song.difficulties.size();
    f.write(reinterpret_cast<const char*>(&diffCount), sizeof(diffCount));

    for (const auto& diff : song.difficulties) {
        writeString(f, diff.path);
        writeString(f, diff.version);
        writeString(f, diff.creator);
        writeString(f, diff.hash);
        writeString(f, diff.backgroundPath);
        writeString(f, diff.audioPath);
        f.write(reinterpret_cast<const char*>(&diff.keyCount), sizeof(diff.keyCount));
        f.write(reinterpret_cast<const char*>(&diff.previewTime), sizeof(diff.previewTime));
        f.write(reinterpret_cast<const char*>(diff.starRatings), sizeof(diff.starRatings));
    }

    return f.good();
}
