/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/game.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "reone/resource/director.h"
#include "reone/game/portraits.h"
#include "reone/system/fileutil.h"
#include "reone/system/logger.h"
#include "reone/system/logutil.h"

namespace reone {
namespace game {
namespace {

constexpr uint32_t kQuickSaveSlot = 0;
constexpr uint32_t kAutoSaveSlot = 1;
constexpr uint32_t kMaxSemanticSlot = 999999;
constexpr uint64_t kFileTimeEpochOffset = 116444736000000000ULL;

uint64_t currentFileTime() {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto subsecondTicks = std::chrono::duration_cast<
        std::chrono::duration<uint64_t, std::ratio<1, 10000000>>>(now - seconds);
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &time);
    auto localAsUtc = _mkgmtime64(&local);
#else
    localtime_r(&time, &local);
    auto localAsUtc = timegm(&local);
#endif
    return kFileTimeEpochOffset +
           static_cast<uint64_t>(localAsUtc) * 10000000ULL +
           subsecondTicks.count();
}

std::string safeDisplayName(std::string name, uint32_t slot) {
    if (name.empty()) {
        name = "Game" + std::to_string(slot);
    }
    for (char &ch : name) {
        auto uch = static_cast<unsigned char>(ch);
        if (uch < 32 || std::string_view("<>:\"/\\|?*").find(ch) !=
                            std::string_view::npos) {
            ch = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    if (name.empty()) {
        name = "Game" + std::to_string(slot);
    }
    if (name.size() > 64) {
        name.resize(64);
    }
    return name;
}

bool isManagedLooseFile(std::string name) {
    boost::to_lower(name);
    static const std::set<std::string> managed {
        "savegame.sav", "globalvars.res", "partytable.res", "savenfo.res",
        "screen.tga", "pifo.ifo"};
    return managed.count(name) != 0 || name.find(".reone-") != std::string::npos;
}

ByteBuffer readLooseFile(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Unable to open loose save resource: " + path.string());
    }
    auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("Unable to size loose save resource: " + path.string());
    }
    ByteBuffer result(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!result.empty() &&
        !stream.read(reinterpret_cast<char *>(result.data()), result.size())) {
        throw std::runtime_error("Unable to read loose save resource: " + path.string());
    }
    return result;
}

const char *saveStatusName(SaveStatus status) {
    switch (status) {
    case SaveStatus::Accepted: return "Accepted";
    case SaveStatus::Busy: return "Busy";
    case SaveStatus::NotAllowed: return "NotAllowed";
    case SaveStatus::SnapshotFailure: return "SnapshotFailure";
    case SaveStatus::SerializationFailure: return "SerializationFailure";
    case SaveStatus::PublicationFailure: return "PublicationFailure";
    case SaveStatus::DurableSuccess: return "DurableSuccess";
    case SaveStatus::DurableSuccessCleanupPending:
        return "DurableSuccessCleanupPending";
    case SaveStatus::Cancelled: return "Cancelled";
    case SaveStatus::InternalExecutionFailure:
        return "InternalExecutionFailure";
    }
    return "Unknown";
}

const char *saveKindName(SaveKind kind) {
    switch (kind) {
    case SaveKind::Manual: return "Manual";
    case SaveKind::Quick: return "Quick";
    case SaveKind::Auto: return "Auto";
    case SaveKind::Developer: return "Developer";
    }
    return "Unknown";
}

} // namespace

SaveEligibilityReason Game::saveEligibility(bool requireStablePoint) const {
    if (_saveInProgress) return SaveEligibilityReason::SaveInProgress;
    if (_transitionInProgress) return SaveEligibilityReason::TransitionInProgress;
    if (!_runtimeSessionPlayable) {
        return _module ? SaveEligibilityReason::ReconstructionIncomplete
                       : SaveEligibilityReason::NoPlayableSession;
    }
    if (!_module) return SaveEligibilityReason::NoModule;
    if (!_module->area()) return SaveEligibilityReason::NoArea;
    if (!_party.player() || !_party.actualPlayer()) return SaveEligibilityReason::NoPlayer;
    if (requireStablePoint && !_atStableSavePoint) {
        return SaveEligibilityReason::NotStableExecutionPoint;
    }
    return SaveEligibilityReason::None;
}

SaveResult Game::requestSave(SaveRequest request) {
    if (request.kind == SaveKind::Quick) {
        request.slot = kQuickSaveSlot;
        request.displayName = "QUICKSAVE";
    } else if (request.kind == SaveKind::Auto) {
        request.slot = kAutoSaveSlot;
        request.displayName = "AUTOSAVE";
    }
    if (request.slot > kMaxSemanticSlot) {
        return {SaveStatus::NotAllowed, SaveEligibilityReason::InvalidSlot,
                SaveSlotPublishError::None, "Save slot is outside 0..999999"};
    }
    request.displayName = safeDisplayName(std::move(request.displayName), request.slot);
    if (_pendingSave || _saveInProgress) {
        return {SaveStatus::Busy, SaveEligibilityReason::SaveInProgress,
                SaveSlotPublishError::None, "Another save request is pending"};
    }
    auto reason = saveEligibility(false);
    if (reason != SaveEligibilityReason::None) {
        return {reason == SaveEligibilityReason::SaveInProgress
                    ? SaveStatus::Busy : SaveStatus::NotAllowed,
                reason, SaveSlotPublishError::None,
                "The runtime is not at an eligible playable save state"};
    }
    request.requestId = _nextSaveRequestId++;
    _pendingSave = std::move(request);
    SaveResult result;
    result.status = SaveStatus::Accepted;
    result.requestId = _pendingSave->requestId;
    result.kind = _pendingSave->kind;
    result.slot = _pendingSave->slot;
    result.displayName = _pendingSave->displayName;
    result.message = "Save request #" + std::to_string(result.requestId) +
                     " accepted for the next stable frame boundary";
    _lastSaveResult = result;
    info("Save request #" + std::to_string(result.requestId) +
         " accepted: kind=" + saveKindName(result.kind) +
         " slot=" + std::to_string(result.slot) +
         " name=\"" + result.displayName + "\"");
    return result;
}

SaveResult Game::requestManualSave(uint32_t slot, std::string displayName) {
    return requestSave({SaveKind::Manual, slot, std::move(displayName), true});
}

SaveResult Game::requestQuickSave() {
    return requestSave({SaveKind::Quick, kQuickSaveSlot, "QUICKSAVE", true});
}

SaveResult Game::requestAutoSave() {
    return requestSave({SaveKind::Auto, kAutoSaveSlot, "AUTOSAVE", true});
}

void Game::processPendingSave() {
    if (!_pendingSave) return;
    auto request = std::move(*_pendingSave);
    _pendingSave.reset();
    _atStableSavePoint = true;
    info("Save request #" + std::to_string(request.requestId) +
         " executing at stable frame: slot=" + std::to_string(request.slot) +
         " name=\"" + request.displayName + "\"");
    SaveResult result;
    try {
        result = executeSave(request);
    } catch (const std::exception &e) {
        result.status = SaveStatus::InternalExecutionFailure;
        result.message =
            std::string("Unexpected save execution exception: ") + e.what();
    } catch (...) {
        result.status = SaveStatus::InternalExecutionFailure;
        result.message = "Unknown save execution exception";
    }
    _atStableSavePoint = false;
    finalizeSaveRequest(request, std::move(result));
}

void Game::finalizeSaveRequest(const SaveRequest &request, SaveResult result) {
    result.requestId = request.requestId;
    result.kind = request.kind;
    result.slot = request.slot;
    result.displayName = request.displayName;
    _lastSaveResult = std::move(result);

    auto summary = "Save request #" + std::to_string(request.requestId) +
                   " terminal: status=" + saveStatusName(_lastSaveResult->status) +
                   " slot=" + std::to_string(request.slot) +
                   " name=\"" + request.displayName + "\" message=" +
                   _lastSaveResult->message;
    if (_lastSaveResult->durable) {
        info(summary);
    } else {
        warn(summary);
    }

    if (request.kind == SaveKind::Developer) {
        std::string consoleText = "Save #" + std::to_string(request.requestId);
        if (_lastSaveResult->status == SaveStatus::DurableSuccess) {
            consoleText += " completed";
        } else if (_lastSaveResult->status ==
                   SaveStatus::DurableSuccessCleanupPending) {
            consoleText += " completed with cleanup warning";
        } else if (_lastSaveResult->status == SaveStatus::Cancelled) {
            consoleText += " cancelled";
        } else {
            consoleText += " failed (";
            consoleText += saveStatusName(_lastSaveResult->status);
            consoleText += ")";
        }
        consoleText += ": slot " + std::to_string(request.slot) + " \"" +
                       request.displayName + "\"";
        if (!_lastSaveResult->message.empty()) {
            consoleText += " - " + _lastSaveResult->message;
        }
        try {
            _console.printLine(consoleText);
        } catch (const std::exception &e) {
            warn("Unable to publish developer save result: " + std::string(e.what()));
        }
    }
    if (_saveSeams.terminalResult) {
        try {
            _saveSeams.terminalResult(request, *_lastSaveResult);
        } catch (const std::exception &e) {
            warn("Save terminal observer failed: " + std::string(e.what()));
        }
    }
    Logger::instance.flush();
}

SaveMetadataInput Game::buildSaveMetadata(const SaveRequest &request) const {
    SaveMetadataInput metadata;
    metadata.displayName = request.displayName;
    metadata.saveNumber = request.slot;
    metadata.timestamp = _saveSeams.timestamp
                             ? _saveSeams.timestamp()
                             : currentFileTime();
    metadata.cheatUsed = _cheatUsed ? 1 : 0;

    auto shadow = _saveResourceShadows.find({SaveResourceKind::Nfo, {}});
    if (shadow) {
        const auto &nfo = shadow->record();
        metadata.gameplayHint = static_cast<uint8_t>(nfo.getUint("GAMEPLAYHINT"));
        metadata.storyHint = static_cast<uint8_t>(nfo.getUint("STORYHINT"));
        metadata.liveContent = static_cast<uint8_t>(nfo.getUint("LIVECONTENT"));
        for (size_t i = 0; i < metadata.storyHints.size(); ++i) {
            metadata.storyHints[i] = static_cast<uint8_t>(
                nfo.getUint("STORYHINT" + std::to_string(i)));
        }
        for (size_t i = 0; i < metadata.liveContentNames.size(); ++i) {
            metadata.liveContentNames[i] = nfo.getString(
                "LIVE" + std::to_string(i + 1));
        }
        for (size_t i = 0; i < metadata.portraits.size(); ++i) {
            metadata.portraits[i] = nfo.getString(
                "PORTRAIT" + std::to_string(i));
        }
    }

    const auto &portraitRows = _services.game.portraits.portraits();
    auto livePortrait = [&portraitRows](const std::shared_ptr<Creature> &creature) {
        if (!creature) return std::string();
        auto portraitId = creature->portraitId();
        if (portraitId > 0 && portraitId < portraitRows.size()) {
            return portraitRows[portraitId].resRef;
        }
        auto appearance = creature->appearance();
        auto found = std::find_if(
            portraitRows.begin(), portraitRows.end(),
            [appearance](const Portrait &portrait) {
                return portrait.appearanceNumber == appearance ||
                       portrait.appearanceS == appearance ||
                       portrait.appearanceL == appearance;
            });
        return found == portraitRows.end() ? std::string() : found->resRef;
    };
    for (size_t i = 0; i < metadata.portraits.size(); ++i) {
        auto creature = i < _party.members().size()
                            ? _party.members()[i].creature
                            : nullptr;
        if (!creature && i == 0) creature = _party.actualPlayer();
        auto portrait = livePortrait(creature);
        if (!portrait.empty()) metadata.portraits[i] = std::move(portrait);
    }
    return metadata;
}

resource::SaveSlotDescriptor Game::saveTarget(const SaveRequest &request) const {
    std::ostringstream prefix;
    prefix << std::setw(6) << std::setfill('0') << request.slot;
    auto directory = _path / "saves" /
                     (prefix.str() + " - " + request.displayName);
    return {directory, directory / "SAVEGAME.sav"};
}

std::map<std::string, ByteBuffer> Game::currentLooseSavePassthrough() const {
    std::map<std::string, ByteBuffer> result;
    auto source = _services.resource.director.saveSlotDescriptor();
    if (!source || !std::filesystem::is_directory(source->directory)) {
        return result;
    }
    for (const auto &entry : std::filesystem::directory_iterator(source->directory)) {
        auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status)) {
            continue;
        }
        auto name = entry.path().filename().string();
        if (!isSafePathComponent(name) || isManagedLooseFile(name)) {
            continue;
        }
        result.emplace(name, readLooseFile(entry.path()));
    }
    return result;
}

SaveResult Game::executeSave(SaveRequest request) {
    auto reason = saveEligibility(true);
    if (reason != SaveEligibilityReason::None) {
        return {SaveStatus::NotAllowed, reason, SaveSlotPublishError::None,
                "Save execution did not reach an eligible stable point"};
    }
    auto committed = _services.resource.director.committedSaveWorkingState();
    if (!committed) {
        return {SaveStatus::SerializationFailure,
                SaveEligibilityReason::ReconstructionIncomplete,
                SaveSlotPublishError::None,
                "The playable session has no committed save working state"};
    }

    _saveInProgress = true;
    struct Guard {
        bool &value;
        ~Guard() { value = false; }
    } guard {_saveInProgress};

    std::optional<ByteBuffer> screenshot;
    if (request.captureScreenshot && _saveSeams.captureScreenshot) {
        try {
            screenshot = _saveSeams.captureScreenshot();
        } catch (const std::exception &e) {
            warn("Save screenshot capture failed: " + std::string(e.what()));
        }
    }

    auto moduleResult = _saveSeams.captureModule
                            ? _saveSeams.captureModule(*this, _module->name())
                            : ModuleSnapshotBuilder(*this, _module->name()).build();
    if (!moduleResult) {
        return {SaveStatus::SnapshotFailure, SaveEligibilityReason::None,
                SaveSlotPublishError::None, moduleResult.message};
    }
    info("Save request #" + std::to_string(request.requestId) +
         " E3d complete; serializing save-wide state");
    auto wideResult = _saveSeams.captureSaveWide
                          ? _saveSeams.captureSaveWide(
                                *this, buildSaveMetadata(request))
                          : SaveWideSnapshotBuilder(
                                *this, buildSaveMetadata(request)).build();
    if (!wideResult) {
        return {SaveStatus::SerializationFailure, SaveEligibilityReason::None,
                SaveSlotPublishError::None, wideResult.message};
    }
    info("Save request #" + std::to_string(request.requestId) +
         " E3e complete; preparing durable publication");

    SaveSlotPackageInput package;
    package.committedWorkingState = std::move(committed);
    package.currentModule = std::move(*moduleResult.snapshot);
    package.saveWide = std::move(*wideResult.snapshot);
    package.target = saveTarget(request);
    package.screenshot = std::move(screenshot);
    try {
        package.loosePassthrough = currentLooseSavePassthrough();
        std::filesystem::create_directories(package.target.directory.parent_path());
    } catch (const std::exception &e) {
        return {SaveStatus::SerializationFailure, SaveEligibilityReason::None,
                SaveSlotPublishError::None, e.what()};
    }

    auto published = _saveSeams.publish
                         ? _saveSeams.publish(std::move(package))
                         : SaveSlotPublisher().publish(std::move(package));
    if (!published) {
        return {SaveStatus::PublicationFailure, SaveEligibilityReason::None,
                published.error, published.message, published.publishedSlot,
                published.durable, published.cleanupPending};
    }

    info("Save request #" + std::to_string(request.requestId) +
         " E3f complete; adopting detached committed state");

    _services.resource.director.adoptPublishedSave(
        *published.publishedSlot, published.committedWorkingState);
    _saveNames = _services.resource.director.saveNames();
    info("Save request #" + std::to_string(request.requestId) + " adopted");

    SaveResult result;
    result.status = published.cleanupPending
                        ? SaveStatus::DurableSuccessCleanupPending
                        : SaveStatus::DurableSuccess;
    result.publicationError = published.error;
    result.message = published.message;
    result.publishedSlot = published.publishedSlot;
    result.durable = true;
    result.cleanupPending = published.cleanupPending;
    return result;
}

bool Game::storeCurrentModuleForTransition() {
    auto committed = _services.resource.director.committedSaveWorkingState();
    if (!committed || !_module) {
        error("Module transition has no committed save working state");
        return false;
    }
    auto captured = _saveSeams.captureModule
                        ? _saveSeams.captureModule(*this, _module->name())
                        : ModuleSnapshotBuilder(*this, _module->name()).build();
    if (!captured) {
        error("Unable to snapshot source module for transition: " + captured.message);
        return false;
    }
    try {
        auto candidate = resource::SaveWorkingStateCandidate::fromCommitted(
            std::move(committed));
        candidate.replaceModule(
            captured.snapshot->target,
            captured.snapshot->archiveBytes,
            resource::ResourceId(
                captured.snapshot->target.resRef, resource::ResType::Rsv));
        auto validation = candidate.validate();
        if (!validation) {
            error("Unable to validate transition working state: " +
                  validation.errors.front());
            return false;
        }
        _services.resource.director.adoptSaveWorkingState(candidate.freeze());
        return true;
    } catch (const std::exception &e) {
        error("Unable to commit transition working state: " + std::string(e.what()));
        return false;
    }
}

void Game::advancePlayedTime(float dt) {
    if (dt <= 0.0f) return;
    _playedTimeFraction += dt;
    auto whole = static_cast<uint32_t>(std::floor(_playedTimeFraction));
    if (whole == 0) return;
    _playedTimeFraction -= whole;
    auto state = _party.persistedState();
    state.playedSeconds += whole;
    _party.setPersistedState(std::move(state));
}

} // namespace game
} // namespace reone
