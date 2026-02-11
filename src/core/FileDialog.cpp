#include "FileDialog.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#ifdef __APPLE__
// macOS uses Cocoa - implemented in FileDialog.mm
#endif

#ifdef __linux__
#include <cstdio>
#include <cstdlib>
#endif

namespace FileDialog {

#ifdef _WIN32
// Windows implementation
std::string openFile(const char* title, const char* defaultPath,
                     const std::vector<const char*>& filterPatterns,
                     const char* filterDescription) {
    wchar_t filename[MAX_PATH] = L"";

    // Build filter string
    std::wstring filter;
    if (filterDescription && !filterPatterns.empty()) {
        // Convert description to wide string
        int descLen = MultiByteToWideChar(CP_UTF8, 0, filterDescription, -1, nullptr, 0);
        std::wstring wDesc(descLen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, filterDescription, -1, &wDesc[0], descLen);
        filter = wDesc + L'\0';

        // Add patterns
        for (size_t i = 0; i < filterPatterns.size(); i++) {
            int patLen = MultiByteToWideChar(CP_UTF8, 0, filterPatterns[i], -1, nullptr, 0);
            std::wstring wPat(patLen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, filterPatterns[i], -1, &wPat[0], patLen);
            filter += wPat;
            if (i < filterPatterns.size() - 1) filter += L';';
        }
        filter += L'\0';
    }
    filter += L"All Files\0*.*\0";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);
        return result;
    }
    return "";
}

std::string saveFile(const char* title, const char* defaultPath,
                     const std::vector<const char*>& filterPatterns,
                     const char* filterDescription) {
    wchar_t filename[MAX_PATH] = L"";

    // Set default filename if provided
    if (defaultPath) {
        MultiByteToWideChar(CP_UTF8, 0, defaultPath, -1, filename, MAX_PATH);
    }

    // Build filter string
    std::wstring filter;
    std::wstring defExt;
    if (filterDescription && !filterPatterns.empty()) {
        int descLen = MultiByteToWideChar(CP_UTF8, 0, filterDescription, -1, nullptr, 0);
        std::wstring wDesc(descLen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, filterDescription, -1, &wDesc[0], descLen);
        filter = wDesc + L'\0';

        for (size_t i = 0; i < filterPatterns.size(); i++) {
            int patLen = MultiByteToWideChar(CP_UTF8, 0, filterPatterns[i], -1, nullptr, 0);
            std::wstring wPat(patLen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, filterPatterns[i], -1, &wPat[0], patLen);
            filter += wPat;
            if (i < filterPatterns.size() - 1) filter += L';';

            // Extract default extension from first pattern
            if (i == 0 && wPat.size() > 2) {
                size_t dotPos = wPat.find(L'.');
                if (dotPos != std::wstring::npos) {
                    defExt = wPat.substr(dotPos + 1);
                }
            }
        }
        filter += L'\0';
    }
    filter += L"All Files\0*.*\0";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt.empty() ? nullptr : defExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&ofn)) {
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);
        return result;
    }
    return "";
}

std::string selectFolder(const char* title) {
    BROWSEINFOW bi = {};
    wchar_t wTitle[256] = L"Select Folder";
    if (title) {
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wTitle, 256);
    }
    bi.lpszTitle = wTitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            std::string result(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], size, nullptr, nullptr);
            return result;
        }
        CoTaskMemFree(pidl);
    }
    return "";
}

#endif // _WIN32

#ifdef __linux__
// Linux implementation using zenity/kdialog
std::string openFile(const char* title, const char* defaultPath,
                     const std::vector<const char*>& filterPatterns,
                     const char* filterDescription) {
    std::string cmd = "zenity --file-selection";
    if (title) {
        cmd += " --title=\"";
        cmd += title;
        cmd += "\"";
    }
    if (!filterPatterns.empty()) {
        cmd += " --file-filter=\"";
        if (filterDescription) {
            cmd += filterDescription;
            cmd += " |";
        }
        for (const auto& pat : filterPatterns) {
            cmd += " ";
            cmd += pat;
        }
        cmd += "\"";
    }
    cmd += " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[1024];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

std::string saveFile(const char* title, const char* defaultPath,
                     const std::vector<const char*>& filterPatterns,
                     const char* filterDescription) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite";
    if (title) {
        cmd += " --title=\"";
        cmd += title;
        cmd += "\"";
    }
    if (defaultPath) {
        cmd += " --filename=\"";
        cmd += defaultPath;
        cmd += "\"";
    }
    cmd += " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[1024];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

std::string selectFolder(const char* title) {
    std::string cmd = "zenity --file-selection --directory";
    if (title) {
        cmd += " --title=\"";
        cmd += title;
        cmd += "\"";
    }
    cmd += " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[1024];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}
#endif // __linux__

} // namespace FileDialog

