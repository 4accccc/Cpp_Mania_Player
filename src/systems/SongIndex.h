#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Number of star rating versions we store
constexpr int STAR_RATING_VERSION_COUNT = 2;

// Cached difficulty info
struct CachedDifficulty {
    std::string path;
    std::string version;
    std::string creator;
    std::string hash;  // MD5 hash of beatmap file
    int keyCount;
    // Star ratings for each algorithm version:
    // [0] = b20260101, [1] = b20220101
    double starRatings[STAR_RATING_VERSION_COUNT];
};

// Cached song entry
struct CachedSong {
    std::string folderPath;
    std::string folderName;
    std::string title;
    std::string artist;
    std::string backgroundPath;
    std::string audioPath;
    int previewTime;
    int source;  // 0=Osu, 1=DJMax, 2=O2Jam
    int64_t lastModified;  // Folder modification time
    std::vector<CachedDifficulty> difficulties;
};

class SongIndex {
public:
    // Get index directory path
    static std::string getIndexDir();

    // Get index file path for a song folder
    static std::string getIndexPath(const std::string& folderPath);

    // Check if index is valid (folder not modified)
    static bool isIndexValid(const std::string& folderPath);

    // Load cached song from index
    static bool loadIndex(const std::string& folderPath, CachedSong& song);

    // Save song to index
    static bool saveIndex(const CachedSong& song);

    // Get folder modification time
    static int64_t getFolderModTime(const std::string& folderPath);

private:
    // Simple hash for folder path
    static std::string hashPath(const std::string& path);

    // Write/read helpers
    static void writeString(std::ofstream& f, const std::string& s);
    static std::string readString(std::ifstream& f);
};
