#include "config_manager.h"

#include "imgui.h"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ConfigManager::initialize(const std::string &configDir)
{
    configDir_ = configDir;
    loadFromFile();
}

void ConfigManager::shutdown()
{
    if (dirty_) {
        saveToFile();
    }
}

// ---------------------------------------------------------------------------
// Autosave (debounced)
// ---------------------------------------------------------------------------

void ConfigManager::autosave()
{
    if (!dirty_) return;

    double now = ImGui::GetTime();
    if (lastDirtyTime_ == 0.0) {
        lastDirtyTime_ = now;
        return;
    }

    if (now - lastDirtyTime_ >= kAutosaveDelay) {
        saveToFile();
        lastDirtyTime_ = 0.0;
    }
}

// ---------------------------------------------------------------------------
// Key-value access
// ---------------------------------------------------------------------------

std::string ConfigManager::get(const std::string &key,
                                const std::string &defaultVal) const
{
    auto it = data_.find(key);
    return (it != data_.end()) ? it->second : defaultVal;
}

void ConfigManager::set(const std::string &key, const std::string &value)
{
    auto it = data_.find(key);
    if (it != data_.end() && it->second == value) return;
    data_[key] = value;
    dirty_ = true;
    lastDirtyTime_ = 0.0;  // reset debounce timer
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

std::string ConfigManager::configFilePath() const
{
    return configDir_ + "/config.ini";
}

bool ConfigManager::loadFromFile()
{
    std::string path = configFilePath();
    std::ifstream file(path);
    if (!file.good()) return false;

    std::string line;
    while (std::getline(file, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                                  line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        data_[line.substr(0, eq)] = line.substr(eq + 1);
    }

    return true;
}

bool ConfigManager::saveToFile()
{
    std::string path = configFilePath();

    // Build content
    std::string content;
    content += "[Config]\n";
    for (const auto &pair : data_) {
        content += pair.first + "=" + pair.second + "\n";
    }

    // Skip write if content hasn't changed
    {
        std::ifstream existing(path);
        if (existing.good()) {
            std::string oldContent((std::istreambuf_iterator<char>(existing)),
                                    std::istreambuf_iterator<char>());
            if (oldContent == content) {
                dirty_ = false;
                return true;
            }
        }
    }

    std::ofstream file(path);
    if (!file.good()) return false;
    file << content;
    dirty_ = false;
    return true;
}
