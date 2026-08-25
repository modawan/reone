/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/savedgame.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "reone/resource/format/gffreader.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/fileinput.h"

namespace reone {
namespace game {

namespace {

constexpr char kSavesDirectoryName[] = "saves";

std::optional<uint32_t> parseSlot(const std::string &name) {
    if (name.size() < 6 ||
        !std::all_of(name.begin(), name.begin() + 6,
                     [](unsigned char ch) { return std::isdigit(ch); }) ||
        (name.size() > 6 && name[6] != ' ')) {
        return std::nullopt;
    }
    try {
        auto value = std::stoul(name.substr(0, 6));
        if (value > 999999) return std::nullopt;
        return static_cast<uint32_t>(value);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

uint32_t displayNumber(
    uint32_t slot,
    const resource::NFO &metadata) {
    // K2 persists the display value explicitly. K1 does not and numbers
    // manual games from one while quick/autosave occupy durable slots 0/1.
    // The directory suffix is not authoritative: Reone stores the user-entered
    // save name there, and retail-style names only corroborate this slot model.
    if (metadata.saveNumber) return *metadata.saveNumber;
    return slot >= 2 ? slot - 1 : slot;
}

ByteBuffer readFile(const std::filesystem::path &path) {
    FileInputStream stream(path);
    ByteBuffer result(stream.length());
    if (!result.empty() && stream.read(result.data(), result.size()) != result.size()) {
        throw std::runtime_error("short save-slot file read");
    }
    return result;
}

SavedGame readSlot(
    uint32_t slot,
    const std::filesystem::path &directory) {
    auto archive = directory / "SAVEGAME.sav";
    auto nfoPath = directory / "savenfo.res";
    if (!std::filesystem::is_regular_file(archive) ||
        !std::filesystem::is_regular_file(nfoPath)) {
        throw std::runtime_error("required save-slot files are missing");
    }
    FileInputStream nfoStream(nfoPath);
    resource::GffReader reader(nfoStream);
    reader.load();

    SavedGame result;
    result.slot = slot;
    result.descriptor = {directory, archive};
    result.metadata = resource::parseNFO(*reader.root());
    result.displayNumber = displayNumber(slot, result.metadata);
    auto screenshot = directory / "Screen.tga";
    if (std::filesystem::is_regular_file(screenshot)) {
        result.screenshot = readFile(screenshot);
    }
    return result;
}

} // namespace

std::filesystem::path savedGamesDirectory(const std::filesystem::path &gamePath) {
    std::error_code ec;
    if (!std::filesystem::is_directory(gamePath, ec)) {
        return gamePath / kSavesDirectoryName;
    }

    auto foldsToSaves = [](const std::string &name) {
        static const std::string canonical {kSavesDirectoryName};
        if (name.size() != canonical.size()) {
            return false;
        }
        return std::equal(
            name.begin(), name.end(), canonical.begin(),
            [](unsigned char lhs, unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            });
    };

    std::vector<std::filesystem::path> roots;
    for (const auto &entry : std::filesystem::directory_iterator(gamePath, ec)) {
        if (entry.is_directory(ec) && foldsToSaves(entry.path().filename().string())) {
            roots.push_back(entry.path());
        }
    }
    if (roots.empty()) {
        return gamePath / kSavesDirectoryName;
    }
    std::sort(roots.begin(), roots.end());

    auto chosen = roots.front();
    for (const auto &root : roots) {
        if (root.filename().string() == kSavesDirectoryName) {
            chosen = root;
            break;
        }
    }

    // A second save root is not cosmetic: every slot below the roots that lose
    // is invisible to the list, and saves written here will not be found by
    // anything that resolved differently. Say so rather than quietly picking.
    for (const auto &root : roots) {
        if (root == chosen) {
            continue;
        }
        warn("Save root '" + root.filename().string() + "' is shadowed by '" +
             chosen.filename().string() + "': saves stored there are not listed. " +
             "Merge them into '" + chosen.filename().string() + "' to recover them");
    }
    return chosen;
}

std::vector<SavedGame> discoverSavedGames(const std::filesystem::path &gamePath) {
    std::vector<SavedGame> result;
    auto savesPath = savedGamesDirectory(gamePath);
    if (!std::filesystem::is_directory(savesPath)) return result;

    // Numeric identity is authoritative. If external tools left duplicates,
    // prefer the most recently modified complete slot deterministically.
    std::map<uint32_t, std::pair<std::filesystem::file_time_type, SavedGame>> bySlot;
    for (const auto &entry : std::filesystem::directory_iterator(savesPath)) {
        try {
            auto status = entry.symlink_status();
            if (std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status)) {
                continue;
            }
            auto slot = parseSlot(entry.path().filename().string());
            if (!slot) continue;
            auto saved = readSlot(*slot, entry.path());
            auto modified = std::filesystem::last_write_time(entry.path());
            auto found = bySlot.find(*slot);
            if (found == bySlot.end() || modified > found->second.first ||
                (modified == found->second.first &&
                 entry.path().string() < found->second.second.descriptor.directory.string())) {
                bySlot[*slot] = {modified, std::move(saved)};
            }
        } catch (const std::exception &e) {
            warn("Error indexing saved game '" + entry.path().string() + "': " + e.what());
        }
    }
    for (auto &[slot, saved] : bySlot) {
        (void)slot;
        result.push_back(std::move(saved.second));
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.slot < b.slot;
    });
    return result;
}

std::string saveGameNumberLabel(const SavedGame &save) {
    return "Game " + std::to_string(save.displayNumber);
}

uint32_t nextManualSaveSlot(const std::vector<SavedGame> &saves) {
    uint32_t result = 2; // 0/1 are retail quick/autosave identities.
    for (const auto &save : saves) {
        if (save.slot >= result && save.slot < 999999) result = save.slot + 1;
    }
    return result;
}

bool deleteSavedGame(
    const std::filesystem::path &gamePath,
    const resource::SaveSlotDescriptor &slot) {
    std::error_code ec;
    auto saves = std::filesystem::weakly_canonical(savedGamesDirectory(gamePath), ec);
    if (ec) return false;
    auto target = std::filesystem::weakly_canonical(slot.directory, ec);
    if (ec || target.parent_path() != saves || target == saves ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(target, ec))) {
        return false;
    }
    if (ec || slot.archive.parent_path() != slot.directory ||
        target.filename() != slot.directory.filename() ||
        !parseSlot(target.filename().string())) {
        return false;
    }
    return std::filesystem::remove_all(target, ec) > 0 && !ec;
}

} // namespace game
} // namespace reone
