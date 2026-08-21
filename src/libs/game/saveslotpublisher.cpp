/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/saveslotpublisher.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <boost/algorithm/string/case_conv.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "reone/resource/container/erf.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/are.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/parser/gff/ifo.h"
#include "reone/resource/parser/gff/nfo.h"
#include "reone/system/stream/memoryinput.h"

namespace reone {
namespace game {

namespace {

using resource::ErfReader;
using resource::ErfResourceContainer;
using resource::ErfWriter;
using resource::Gff;
using resource::GffReader;
using resource::ResType;
using resource::ResourceId;
using resource::SaveSlotDescriptor;
using resource::SaveWorkingState;
using resource::SaveWorkingStateCandidate;
using resource::Storage;

constexpr const char *kArchiveName = "SAVEGAME.sav";
constexpr const char *kGlobalsName = "GLOBALVARS.res";
constexpr const char *kPartyName = "PARTYTABLE.res";
constexpr const char *kNfoName = "savenfo.res";
constexpr const char *kScreenshotName = "Screen.tga";
constexpr const char *kMarkerMagic = "REONE_SAVE_TXN_V1";

enum class TransactionPhase {
    CandidateValidated = 1,
    OldTargetBackedUp = 2,
    NewTargetPublished = 3,
    NewTargetReopened = 4,
};

struct TransactionMarker {
    TransactionPhase phase {TransactionPhase::CandidateValidated};
    std::string targetName;
    std::string candidateName;
    std::string backupName;
};

struct ExpectedSlot {
    std::map<std::string, ByteBuffer> files;
    std::set<ResourceId> outerIds;
    ResourceId activeModule;
    std::string moduleName;
    std::string areaName;
    resource::GameID gameId {resource::GameID::KotOR};
    bool screenshotPresent {false};
};

struct ValidatedSlot {
    std::shared_ptr<const SaveWorkingState> state;
    size_t nestedModules {0};
};

std::set<std::string> activeTargets;

std::string lower(std::string value) {
    boost::algorithm::to_lower(value);
    return value;
}

uint64_t fnv1a(const char *data, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string digest(const ByteBuffer &bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << fnv1a(bytes.data(), bytes.size());
    return stream.str();
}

std::string uniqueSuffix() {
    static std::atomic<uint64_t> sequence {0};
    std::random_device random;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(8) << random()
           << std::setw(8) << random()
           << std::setw(16) << ++sequence;
    return stream.str();
}

bool inject(
    const SaveSlotPackageInput &input,
    SaveSlotPublishCheckpoint checkpoint) {
    return input.failureInjector && input.failureInjector(checkpoint);
}

ByteBuffer readFile(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) {
        throw std::runtime_error("Unable to open " + path.string());
    }
    auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("Unable to determine size of " + path.string());
    }
    ByteBuffer bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Short read from " + path.string());
        }
    }
    return bytes;
}

void writeFile(const std::filesystem::path &path, const ByteBuffer &bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        throw std::runtime_error("Unable to create " + path.string());
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto count = static_cast<std::streamsize>(std::min<size_t>(
            bytes.size() - offset,
            static_cast<size_t>(std::numeric_limits<std::streamsize>::max())));
        stream.write(bytes.data() + offset, count);
        if (!stream) {
            throw std::runtime_error("Short write to " + path.string());
        }
        offset += static_cast<size_t>(count);
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error("Unable to flush " + path.string());
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("Unable to close " + path.string());
    }
}

std::filesystem::path markerPath(const SaveSlotDescriptor &target) {
    auto name = target.directory.filename().string();
    std::ostringstream hash;
    hash << std::hex << fnv1a(name.data(), name.size());
    return target.directory.parent_path() /
           (".reone-save-txn-" + hash.str() + ".marker");
}

bool isSafeSiblingName(const std::string &name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('\0') != std::string::npos) {
        return false;
    }
    auto path = std::filesystem::path(name);
    return !path.is_absolute() && path.filename() == path &&
           !path.has_parent_path();
}

void validateDescriptor(const SaveSlotDescriptor &target) {
    if (target.directory.empty() || !target.directory.is_absolute()) {
        throw std::invalid_argument("Save target directory must be absolute");
    }
    auto normalizedDirectory = target.directory.lexically_normal();
    auto expectedArchive = (normalizedDirectory / kArchiveName).lexically_normal();
    if (target.archive.lexically_normal() != expectedArchive) {
        throw std::invalid_argument(
            "Save target archive must be the target slot's SAVEGAME.sav");
    }
    if (!isSafeSiblingName(normalizedDirectory.filename().string())) {
        throw std::invalid_argument("Save target must be one direct child directory");
    }
    auto parent = normalizedDirectory.parent_path();
    if (!std::filesystem::is_directory(parent) ||
        std::filesystem::is_symlink(parent)) {
        throw std::invalid_argument(
            "Save target parent must be an existing non-symlink directory");
    }
    if (std::filesystem::exists(normalizedDirectory) &&
        (!std::filesystem::is_directory(normalizedDirectory) ||
         std::filesystem::is_symlink(normalizedDirectory))) {
        throw std::invalid_argument(
            "Save target must be absent or an existing non-symlink directory");
    }
}

std::shared_ptr<Gff> readGff(
    const ByteBuffer &bytes, const std::string &signature) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    GffReader reader(stream);
    reader.load();
    auto root = reader.root();
    if (!root->signature() || *root->signature() != signature) {
        throw std::runtime_error(
            "Expected " + signature + " structured resource");
    }
    return root;
}

std::set<ResourceId> readArchiveIds(
    const ByteBuffer &bytes, const std::string &signature) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    ErfReader reader(stream);
    reader.load();
    if (reader.signature() != signature) {
        throw std::runtime_error("Unexpected archive signature");
    }
    std::set<ResourceId> ids;
    for (const auto &key : reader.keys()) {
        if (!ids.insert(key.resId).second) {
            throw std::runtime_error("Archive contains a duplicate resource identity");
        }
    }
    return ids;
}

size_t validateNestedModule(const ByteBuffer &bytes) {
    auto ids = readArchiveIds(bytes, std::string("MOD V1.0", 8));
    size_t ifos = 0;
    size_t ares = 0;
    size_t gits = 0;
    for (const auto &id : ids) {
        if (id == ResourceId("module", ResType::Ifo)) {
            ++ifos;
        } else if (id.type == ResType::Are) {
            ++ares;
        } else if (id.type == ResType::Git) {
            ++gits;
        }
    }
    if (ids.size() != 3 || ifos != 1 || ares != 1 || gits != 1) {
        throw std::runtime_error(
            "Saved module must contain exactly module.ifo, one ARE and one GIT");
    }

    ErfResourceContainer container {Storage(ByteBuffer(bytes))};
    container.init();
    auto ifo = container.findResourceData({"module", ResType::Ifo});
    auto areId = *std::find_if(ids.begin(), ids.end(), [](const ResourceId &id) {
        return id.type == ResType::Are;
    });
    auto gitId = *std::find_if(ids.begin(), ids.end(), [](const ResourceId &id) {
        return id.type == ResType::Git;
    });
    auto ifoGff = readGff(*ifo, "IFO V3.2");
    auto areGff = readGff(*container.findResourceData(areId), "ARE V3.2");
    auto gitGff = readGff(*container.findResourceData(gitId), "GIT V3.2");
    (void)resource::generated::parseIFO(*ifoGff);
    (void)resource::generated::parseARE(*areGff);
    (void)resource::generated::parseGIT(*gitGff);
    return 1;
}

void validateTga(const ByteBuffer &bytes) {
    if (bytes.size() < 18) {
        throw std::runtime_error("Screenshot TGA header is truncated");
    }
    auto byte = [&bytes](size_t at) {
        return static_cast<uint8_t>(bytes[at]);
    };
    auto word = [&byte](size_t at) {
        return static_cast<uint16_t>(byte(at) | (byte(at + 1) << 8));
    };
    auto type = byte(2);
    auto width = word(12);
    auto height = word(14);
    auto depth = byte(16);
    if ((type != 2 && type != 3 && type != 10 && type != 11) ||
        width == 0 || height == 0 ||
        (depth != 8 && depth != 24 && depth != 32)) {
        throw std::runtime_error("Unsupported or invalid screenshot TGA");
    }
    size_t offset = 18 + byte(0);
    if (byte(1)) {
        offset += static_cast<size_t>(word(5)) * ((byte(7) + 7) / 8);
    }
    if (offset > bytes.size()) {
        throw std::runtime_error("Screenshot TGA tables are truncated");
    }
    if (type == 2 || type == 3) {
        uint64_t payload = static_cast<uint64_t>(width) * height * (depth / 8);
        if (payload > bytes.size() - offset) {
            throw std::runtime_error("Screenshot TGA pixels are truncated");
        }
    } else if (offset == bytes.size()) {
        throw std::runtime_error("Screenshot TGA has no encoded pixels");
    }
}

void validatePassthrough(const std::map<std::string, ByteBuffer> &files) {
    static const std::set<std::string> managed {
        lower(kArchiveName), lower(kGlobalsName), lower(kPartyName),
        lower(kNfoName), lower(kScreenshotName)};
    std::set<std::string> seen;
    for (const auto &[name, bytes] : files) {
        (void)bytes;
        auto folded = lower(name);
        if (!isSafeSiblingName(name) || managed.count(folded) ||
            folded.find(".reone-") != std::string::npos ||
            !seen.insert(folded).second) {
            throw std::invalid_argument(
                "Unsafe or managed loose passthrough filename: " + name);
        }
    }
}

ByteBuffer packageCandidate(const SaveWorkingStateCandidate &candidate) {
    ErfWriter writer;
    for (const auto &id : candidate.deterministicResourceIds()) {
        auto view = candidate.find(id);
        if (!view) {
            throw std::runtime_error("Candidate lost " + id.string());
        }
        writer.add(ErfWriter::Resource::lazy(
            id, [retained = std::move(*view), id]() mutable {
                auto resource = retained.read();
                if (!resource) {
                    throw std::runtime_error(
                        "Unable to read candidate resource " + id.string());
                }
                return std::move(resource->data);
            }));
    }
    return writer.toBytes(ErfWriter::FileType::MOD);
}

void validateExpectedInput(const SaveSlotPackageInput &input) {
    validateDescriptor(input.target);
    validatePassthrough(input.loosePassthrough);
    if (!input.committedWorkingState) {
        throw std::invalid_argument("A committed save working state is required");
    }
    if (input.currentModule.target.type != ResType::Sav ||
        input.currentModule.archiveBytes.empty() || !input.currentModule.ifo ||
        !input.currentModule.are || !input.currentModule.git) {
        throw std::invalid_argument("Current module snapshot is incomplete");
    }
    if (input.saveWide.moduleName.empty() || input.saveWide.areaName.empty() ||
        input.currentModule.target.resRef.value() !=
            lower(input.saveWide.moduleName)) {
        throw std::invalid_argument(
            "Save-wide active module does not match the current module artifact");
    }
    if (!input.saveWide.outerWorkingResources.count({"inventory", ResType::Res}) ||
        !input.saveWide.outerWorkingResources.count({"repute", ResType::Fac})) {
        throw std::invalid_argument(
            "Save-wide snapshot lacks inventory.res or repute.fac");
    }
    validateNestedModule(input.currentModule.archiveBytes);
    ErfResourceContainer currentArchive {
        Storage(ByteBuffer(input.currentModule.archiveBytes))};
    currentArchive.init();
    auto archiveIfo = currentArchive.findResourceData({"module", ResType::Ifo});
    auto archiveAre = currentArchive.findResourceData(
        {input.saveWide.areaName, ResType::Are});
    auto archiveGit = currentArchive.findResourceData(
        {input.saveWide.areaName, ResType::Git});
    if (!archiveIfo || !archiveAre || !archiveGit ||
        *archiveIfo != input.currentModule.ifoBytes ||
        *archiveAre != input.currentModule.areBytes ||
        *archiveGit != input.currentModule.gitBytes ||
        resource::GffWriter(
            resource::GffFileFormat::v32("IFO "),
            *input.currentModule.ifo).toBytes() != input.currentModule.ifoBytes ||
        resource::GffWriter(
            resource::GffFileFormat::v32("ARE "),
            *input.currentModule.are).toBytes() != input.currentModule.areBytes ||
        resource::GffWriter(
            resource::GffFileFormat::v32("GIT "),
            *input.currentModule.git).toBytes() != input.currentModule.gitBytes) {
        throw std::invalid_argument(
            "Current module semantic, encoded and nested archive forms disagree");
    }
    if (input.screenshot) {
        validateTga(*input.screenshot);
    }

    const std::map<std::string, ResourceId> loose {
        {kGlobalsName, {"globalvars", ResType::Res}},
        {kPartyName, {"partytable", ResType::Res}},
        {kNfoName, {"savenfo", ResType::Res}},
    };
    for (const auto &[name, id] : loose) {
        auto found = input.saveWide.looseSlotResources.find(id);
        if (found == input.saveWide.looseSlotResources.end()) {
            throw std::invalid_argument("Save-wide snapshot lacks " + name);
        }
    }
    if (input.saveWide.looseSlotResources.size() != loose.size()) {
        throw std::invalid_argument(
            "Save-wide snapshot contains an unsupported loose managed resource");
    }
    auto globals = readGff(
        input.saveWide.looseSlotResources.at({"globalvars", ResType::Res}),
        "GVT V3.2");
    (void)resource::parseGVT(*globals);
    auto party = readGff(
        input.saveWide.looseSlotResources.at({"partytable", ResType::Res}),
        "PT  V3.2");
    auto nfo = readGff(
        input.saveWide.looseSlotResources.at({"savenfo", ResType::Res}),
        "NFO V3.2");
    if (lower(nfo->getString("LASTMODULE")) != lower(input.saveWide.moduleName)) {
        throw std::invalid_argument(
            "Save metadata does not identify the active module");
    }
    (void)resource::parseNFO(*nfo);

    std::set<ResourceId> encodedIds;
    for (const auto &[id, bytes] : input.saveWide.looseSlotResources) {
        (void)bytes;
        encodedIds.insert(id);
    }
    for (const auto &[id, bytes] : input.saveWide.outerWorkingResources) {
        (void)bytes;
        if (!input.saveWide.managedOuterResources.count(id)) {
            throw std::invalid_argument(
                "Save-wide output contains an unmanaged resource: " + id.string());
        }
        encodedIds.insert(id);
    }
    std::set<ResourceId> semanticIds;
    for (const auto &[id, gff] : input.saveWide.semanticResources) {
        if (!gff) {
            throw std::invalid_argument(
                "Save-wide semantic resource is null: " + id.string());
        }
        semanticIds.insert(id);
    }
    if (semanticIds != encodedIds) {
        throw std::invalid_argument(
            "Save-wide semantic and encoded resource identities differ");
    }
    auto validateSemanticBytes = [&input](
        const ResourceId &id, const ByteBuffer &bytes) {
        std::string signature;
        if (id == ResourceId("globalvars", ResType::Res)) signature = "GVT ";
        else if (id == ResourceId("partytable", ResType::Res)) signature = "PT  ";
        else if (id == ResourceId("savenfo", ResType::Res)) signature = "NFO ";
        else if (id == ResourceId("inventory", ResType::Res)) signature = "INV ";
        else if (id == ResourceId("repute", ResType::Fac)) signature = "FAC ";
        else if (id.type == ResType::Utc) signature = "UTC ";
        else {
            throw std::invalid_argument(
                "Unsupported managed save-wide semantic resource: " + id.string());
        }
        auto encoded = resource::GffWriter(
            resource::GffFileFormat::v32(signature),
            *input.saveWide.semanticResources.at(id)).toBytes();
        if (encoded != bytes) {
            throw std::invalid_argument(
                "Save-wide semantic and encoded bytes disagree: " + id.string());
        }
    };
    for (const auto &[id, bytes] : input.saveWide.looseSlotResources) {
        validateSemanticBytes(id, bytes);
    }
    for (const auto &[id, bytes] : input.saveWide.outerWorkingResources) {
        validateSemanticBytes(id, bytes);
    }

    const size_t npcCount = input.saveWide.gameId == resource::GameID::TSL
                                ? 12 : 9;
    auto availableNpcs = party->getList("PT_AVAIL_NPCS");
    if (availableNpcs.size() != npcCount) {
        throw std::invalid_argument("PARTYTABLE NPC topology has the wrong title size");
    }
    for (size_t index = 0; index < npcCount; ++index) {
        bool expected = availableNpcs[index]->getBool("PT_NPC_AVAIL");
        bool present = input.saveWide.outerWorkingResources.count(
                           {"availnpc" + std::to_string(index), ResType::Utc}) != 0;
        if (present != expected) {
            throw std::invalid_argument(
                "PARTYTABLE and availnpc topology disagree");
        }
    }
    int controlled = party->getInt("PT_CONTROLLED_NP", -1);
    bool pcExpected = input.saveWide.gameId == resource::GameID::TSL ||
                      controlled != -1;
    if ((input.saveWide.outerWorkingResources.count({"pc", ResType::Utc}) != 0) !=
        pcExpected) {
        throw std::invalid_argument("PARTYTABLE and pc.utc topology disagree");
    }
    auto players = input.currentModule.ifo->getList("Mod_PlayerList");
    if (players.size() != 1 ||
        players.front()->getUint("ObjectId", UINT32_MAX) == UINT32_MAX ||
        players.front()->getBool("Mod_IsPrimaryPlr") != (controlled == -1)) {
        throw std::invalid_argument(
            "Current module player topology disagrees with PARTYTABLE");
    }
    if (input.saveWide.gameId == resource::GameID::TSL) {
        auto availablePuppets = party->getList("PT_AVAIL_PUPS");
        if (availablePuppets.size() != 3) {
            throw std::invalid_argument("K2 PARTYTABLE puppet topology is incomplete");
        }
        for (size_t index = 0; index < availablePuppets.size(); ++index) {
            bool expected = availablePuppets[index]->getBool("PT_PUP_AVAIL");
            bool present = input.saveWide.outerWorkingResources.count(
                               {"availpup" + std::to_string(index), ResType::Utc}) != 0;
            if (present != expected) {
                throw std::invalid_argument(
                    "PARTYTABLE and availpup topology disagree");
            }
        }
    } else if (party->has("PT_AVAIL_PUPS")) {
        throw std::invalid_argument("K1 PARTYTABLE contains K2 puppet state");
    }

    auto factions = readGff(
        input.saveWide.outerWorkingResources.at({"repute", ResType::Fac}),
        "FAC V3.2");
    auto factionCount = factions->getList("FactionList").size();
    std::set<std::pair<uint32_t, uint32_t>> pairs;
    for (const auto &entry : factions->getList("RepList")) {
        auto target = entry->getUint("FactionID1", UINT32_MAX);
        auto source = entry->getUint("FactionID2", UINT32_MAX);
        if (target >= factionCount || source == 0 || source >= factionCount ||
            !pairs.emplace(target, source).second) {
            throw std::invalid_argument("FAC contains an invalid reputation reference");
        }
    }
    readGff(
        input.saveWide.outerWorkingResources.at({"inventory", ResType::Res}),
        "INV V3.2");
    for (const auto &[id, bytes] : input.saveWide.outerWorkingResources) {
        if (id.type == ResType::Utc) {
            auto utc = readGff(bytes, "UTC V3.2");
            if (utc->has("ObjectId")) {
                throw std::invalid_argument(
                    "Detached save-wide UTC has a world ObjectId: " + id.string());
            }
        }
    }
}

ExpectedSlot compose(const SaveSlotPackageInput &input) {
    auto candidate = SaveWorkingStateCandidate::fromCommitted(
        input.committedWorkingState);
    candidate.replaceModule(
        input.currentModule.target,
        input.currentModule.archiveBytes,
        ResourceId(input.currentModule.target.resRef, ResType::Rsv));
    input.saveWide.applyTo(candidate);

    auto validation = candidate.validate([&input](
        const SaveWorkingStateCandidate &value,
        resource::SaveWorkingStateCandidateValidation &result) {
        if (!value.contains({"inventory", ResType::Res})) {
            result.addError("Missing inventory.res");
        }
        if (!value.contains({"repute", ResType::Fac})) {
            result.addError("Missing repute.fac");
        }
        if (!value.contains(input.currentModule.target)) {
            result.addError("Missing active saved module");
        }
        if (value.contains(
                {input.currentModule.target.resRef, ResType::Rsv})) {
            result.addError("Matching active module .rsv was not tombstoned");
        }
        for (const auto &id : {
                 ResourceId("globalvars", ResType::Res),
                 ResourceId("partytable", ResType::Res),
                 ResourceId("savenfo", ResType::Res)}) {
            if (value.contains(id)) {
                result.addError("Loose managed resource leaked into outer archive: " +
                                id.string());
            }
        }
        for (const auto &id : input.saveWide.managedOuterResources) {
            auto expected = input.saveWide.outerWorkingResources.count(id) != 0;
            if (value.contains(id) != expected) {
                result.addError("Managed outer topology mismatch: " + id.string());
            }
        }
    });
    if (!validation) {
        std::ostringstream message;
        for (const auto &error : validation.errors) {
            if (message.tellp() > 0) {
                message << "; ";
            }
            message << error;
        }
        throw std::runtime_error(message.str());
    }

    ExpectedSlot expected;
    expected.files.emplace(kArchiveName, packageCandidate(candidate));
    expected.files.emplace(
        kGlobalsName,
        input.saveWide.looseSlotResources.at({"globalvars", ResType::Res}));
    expected.files.emplace(
        kPartyName,
        input.saveWide.looseSlotResources.at({"partytable", ResType::Res}));
    expected.files.emplace(
        kNfoName,
        input.saveWide.looseSlotResources.at({"savenfo", ResType::Res}));
    if (input.screenshot) {
        expected.files.emplace(kScreenshotName, *input.screenshot);
    }
    expected.files.insert(
        input.loosePassthrough.begin(), input.loosePassthrough.end());
    auto finalIds = candidate.deterministicResourceIds();
    expected.outerIds = std::set<ResourceId>(finalIds.begin(), finalIds.end());
    expected.activeModule = input.currentModule.target;
    expected.moduleName = input.saveWide.moduleName;
    expected.areaName = input.saveWide.areaName;
    expected.gameId = input.saveWide.gameId;
    expected.screenshotPresent = input.screenshot.has_value();
    return expected;
}

ValidatedSlot validateSlot(
    const std::filesystem::path &directory,
    const ExpectedSlot *expected = nullptr) {
    if (!std::filesystem::is_directory(directory) ||
        std::filesystem::is_symlink(directory)) {
        throw std::runtime_error("Published slot is not a safe directory");
    }
    for (const auto *name : {kArchiveName, kGlobalsName, kPartyName, kNfoName}) {
        auto path = directory / name;
        if (!std::filesystem::is_regular_file(path) ||
            std::filesystem::is_symlink(path)) {
            throw std::runtime_error(std::string("Published slot lacks ") + name);
        }
    }
    auto globals = readGff(readFile(directory / kGlobalsName), "GVT V3.2");
    (void)resource::parseGVT(*globals);
    readGff(readFile(directory / kPartyName), "PT  V3.2");
    auto nfo = readGff(readFile(directory / kNfoName), "NFO V3.2");
    (void)resource::parseNFO(*nfo);
    if (std::filesystem::exists(directory / kScreenshotName)) {
        validateTga(readFile(directory / kScreenshotName));
    }

    auto state = std::make_shared<const SaveWorkingState>(directory / kArchiveName);
    if (!state->contains({"inventory", ResType::Res}) ||
        !state->contains({"repute", ResType::Fac})) {
        throw std::runtime_error("Published outer archive lacks required save-wide state");
    }
    readGff(state->find({"inventory", ResType::Res})->data, "INV V3.2");
    readGff(state->find({"repute", ResType::Fac})->data, "FAC V3.2");
    for (const auto &id : state->resourceIds()) {
        if (id.type == ResType::Utc) {
            readGff(state->find(id)->data, "UTC V3.2");
        }
    }

    size_t nestedModules = 0;
    for (const auto &id : state->resourceIds()) {
        if (id.type == ResType::Sav) {
            nestedModules += validateNestedModule(state->find(id)->data);
        }
    }
    if (nestedModules == 0) {
        throw std::runtime_error("Published outer archive contains no saved module");
    }

    if (expected) {
        for (const auto &[name, bytes] : expected->files) {
            auto path = directory / name;
            if (!std::filesystem::is_regular_file(path) ||
                readFile(path) != bytes) {
                throw std::runtime_error(
                    "Published file differs from prepared candidate: " + name);
            }
        }
        std::set<ResourceId> actual(
            state->resourceIds().begin(), state->resourceIds().end());
        if (actual != expected->outerIds) {
            throw std::runtime_error("Published outer archive identity set differs");
        }
        if (!state->contains(expected->activeModule) ||
            state->contains(
                {expected->activeModule.resRef, ResType::Rsv})) {
            throw std::runtime_error("Published active module topology is invalid");
        }
        if (lower(nfo->getString("LASTMODULE")) != lower(expected->moduleName)) {
            throw std::runtime_error("Published metadata cross-link is invalid");
        }
        auto active = state->find(expected->activeModule);
        ErfResourceContainer module {Storage(ByteBuffer(active->data))};
        module.init();
        auto ifoBytes = module.findResourceData({"module", ResType::Ifo});
        auto ifo = readGff(*ifoBytes, "IFO V3.2");
        ResourceId areId;
        ResourceId gitId;
        for (const auto &id : module.resourceIds()) {
            if (id.type == ResType::Are) areId = id;
            if (id.type == ResType::Git) gitId = id;
        }
        if (areId.resRef.value() != lower(expected->areaName) ||
            gitId.resRef != areId.resRef) {
            throw std::runtime_error(
                "Published active ARE/GIT identity is inconsistent");
        }
        auto are = readGff(*module.findResourceData(areId), "ARE V3.2");
        auto git = readGff(*module.findResourceData(gitId), "GIT V3.2");
        (void)resource::generated::parseIFO(*ifo);
        (void)resource::generated::parseARE(*are);
        (void)resource::generated::parseGIT(*git);
        auto areaList = ifo->getList("Mod_Area_list");
        if (lower(ifo->getString("Mod_Entry_Area")) !=
                lower(expected->areaName) ||
            areaList.size() != 1 ||
            lower(areaList.front()->getString("Area_Name")) !=
                lower(expected->areaName) ||
            areaList.front()->getUint("ObjectId", UINT32_MAX) !=
                ifo->getUint("Mod_Area", UINT32_MAX) ||
            ifo->getUint("Mod_NextObjId0") < 2) {
            throw std::runtime_error("Published active module cross-link is invalid");
        }
        constexpr uint32_t invalidObjectId = 0x7f000000u;
        uint32_t nextId = ifo->getUint("Mod_NextObjId0");
        if (nextId < 2 || nextId >= invalidObjectId) {
            throw std::runtime_error("Published Mod_NextObjId0 is outside the ordinary namespace");
        }
        std::set<uint32_t> ordinaryIds;
        std::set<uint32_t> reservedPartyIds;
        auto addOrdinaryId = [&ordinaryIds, invalidObjectId](const std::shared_ptr<Gff> &record,
                                            const char *kind) {
            auto id = record->getUint("ObjectId", invalidObjectId);
            if (id >= invalidObjectId || !ordinaryIds.insert(id).second) {
                throw std::runtime_error(std::string("Published ") + kind +
                                         " has an invalid or duplicate object ID");
            }
        };
        auto addPartyId = [&addOrdinaryId, &reservedPartyIds, invalidObjectId](
                              const std::shared_ptr<Gff> &record, const char *kind) {
            auto id = record->getUint("ObjectId", invalidObjectId);
            if (id < invalidObjectId) {
                addOrdinaryId(record, kind);
            } else if (id == invalidObjectId || id > 0x7fffffffu ||
                       !reservedPartyIds.insert(id).second) {
                throw std::runtime_error(std::string("Published ") + kind +
                                         " has an invalid or duplicate reserved ID");
            }
        };
        auto addItems = [&addOrdinaryId](const std::shared_ptr<Gff> &owner,
                                         bool includeInventory) {
            for (const auto &item : owner->getList("Equip_ItemList")) {
                addOrdinaryId(item, "equipped item");
            }
            if (!includeInventory) return;
            for (const auto &item : owner->getList("ItemList")) {
                addOrdinaryId(item, "contained item");
            }
        };
        auto areaObjectId = ifo->getUint("Mod_Area", invalidObjectId);
        if (areaObjectId >= invalidObjectId ||
            !ordinaryIds.insert(areaObjectId).second) {
            throw std::runtime_error("Published active area has an invalid world ID");
        }
        static const std::array<const char *, 9> lists {
            "Creature List", "Door List", "Placeable List", "TriggerList",
            "Encounter List", "StoreList", "WaypointList", "SoundList", "List"};
        for (const char *label : lists) {
            for (const auto &record : git->getList(label)) {
                addOrdinaryId(record, "GIT object");
                if (std::string(label) == "Creature List") addItems(record, true);
                if (std::string(label) == "Placeable List" ||
                    std::string(label) == "StoreList") {
                    for (const auto &item : record->getList("ItemList")) {
                        addOrdinaryId(item, "contained item");
                    }
                }
            }
        }
        for (const auto &record : ifo->getList("Mod_PlayerList")) {
            addPartyId(record, "module player");
            if (!record->getList("ItemList").empty()) {
                throw std::runtime_error("Published module player duplicates shared inventory");
            }
            addItems(record, false);
        }
        for (const auto &record : ifo->getList("Creature List")) {
            addPartyId(record, "limbo creature");
            addItems(record, true);
        }
        if (!ordinaryIds.empty() && nextId <= *ordinaryIds.rbegin()) {
            throw std::runtime_error(
                "Published Mod_NextObjId0 does not advance beyond ordinary object IDs");
        }
        auto screenshot = std::filesystem::exists(directory / kScreenshotName);
        if (screenshot != expected->screenshotPresent) {
            throw std::runtime_error("Published screenshot presence is wrong");
        }
    }
    return {std::move(state), nestedModules};
}

SaveSlotManifest makeManifest(
    const ExpectedSlot &expected, size_t nestedModules) {
    SaveSlotManifest manifest;
    for (const auto &[name, bytes] : expected.files) {
        manifest.files.push_back({name, bytes.size(), digest(bytes)});
    }
    manifest.outerResourceCount = expected.outerIds.size();
    manifest.nestedModuleCount = nestedModules;
    manifest.activeModule = expected.activeModule;
    manifest.gameId = expected.gameId;
    manifest.screenshotPresent = expected.screenshotPresent;
    return manifest;
}

void writeMarker(
    const std::filesystem::path &path, const TransactionMarker &marker) {
    std::ostringstream text;
    text << kMarkerMagic << '\n'
         << static_cast<int>(marker.phase) << '\n'
         << marker.targetName << '\n'
         << marker.candidateName << '\n'
         << marker.backupName << '\n';
    auto value = text.str();
    auto next = path;
    next += ".next";
    writeFile(next, ByteBuffer(value.begin(), value.end()));
    try {
#ifdef _WIN32
        if (!MoveFileExW(
                next.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error(
                "Unable to atomically replace save transaction marker: " +
                std::to_string(GetLastError()));
        }
#else
        std::filesystem::rename(next, path);
#endif
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(next, error);
        throw;
    }
}

TransactionMarker readMarker(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(path) ||
        std::filesystem::is_symlink(path)) {
        throw std::runtime_error("Save transaction marker is not a safe file");
    }
    auto bytes = readFile(path);
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string magic;
    std::string phase;
    TransactionMarker marker;
    if (!std::getline(stream, magic) || magic != kMarkerMagic ||
        !std::getline(stream, phase) ||
        !std::getline(stream, marker.targetName) ||
        !std::getline(stream, marker.candidateName) ||
        !std::getline(stream, marker.backupName)) {
        throw std::runtime_error("Malformed save transaction marker");
    }
    int value = 0;
    try {
        size_t used = 0;
        value = std::stoi(phase, &used);
        if (used != phase.size()) {
            throw std::runtime_error("phase");
        }
    } catch (...) {
        throw std::runtime_error("Malformed save transaction phase");
    }
    if (value < static_cast<int>(TransactionPhase::CandidateValidated) ||
        value > static_cast<int>(TransactionPhase::NewTargetReopened)) {
        throw std::runtime_error("Unsupported save transaction phase");
    }
    marker.phase = static_cast<TransactionPhase>(value);
    if (!isSafeSiblingName(marker.targetName) ||
        !isSafeSiblingName(marker.candidateName) ||
        (!marker.backupName.empty() && !isSafeSiblingName(marker.backupName))) {
        throw std::runtime_error("Unsafe path in save transaction marker");
    }
    auto candidatePrefix = "." + marker.targetName + ".reone-tmp-";
    auto backupPrefix = "." + marker.targetName + ".reone-bak-";
    if (marker.candidateName.rfind(candidatePrefix, 0) != 0) {
        throw std::runtime_error("Candidate path is not bound to marker target");
    }
    auto identity = marker.candidateName.substr(candidatePrefix.size());
    if (identity.size() != 32 ||
        !std::all_of(identity.begin(), identity.end(), [](unsigned char value) {
            return std::isxdigit(value) != 0;
        }) ||
        (!marker.backupName.empty() &&
         marker.backupName != backupPrefix + identity)) {
        throw std::runtime_error("Transaction sibling identity is invalid");
    }
    return marker;
}

SaveSlotPublishResult failure(
    SaveSlotPublishError error, std::string message) {
    SaveSlotPublishResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

void removeTree(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        return;
    }
    if (!std::filesystem::is_directory(path) ||
        std::filesystem::is_symlink(path)) {
        throw std::runtime_error("Refusing to remove unsafe transaction path");
    }
    std::filesystem::remove_all(path);
}

class ActiveTargetGuard {
public:
    explicit ActiveTargetGuard(std::string target) : _target(std::move(target)) {
        _active = activeTargets.insert(_target).second;
    }
    ~ActiveTargetGuard() {
        if (_active) {
            activeTargets.erase(_target);
        }
    }
    bool active() const { return _active; }

private:
    std::string _target;
    bool _active {false};
};

SaveSlotPublishResult recoverInternal(const SaveSlotDescriptor &target) {
    validateDescriptor(target);
    auto markerFile = markerPath(target);
    if (!std::filesystem::exists(markerFile)) {
        if (!std::filesystem::exists(target.directory)) {
            return failure(
                SaveSlotPublishError::RecoveryFailure,
                "No transaction marker or durable target exists");
        }
        auto validated = validateSlot(target.directory);
        SaveSlotPublishResult result;
        result.publishedSlot = target;
        result.committedWorkingState = std::move(validated.state);
        result.durable = true;
        result.message = "No interrupted transaction was present";
        return result;
    }

    auto marker = readMarker(markerFile);
    if (marker.targetName != target.directory.filename().string()) {
        throw std::runtime_error("Transaction marker identifies another target");
    }
    auto parent = target.directory.parent_path();
    auto candidate = parent / marker.candidateName;
    auto backup = marker.backupName.empty()
                      ? std::filesystem::path()
                      : parent / marker.backupName;
    auto targetExists = std::filesystem::exists(target.directory);
    auto candidateExists = std::filesystem::exists(candidate);
    auto backupExists = !backup.empty() && std::filesystem::exists(backup);

    std::shared_ptr<const SaveWorkingState> state;
    std::string message;
    if (!targetExists && candidateExists && !backupExists &&
        marker.backupName.empty()) {
        // Interrupted new-slot publication before the candidate was committed.
        removeTree(candidate);
        std::filesystem::remove(markerFile);
        return failure(
            SaveSlotPublishError::RecoveryFailure,
            "Discarded an uncommitted new-slot candidate; no durable target exists");
    } else if (targetExists && candidateExists && !backupExists) {
        // Case A: the old target is still authoritative.
        state = validateSlot(target.directory).state;
        removeTree(candidate);
        message = "Discarded an uncommitted validated candidate";
    } else if (!targetExists && candidateExists && backupExists) {
        // Case B: restore the old target.
        validateSlot(backup);
        std::filesystem::rename(backup, target.directory);
        removeTree(candidate);
        state = validateSlot(target.directory).state;
        message = "Restored the old target after an interrupted backup rename";
    } else if (targetExists && !candidateExists && backupExists) {
        // Case C: prefer a structurally valid new target, otherwise roll back.
        try {
            state = validateSlot(target.directory).state;
            removeTree(backup);
            message = "Finalized a valid newly published target";
        } catch (...) {
            validateSlot(backup);
            removeTree(target.directory);
            std::filesystem::rename(backup, target.directory);
            state = validateSlot(target.directory).state;
            message = "Rejected an invalid new target and restored the old target";
        }
    } else if (targetExists && !candidateExists && !backupExists) {
        // A new-slot target after rename, or a stale marker after cleanup.
        try {
            state = validateSlot(target.directory).state;
            message = marker.phase >= TransactionPhase::NewTargetPublished
                          ? "Finalized a valid newly published slot"
                          : "Removed a stale transaction marker";
        } catch (...) {
            if (marker.phase >= TransactionPhase::NewTargetPublished &&
                marker.backupName.empty()) {
                removeTree(target.directory);
                std::filesystem::remove(markerFile);
            }
            throw;
        }
    } else if (targetExists && candidateExists && backupExists) {
        // A lagging phase write after backup: target is the new candidate only
        // if its bytes validate. Candidate itself is redundant in either case.
        try {
            state = validateSlot(target.directory).state;
            removeTree(candidate);
            removeTree(backup);
            message = "Finalized a valid target from a lagging transaction phase";
        } catch (...) {
            validateSlot(backup);
            removeTree(target.directory);
            std::filesystem::rename(backup, target.directory);
            removeTree(candidate);
            state = validateSlot(target.directory).state;
            message = "Restored the old target from a lagging transaction phase";
        }
    } else {
        throw std::runtime_error(
            "Transaction artifacts do not describe a recoverable slot state");
    }
    std::filesystem::remove(markerFile);

    SaveSlotPublishResult result;
    result.publishedSlot = target;
    result.committedWorkingState = std::move(state);
    result.durable = true;
    result.message = std::move(message);
    return result;
}

} // namespace

SaveSlotPublishResult SaveSlotPublisher::recover(
    const SaveSlotDescriptor &target) const noexcept {
    try {
        ActiveTargetGuard guard(lower(
            target.directory.lexically_normal().string()));
        if (!guard.active()) {
            return failure(
                SaveSlotPublishError::Busy,
                "Another save publication is active for this target");
        }
        return recoverInternal(target);
    } catch (const std::exception &e) {
        return failure(SaveSlotPublishError::RecoveryFailure, e.what());
    } catch (...) {
        return failure(
            SaveSlotPublishError::RecoveryFailure,
            "Unknown interrupted-save recovery failure");
    }
}

SaveSlotPublishResult SaveSlotPublisher::publish(
    SaveSlotPackageInput input) const noexcept {
    std::filesystem::path candidatePath;
    std::filesystem::path backupPath;
    std::filesystem::path markerFile;
    try {
        try {
            validateExpectedInput(input);
        } catch (const std::exception &e) {
            return failure(SaveSlotPublishError::InvalidInput, e.what());
        }
        auto targetKey = lower(
            input.target.directory.lexically_normal().string());
        ActiveTargetGuard guard(targetKey);
        if (!guard.active()) {
            return failure(
                SaveSlotPublishError::Busy,
                "Another save publication is active for this target");
        }

        markerFile = markerPath(input.target);
        if (std::filesystem::exists(markerFile)) {
            auto recovered = recoverInternal(input.target);
            if (!recovered) {
                recovered.error = SaveSlotPublishError::RecoveryFailure;
                return recovered;
            }
        }

        ExpectedSlot expected;
        try {
            expected = compose(input);
        } catch (const std::exception &e) {
            return failure(SaveSlotPublishError::CompositionFailure, e.what());
        }
        if (inject(input, SaveSlotPublishCheckpoint::AfterOuterPackaging)) {
            return failure(
                SaveSlotPublishError::PackagingFailure,
                "Injected failure after outer archive packaging");
        }

        auto parent = input.target.directory.parent_path();
        auto suffix = uniqueSuffix();
        auto targetName = input.target.directory.filename().string();
        candidatePath = parent / ("." + targetName + ".reone-tmp-" + suffix);
        backupPath = parent / ("." + targetName + ".reone-bak-" + suffix);
        if (std::filesystem::exists(candidatePath) ||
            std::filesystem::exists(backupPath) ||
            !std::filesystem::create_directory(candidatePath)) {
            return failure(
                SaveSlotPublishError::CandidateWriteFailure,
                "Unable to reserve unique transaction sibling paths");
        }

        size_t written = 0;
        for (const auto &[name, bytes] : expected.files) {
            writeFile(candidatePath / name, bytes);
            if (++written == 1 &&
                inject(input, SaveSlotPublishCheckpoint::DuringCandidateWrite)) {
                removeTree(candidatePath);
                return failure(
                    SaveSlotPublishError::CandidateWriteFailure,
                    "Injected failure during candidate write");
            }
        }
        if (inject(input, SaveSlotPublishCheckpoint::BeforeCandidateValidation)) {
            removeTree(candidatePath);
            return failure(
                SaveSlotPublishError::CandidateValidationFailure,
                "Injected failure before candidate validation");
        }
        validateSlot(candidatePath, &expected);

        TransactionMarker marker;
        marker.targetName = targetName;
        marker.candidateName = candidatePath.filename().string();
        const bool replacingTarget =
            std::filesystem::exists(input.target.directory);
        marker.backupName = replacingTarget
                                ? backupPath.filename().string()
                                : std::string();
        marker.phase = TransactionPhase::CandidateValidated;
        writeMarker(markerFile, marker);
        if (inject(input, SaveSlotPublishCheckpoint::AfterCandidateValidation)) {
            if (!input.leaveRecoveryStateOnInjectedFailure) {
                auto restored = recoverInternal(input.target);
                (void)restored;
            }
            return failure(
                SaveSlotPublishError::TransactionFailure,
                "Injected interruption after candidate validation");
        }

        if (replacingTarget) {
            std::filesystem::rename(input.target.directory, backupPath);
            marker.phase = TransactionPhase::OldTargetBackedUp;
            writeMarker(markerFile, marker);
            if (inject(input, SaveSlotPublishCheckpoint::AfterTargetBackup)) {
                if (!input.leaveRecoveryStateOnInjectedFailure) {
                    auto restored = recoverInternal(input.target);
                    (void)restored;
                }
                return failure(
                    SaveSlotPublishError::TransactionFailure,
                    "Injected interruption after target backup");
            }
        }

        std::filesystem::rename(candidatePath, input.target.directory);
        marker.phase = TransactionPhase::NewTargetPublished;
        writeMarker(markerFile, marker);
        if (inject(input, SaveSlotPublishCheckpoint::CorruptPublishedTarget)) {
            writeFile(input.target.archive, ByteBuffer {'b', 'a', 'd'});
        }
        if (inject(input, SaveSlotPublishCheckpoint::AfterTargetPublish) ||
            inject(input, SaveSlotPublishCheckpoint::CorruptPublishedTarget)) {
            if (input.leaveRecoveryStateOnInjectedFailure) {
                return failure(
                    SaveSlotPublishError::TransactionFailure,
                    "Injected interruption after target publication");
            }
            auto recovered = recoverInternal(input.target);
            if (recovered &&
                inject(input, SaveSlotPublishCheckpoint::AfterTargetPublish)) {
                recovered.message =
                    "Publication completed through interrupted-transaction recovery";
                return recovered;
            }
            return failure(
                SaveSlotPublishError::PublishedValidationFailure,
                "Invalid published target was rolled back");
        }

        ValidatedSlot published;
        try {
            published = validateSlot(input.target.directory, &expected);
        } catch (const std::exception &e) {
            auto recovered = recoverInternal(input.target);
            (void)recovered;
            return failure(
                SaveSlotPublishError::PublishedValidationFailure, e.what());
        }

        // The returned state above was constructed from the durable target.
        // Representative reads ensure it is usable before old bytes disappear.
        auto active = published.state->find(expected.activeModule);
        auto inventory = published.state->find({"inventory", ResType::Res});
        auto repute = published.state->find({"repute", ResType::Fac});
        if (!active || !inventory || !repute) {
            auto recovered = recoverInternal(input.target);
            (void)recovered;
            return failure(
                SaveSlotPublishError::ReopenFailure,
                "Reopened durable state failed representative reads");
        }
        marker.phase = TransactionPhase::NewTargetReopened;
        writeMarker(markerFile, marker);

        SaveSlotPublishResult result;
        result.publishedSlot = input.target;
        result.committedWorkingState = std::move(published.state);
        result.manifest = makeManifest(expected, published.nestedModules);
        result.durable = true;
        result.message = "Durable save slot published and reopened";

        if (inject(input, SaveSlotPublishCheckpoint::BeforeBackupCleanup)) {
            result.error = SaveSlotPublishError::CleanupFailure;
            result.cleanupPending = true;
            result.message =
                "Durable save published; transaction cleanup remains pending";
            return result;
        }
        try {
            if (!marker.backupName.empty()) {
                removeTree(backupPath);
            }
            std::filesystem::remove(markerFile);
        } catch (const std::exception &e) {
            result.error = SaveSlotPublishError::CleanupFailure;
            result.cleanupPending = true;
            result.message = std::string(
                "Durable save published; cleanup remains pending: ") + e.what();
        }
        return result;
    } catch (const std::invalid_argument &e) {
        return failure(SaveSlotPublishError::InvalidInput, e.what());
    } catch (const std::exception &e) {
        // Before a marker exists, an internally named candidate is safe to
        // discard. Once a marker exists, leave artifacts for explicit recovery.
        try {
            if (!markerFile.empty() && !std::filesystem::exists(markerFile) &&
                !candidatePath.empty()) {
                removeTree(candidatePath);
            }
        } catch (...) {
        }
        return failure(SaveSlotPublishError::TransactionFailure, e.what());
    } catch (...) {
        return failure(
            SaveSlotPublishError::TransactionFailure,
            "Unknown durable save publication failure");
    }
}

} // namespace game
} // namespace reone
