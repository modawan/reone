/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/menupresentation.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace reone {

namespace game {

namespace {

// Configuration sections. Keys before the first section header belong to the
// top level, named here by an empty section.
const std::string kTopLevel;
const std::string kGameOptionsSection {"Game Options"};
const std::string kAutoPauseSection {"Autopause Options"};

// Top-level keys of the main menu's presentation.
const std::string kMenuSelectorKey {"k2-menu-selector"};
const std::string kMenuGenderKey {"k2-menu-gender"};
const std::string kMenuAppearanceKey {"k2-menu-appearance"};
const std::string kMenuBodyKey {"k2-menu-body"};
const std::string kMenuTextureKey {"k2-menu-texture"};

// The Game Options keys of the feedback options.
const std::array<std::pair<std::string_view, bool FeedbackOptions::*>, 9> kFeedbackKeys {{
    {"Hide Unequippable", &FeedbackOptions::hideUnequippable},
    {"Tutorial Popups", &FeedbackOptions::tutorialPopups},
    {"Subtitles", &FeedbackOptions::subtitles},
    {"Mini Map", &FeedbackOptions::miniMap},
    {"Floating Numbers", &FeedbackOptions::floatingNumbers},
    {"Status Summary", &FeedbackOptions::statusSummary},
    {"Use Small Fonts", &FeedbackOptions::smallFonts},
    {"Hide InGame GUI", &FeedbackOptions::hideInGameGui},
    {"Enable Tooltips", &FeedbackOptions::tooltips},
}};

// The Autopause Options keys.
const std::array<std::pair<std::string_view, bool AutoPauseOptions::*>, 6> kAutoPauseKeys {{
    {"End Of Combat Round", &AutoPauseOptions::endOfCombatRound},
    {"Enemy Sighted", &AutoPauseOptions::enemySighted},
    {"Mine Sighted", &AutoPauseOptions::mineSighted},
    {"Party Killed", &AutoPauseOptions::partyKilled},
    {"Action Menu", &AutoPauseOptions::actionMenu},
    {"New Target Selected", &AutoPauseOptions::newTargetSelected},
}};

std::string trim(const std::string &s) {
    auto begin = s.find_first_not_of(" \t\r");
    return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

/** A key and the number to write for it; no number removes the key. */
using ConfigurationValue = std::pair<std::string, std::optional<int>>;

/**
 * The game's configuration file, kept line by line: the game reads and writes
 * its own keys, and every other line, comment and section is written back
 * unchanged.
 */
class ConfigurationFile {
public:
    /** The configuration at path; a host without one (an empty path) or a file not yet written reads as empty. */
    static ConfigurationFile read(const std::filesystem::path &path) {
        ConfigurationFile result;
        if (path.empty()) {
            return result;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            if (std::filesystem::exists(path)) {
                throw std::runtime_error("Cannot read configuration: " + path.string());
            }
            return result;
        }
        const std::string content {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::istringstream lines(content);
        for (std::string line; std::getline(lines, line);) {
            result._lines.push_back(std::move(line));
        }
        result._endsWithNewline = !content.empty() && content.back() == '\n';
        return result;
    }

    /** The number of a key in a section, as last set there; none when the key is absent or holds no number. */
    std::optional<int> number(const std::string &section, const std::string &key) const {
        std::optional<int> result;
        std::string current;
        for (const auto &line : _lines) {
            if (auto header = sectionHeader(line)) {
                current = *header;
                continue;
            }
            if (current != section || keyOf(line) != key) {
                continue;
            }
            std::istringstream value(line.substr(line.find('=') + 1));
            int number;
            if (value >> number) {
                result = number;
            }
        }
        return result;
    }

    /** Whether a key in a section is on: any number but zero. None when the key is absent. */
    std::optional<bool> flag(const std::string &section, const std::string &key) const {
        auto value = number(section, key);
        return value ? std::optional<bool>(*value != 0) : std::nullopt;
    }

    /**
     * Replace these keys of a section. The section's keys are written together
     * at its start, the top level's at the start of the file; a section the
     * file lacks is added at its end.
     */
    void set(const std::string &section, const std::vector<ConfigurationValue> &values) {
        std::vector<std::string> kept;
        std::string current;
        for (auto &line : _lines) {
            if (auto header = sectionHeader(line)) {
                current = *header;
            } else if (current == section && isOneOf(keyOf(line), values)) {
                continue;
            }
            kept.push_back(std::move(line));
        }
        _lines = std::move(kept);

        std::vector<std::string> written;
        for (const auto &[key, value] : values) {
            if (value) {
                written.push_back(key + "=" + std::to_string(*value));
            }
        }
        auto position = _lines.begin();
        if (!section.empty()) {
            position = std::find_if(_lines.begin(), _lines.end(), [&section](const std::string &line) {
                return sectionHeader(line) == section;
            });
            if (position == _lines.end()) {
                _lines.push_back("[" + section + "]");
                _lines.insert(_lines.end(), written.begin(), written.end());
                _endsWithNewline = true;
                return;
            }
            ++position;
        }
        if (_lines.empty()) {
            _endsWithNewline = true;
        }
        _lines.insert(position, written.begin(), written.end());
    }

    /**
     * Write the configuration through a sibling file, so that a failed write
     * never truncates the live configuration. A host without a configuration
     * (an empty path) keeps its options in memory only.
     */
    void write(const std::filesystem::path &path) const {
        if (path.empty()) {
            return;
        }
        std::string content;
        for (size_t i = 0; i < _lines.size(); ++i) {
            content += _lines[i];
            if (i + 1 < _lines.size() || _endsWithNewline) {
                content += '\n';
            }
        }
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << content;
        file.close();
        if (!file) {
            throw std::runtime_error("Cannot write configuration: " + temporary.string());
        }
        std::filesystem::rename(temporary, path);
    }

private:
    std::vector<std::string> _lines;
    bool _endsWithNewline {false};

    /** The section a header line opens; none for any other line. */
    static std::optional<std::string> sectionHeader(const std::string &line) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() != '[') {
            return std::nullopt;
        }
        const auto end = trimmed.back() == ']' ? trimmed.size() - 1 : trimmed.size();
        return trimmed.substr(1, end - 1);
    }

    /** The key a line sets; empty for a line that sets none. */
    static std::string keyOf(const std::string &line) {
        const auto separator = line.find('=');
        return separator == std::string::npos ? std::string() : trim(line.substr(0, separator));
    }

    static bool isOneOf(const std::string &key, const std::vector<ConfigurationValue> &values) {
        return !key.empty() && std::any_of(values.begin(), values.end(), [&key](const ConfigurationValue &value) {
            return value.first == key;
        });
    }
};

} // namespace

std::string MenuPresentation::modelResRef(bool tsl) const {
    if (!tsl) return "mainmenu";
    return "mainmenu0" + std::to_string(validateSelector(selector) + 1);
}

MenuPresentation MenuPresentation::load(const std::filesystem::path &path) {
    const auto configuration = ConfigurationFile::read(path);
    MenuPresentation result;
    result.selector = validateSelector(configuration.number(kTopLevel, kMenuSelectorKey).value_or(0));
    const auto gender = configuration.number(kTopLevel, kMenuGenderKey);
    const auto appearance = configuration.number(kTopLevel, kMenuAppearanceKey);
    const auto body = configuration.number(kTopLevel, kMenuBodyKey);
    const auto texture = configuration.number(kTopLevel, kMenuTextureKey);
    if (gender && appearance && body && texture &&
        *gender >= 0 && *gender <= 4 && *appearance >= 0 && *appearance <= 65535 &&
        *body >= 0 && *body < 26 && *texture >= 0 && *texture <= 255) {
        result.leader = CreaturePresentation {*gender, *appearance, *body, *texture};
    }
    return result;
}

void MenuPresentation::save(const std::filesystem::path &path) const {
    auto configuration = ConfigurationFile::read(path);
    auto leaderValue = [this](int CreaturePresentation::*value) {
        return leader ? std::optional<int>((*leader).*value) : std::nullopt;
    };
    configuration.set(kTopLevel, {
                                     {kMenuSelectorKey, validateSelector(selector)},
                                     {kMenuGenderKey, leaderValue(&CreaturePresentation::gender)},
                                     {kMenuAppearanceKey, leaderValue(&CreaturePresentation::appearance)},
                                     {kMenuBodyKey, leaderValue(&CreaturePresentation::bodyVariation)},
                                     {kMenuTextureKey, leaderValue(&CreaturePresentation::textureVariation)},
                                 });
    configuration.write(path);
}

FeedbackOptions FeedbackOptions::load(const std::filesystem::path &path) {
    const auto configuration = ConfigurationFile::read(path);
    FeedbackOptions result;
    for (const auto &[key, option] : kFeedbackKeys) {
        if (auto on = configuration.flag(kGameOptionsSection, std::string(key))) {
            result.*option = *on;
        }
    }
    return result;
}

void FeedbackOptions::save(const std::filesystem::path &path) const {
    auto configuration = ConfigurationFile::read(path);
    std::vector<ConfigurationValue> values;
    for (const auto &[key, option] : kFeedbackKeys) {
        values.emplace_back(std::string(key), this->*option ? 1 : 0);
    }
    configuration.set(kGameOptionsSection, values);
    configuration.write(path);
}

AutoPauseOptions AutoPauseOptions::defaults(bool tsl) {
    AutoPauseOptions result;
    result.mineSighted = !tsl;
    return result;
}

AutoPauseOptions AutoPauseOptions::load(const std::filesystem::path &path, bool tsl) {
    const auto configuration = ConfigurationFile::read(path);
    auto result = defaults(tsl);
    for (const auto &[key, option] : kAutoPauseKeys) {
        if (auto on = configuration.flag(kAutoPauseSection, std::string(key))) {
            result.*option = *on;
        }
    }
    return result;
}

void AutoPauseOptions::save(const std::filesystem::path &path) const {
    auto configuration = ConfigurationFile::read(path);
    std::vector<ConfigurationValue> values;
    for (const auto &[key, option] : kAutoPauseKeys) {
        values.emplace_back(std::string(key), this->*option ? 1 : 0);
    }
    configuration.set(kAutoPauseSection, values);
    configuration.write(path);
}

} // namespace game

} // namespace reone
