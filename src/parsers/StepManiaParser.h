#pragma once
#include "OsuParser.h"
#include <string>
#include <vector>

// StepMania (.sm/.ssc) parser
// Supports dance-single (4K), dance-double (8K), pump-single (5K), etc.

class StepManiaParser {
public:
    // Check if file is a StepMania chart
    static bool isStepManiaFile(const std::string& path);

    // Parse .sm or .ssc file into BeatmapInfo
    // difficultyIndex: which difficulty to load (0-based)
    // If -1, loads the first available difficulty
    static bool parse(const std::string& path, BeatmapInfo& info, int difficultyIndex = -1);

    // Get list of available difficulties in the file
    struct DifficultyInfo {
        std::string stepsType;      // e.g., "dance-single"
        std::string difficulty;     // e.g., "Hard", "Challenge"
        int meter;                  // difficulty rating
        int keyCount;               // mapped key count
    };
    static std::vector<DifficultyInfo> getDifficulties(const std::string& path);

    // Map StepMania steps type to key count
    static int stepsTypeToKeyCount(const std::string& stepsType);

private:
    // BPM change point
    struct BPMChange {
        double beat;    // beat number (0-based)
        double bpm;
    };

    // Stop (pause) point
    struct StopPoint {
        double beat;
        double duration;  // in seconds
    };

    // Parse SM format (older)
    static bool parseSM(const std::string& content, const std::string& path,
                        BeatmapInfo& info, int difficultyIndex);

    // Parse SSC format (newer)
    static bool parseSSC(const std::string& content, const std::string& path,
                         BeatmapInfo& info, int difficultyIndex);

    // Parse note data string into notes
    static bool parseNoteData(const std::string& noteData, int keyCount,
                              const std::vector<BPMChange>& bpms,
                              const std::vector<StopPoint>& stops,
                              double offset,
                              std::vector<Note>& notes);

    // Convert beat to milliseconds considering BPM changes and stops
    static double beatToMs(double beat, const std::vector<BPMChange>& bpms,
                           const std::vector<StopPoint>& stops, double offset);

    // Parse BPM string (e.g., "0.000=120.000,4.000=140.000")
    static std::vector<BPMChange> parseBPMs(const std::string& bpmStr);

    // Parse stops string
    static std::vector<StopPoint> parseStops(const std::string& stopStr);

    // Helper to trim whitespace
    static std::string trim(const std::string& str);

    // Helper to get tag value from content
    static std::string getTagValue(const std::string& content, const std::string& tag);
};
