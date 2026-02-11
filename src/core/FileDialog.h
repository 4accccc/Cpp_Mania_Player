#pragma once
#include <string>
#include <vector>

// Cross-platform file dialog wrapper
namespace FileDialog {
    // Open file dialog - returns empty string if cancelled
    std::string openFile(const char* title, const char* defaultPath,
                         const std::vector<const char*>& filterPatterns,
                         const char* filterDescription);

    // Save file dialog - returns empty string if cancelled
    std::string saveFile(const char* title, const char* defaultPath,
                         const std::vector<const char*>& filterPatterns,
                         const char* filterDescription);

    // Select folder dialog - returns empty string if cancelled
    std::string selectFolder(const char* title);
}
