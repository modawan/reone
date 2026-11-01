/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/game/game.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <exception>
#include <numeric>
#include <optional>

#include <chrono>
#include <thread>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <imgui.h>

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/combat.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/races.h"
#include "reone/game/d20/spells.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/location.h"
#include "reone/game/minigame.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/player.h"
#include "reone/game/projectiles.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/savedruntime.h"
#include "reone/game/savewidesnapshot.h"
#include "reone/game/script/routines.h"
#include "reone/game/surfaces.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/gui/guis.h"
#include "reone/movie/format/bikreader.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/parser/gff/nfo.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/cursors.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/movies.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/provider/soundsets.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/graphs.h"
#include "reone/scene/render/pipeline.h"
#include "reone/script/di/services.h"
#include "reone/system/binarywriter.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/smallset.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/threadutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

ModuleLoadContext resolveModuleLoadContext(
    bool initialSaveRestore,
    bool savedModuleSnapshot) {

    if (initialSaveRestore) {
        return savedModuleSnapshot
                   ? ModuleLoadContext::InitialSaveRestore
                   : ModuleLoadContext::InitialTemplateRestore;
    }
    return savedModuleSnapshot
               ? ModuleLoadContext::SavedModuleTransition
               : ModuleLoadContext::FreshModule;
}

bool restoresSavedWorld(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialSaveRestore ||
           context == ModuleLoadContext::SavedModuleTransition;
}

bool restoresSavedSession(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialTemplateRestore ||
           context == ModuleLoadContext::InitialSaveRestore;
}

bool preservesSavedPlacement(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialSaveRestore;
}

bool Game::bindEffectCreator(EffectInstance &effect) const {
    if (effect._runtimeSession &&
        *effect._runtimeSession != _runtimeSessionGeneration) {
        effect.creator.reset();
        for (auto &object : effect.objectParameterObjects) {
            object.reset();
        }
        return false;
    }
    const auto &serializedContext = effect.serializedReferenceContext;
    if (!effect._runtimeSession) {
        effect._runtimeSession = _runtimeSessionGeneration;
    }
    auto resolve = [this, &serializedContext](uint32_t id) {
        return serializedContext
                   ? resolveSerializedObjectReference(id, *serializedContext)
                   : getObjectById(id);
    };
    bool allBound = true;
    if (effect.creatorId != kSavedEffectInvalidObjectId &&
        effect.creatorId != kSavedRuntimeInvalidObjectId) {
        allBound = effect.bindCreator(resolve(effect.creatorId));
    }
    for (size_t index = 0; index < effect.objectParameters.size(); ++index) {
        uint32_t id = effect.objectParameters[index];
        if (id == kSavedEffectInvalidObjectId ||
            id == kSavedRuntimeInvalidObjectId) {
            continue;
        }
        allBound = effect.bindObjectParameter(index, resolve(id)) &&
                   allBound;
    }
    if (serializedContext) {
        // Object identities are stable: once bound, the creator and object
        // parameters are the identities of the objects they name, and a
        // reference with no object names none.
        auto rebase = [](uint32_t &id, const std::shared_ptr<Object> &object) {
            if (id == kSavedEffectInvalidObjectId || id == kSavedRuntimeInvalidObjectId) {
                return;
            }
            id = object ? object->id() : kSavedEffectInvalidObjectId;
        };
        rebase(effect.creatorId, effect.boundCreator());
        for (size_t index = 0; index < effect.objectParameters.size(); ++index) {
            rebase(effect.objectParameters[index], effect.boundObjectParameter(index));
        }
        effect.serializedReferenceContext.reset();
    }
    return allBound;
}

bool Game::bindSavedObjectReference(SavedObjectReference &reference) const {
    if (reference._runtimeSession && *reference._runtimeSession != _runtimeSessionGeneration) {
        reference._object.reset();
        return false;
    }
    if (reference.isInvalid() || reference.isRawValue()) {
        reference._object.reset();
        return false;
    }
    if (!reference._runtimeSession) {
        reference._runtimeSession = _runtimeSessionGeneration;
    }
    const auto &serializedContext = reference.serializedIdentityContext();
    const bool moduleGraphReference =
        serializedContext &&
        serializedContext->domain == SerializedIdentityDomain::ModuleGraph;
    if (moduleGraphReference && reference._savedGraph &&
        *reference._savedGraph != _savedGraphGeneration) {
        reference._object.reset();
        return false;
    }
    if (moduleGraphReference && !reference._savedGraph) {
        reference._savedGraph = _savedGraphGeneration;
    }

    auto object = serializedContext
                      ? resolveSerializedObjectReference(
                            reference.id, *serializedContext)
                      : getObjectById(reference.id);
    if (!object) {
        reference._object.reset();
        return false;
    }
    reference._object = object;
    return true;
}

std::shared_ptr<Object> Game::resolveSerializedObjectReference(
    uint32_t id,
    const SerializedIdentityContext &identityContext) const {
    switch (identityContext.domain) {
    case SerializedIdentityDomain::ModuleGraph:
        if (!_reservedSavedIdentityNamespace ||
            *_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
            return nullptr;
        }
        return getObjectBySavedId(id);
    case SerializedIdentityDomain::Template:
        // ObjectId-shaped blueprint fields are not references into a live serialized graph.
        return nullptr;
    case SerializedIdentityDomain::DetachedRecord: {
        std::shared_ptr<Object> result;
        for (const auto &[_, candidate] : _objectById) {
            auto identity = candidate->serializedObjectIdentity();
            if (!identity || !(identity->context == identityContext) ||
                identity->id != id) {
                continue;
            }
            if (result && result.get() != candidate.get()) {
                throw ValidationException(
                    "Ambiguous detached-record object reference " +
                    identityContext.identityNamespace + ":" +
                    std::to_string(id));
            }
            result = candidate;
        }
        return result;
    }
    }
    return nullptr;
}

static constexpr char kDeveloperOverlayToggleHelp[] = "Ctrl+Shift+D";
static constexpr char kDeveloperTriggerToggleHelp[] = "Ctrl+Shift+T";
static constexpr char kDeveloperActorToggleHelp[] = "Ctrl+Shift+A";
static constexpr char kDeveloperActorLongToggleHelp[] = "Ctrl+Shift+L";
static constexpr char kDeveloperWatchToggleHelp[] = "Ctrl+Shift+W";
static constexpr float kDeveloperActorLabelDistance = 32.0f;

static std::shared_ptr<Gff> decodeSaveGff(std::optional<Resource> resource) {
    if (!resource) {
        return {};
    }
    MemoryInputStream stream(resource->data);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

static const char *screenName(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::None:
        return "None";
    case Game::Screen::MainMenu:
        return "MainMenu";
    case Game::Screen::Loading:
        return "Loading";
    case Game::Screen::CharacterGeneration:
        return "CharacterGeneration";
    case Game::Screen::InGame:
        return "InGame";
    case Game::Screen::InGameMenu:
        return "InGameMenu";
    case Game::Screen::Conversation:
        return "Conversation";
    case Game::Screen::Container:
        return "Container";
    case Game::Screen::PartySelection:
        return "PartySelection";
    case Game::Screen::SaveLoad:
        return "SaveLoad";
    case Game::Screen::GalaxyMap:
        return "GalaxyMap";
    case Game::Screen::SwoopRace:
        return "SwoopRace";
    case Game::Screen::PazaakWager:
        return "PazaakWager";
    case Game::Screen::PazaakSetup:
        return "PazaakSetup";
    case Game::Screen::PazaakBoard:
        return "PazaakBoard";
    default:
        return "Unknown";
    }
}

// The menus and panels that pause the world while they are shown over a
// running module; levelling up is the only character generation with a
// module behind it.
static bool pausesWorld(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::CharacterGeneration:
    case Game::Screen::InGameMenu:
    case Game::Screen::Container:
    case Game::Screen::PartySelection:
    case Game::Screen::SaveLoad:
    case Game::Screen::GalaxyMap:
        return true;
    default:
        return false;
    }
}

// The screens shown over a running module. Pazaak does not pause the world,
// but the world waits behind it here, apart from due events. The minigames
// hold the module instead.
static bool isMenuOverModule(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::PazaakWager:
    case Game::Screen::PazaakSetup:
    case Game::Screen::PazaakBoard:
        return true;
    default:
        return pausesWorld(screen);
    }
}

bool Game::holdsWorld() const {
    return _paused || pausesWorld(_screen);
}

static pazaak::HandSelection randomPazaakHandSelection(const pazaak::SideDeck &) {
    std::array<size_t, pazaak::kSideDeckSize> indices;
    std::iota(indices.begin(), indices.end(), 0);
    for (size_t i = indices.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(indices[i], indices[swapIndex]);
    }
    return {indices[0], indices[1], indices[2], indices[3]};
}

// Developer-only KotOR II showcase hand selection. The showcase side deck is
// built in a fixed order, so rotating the four-card window across sets exposes
// every supported card family during a single match while still obeying the
// ordinary ten-card side deck and four-card hand rules.
static PazaakSession::HandSelector showcasePazaakHandSelector() {
    auto set = std::make_shared<size_t>(0);
    return [set](const pazaak::SideDeck &) {
        static const std::array<pazaak::HandSelection, 3> windows {
            pazaak::HandSelection {0, 1, 2, 3},
            pazaak::HandSelection {4, 5, 6, 7},
            pazaak::HandSelection {8, 9, 0, 1},
        };
        pazaak::HandSelection selection = windows[*set % windows.size()];
        ++*set;
        return selection;
    };
}

static pazaak::MainDeck randomPazaakMainDeck() {
    std::vector<int> cards(pazaak::MainDeck::standardOrdered().cards());
    for (size_t i = cards.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(cards[i], cards[swapIndex]);
    }
    return pazaak::MainDeck(std::move(cards));
}

// KotOR II owns five extra card types after the numbered ones. Their order
// follows the authored side-deck screen: Tiebreaker, Double, Flip 2&4, Flip 3&6
// and Value Change.
static std::optional<pazaak::CardDefinition> getPazaakCardDefinition(int cardId, bool tsl) {
    if (cardId < 0 || cardId >= (tsl ? 23 : 18)) {
        return std::nullopt;
    }
    if (cardId >= 18) {
        switch (cardId) {
        case 18:
            return pazaak::CardDefinition::tiebreaker();
        case 19:
            return pazaak::CardDefinition::doubleCard();
        case 20:
            return pazaak::CardDefinition::flipTwoFour();
        case 21:
            return pazaak::CardDefinition::flipThreeSix();
        default:
            return pazaak::CardDefinition::valueChange();
        }
    }
    int magnitude = cardId % 6 + 1;
    if (cardId < 6) {
        return pazaak::CardDefinition::fixedPositive(magnitude);
    }
    if (cardId < 12) {
        return pazaak::CardDefinition::fixedNegative(magnitude);
    }
    return pazaak::CardDefinition::signSelectable(magnitude);
}

// The card a pazaakdecks.2da cell names. The first character picks the kind:
// '-' and '*' for the negative and sign-selectable cards and, in KotOR II, '$'
// for Double and T, F and V (in either case) for Tiebreaker, the Flip cards
// (F1 is Flip 2&4, any other Flip 3&6) and Value Change. Any other first
// character is a positive card. The second character is the card's value.
static std::optional<pazaak::CardDefinition> parsePazaakDeckCard(
    const std::string &token, bool tsl) {

    const char kind = token.empty() ? '\0' : token[0];
    const char value = token.size() > 1 ? token[1] : '\0';
    int cardId = value - '1';
    if (kind == '-') {
        cardId = value - '1' + 6;
    } else if (kind == '*') {
        cardId = value - '1' + 12;
    } else if (tsl) {
        switch (kind) {
        case '$':
            cardId = 19;
            break;
        case 'T':
        case 't':
            cardId = 18;
            break;
        case 'F':
        case 'f':
            cardId = value == '1' ? 20 : 21;
            break;
        case 'V':
        case 'v':
            cardId = 22;
            break;
        default:
            break;
        }
    }
    return getPazaakCardDefinition(cardId, tsl);
}

// The opponent's side deck is a row's first ten columns; a negative row is a
// random one.
static std::optional<pazaak::SideDeck> loadPazaakOpponentDeck(
    const resource::TwoDA &decks,
    int row, bool tsl) {

    if (row < 0 && decks.getRowCount() > 0) {
        row = randomInt(0, decks.getRowCount() - 1);
    }
    if (row < 0 || row >= decks.getRowCount()) {
        return std::nullopt;
    }
    std::vector<pazaak::CardDefinition> cards;
    cards.reserve(pazaak::kSideDeckSize);
    for (size_t i = 0; i < pazaak::kSideDeckSize; ++i) {
        auto card = parsePazaakDeckCard(decks.getString(row, static_cast<int>(i)), tsl);
        if (!card) {
            return std::nullopt;
        }
        cards.push_back(*card);
    }
    return pazaak::SideDeck {
        cards[0], cards[1], cards[2], cards[3], cards[4],
        cards[5], cards[6], cards[7], cards[8], cards[9],
    };
}

static const char *cameraTypeName(CameraType type) {
    switch (type) {
    case CameraType::FirstPerson:
        return "FirstPerson";
    case CameraType::ThirdPerson:
        return "ThirdPerson";
    case CameraType::Static:
        return "Static";
    case CameraType::Animated:
        return "Animated";
    case CameraType::Dialog:
        return "Dialog";
    default:
        return "Unknown";
    }
}

static const char *objectTypeName(ObjectType type) {
    switch (type) {
    case ObjectType::Creature:
        return "creature";
    case ObjectType::Item:
        return "item";
    case ObjectType::Trigger:
        return "trigger";
    case ObjectType::Door:
        return "door";
    case ObjectType::Waypoint:
        return "waypoint";
    case ObjectType::Placeable:
        return "placeable";
    case ObjectType::Store:
        return "store";
    case ObjectType::Encounter:
        return "encounter";
    case ObjectType::Sound:
        return "sound";
    case ObjectType::Module:
        return "module";
    case ObjectType::Area:
        return "area";
    case ObjectType::Room:
        return "room";
    case ObjectType::Camera:
        return "camera";
    default:
        return "object";
    }
}

static bool isDeveloperOverlayChord(const input::KeyEvent &event) {
    bool control = (event.mod & input::KeyModifiers::control) != 0;
    bool shift = (event.mod & input::KeyModifiers::shift) != 0;
    return control && shift;
}

static const char *triggerDebugStateName(Trigger::DebugState state) {
    switch (state) {
    case Trigger::DebugState::Entered:
        return "enter";
    case Trigger::DebugState::Inside:
        return "inside";
    case Trigger::DebugState::Tested:
        return "tested";
    default:
        return "default";
    }
}

static int getDebugFaction(const std::shared_ptr<Object> &object) {
    if (!object) {
        return -1;
    }
    if (auto creature = dyn_cast<Creature>(object)) {
        return static_cast<int>(creature->faction());
    }
    if (auto door = dyn_cast<Door>(object)) {
        return static_cast<int>(door->faction());
    }
    if (auto placeable = dyn_cast<Placeable>(object)) {
        return static_cast<int>(placeable->faction());
    }
    return -1;
}

void Game::init() {
    _options.game.autoPause = AutoPauseOptions::load(_options.game.configurationPath, isTSL());
    _options.game.feedback = FeedbackOptions::load(_options.game.configurationPath);
    _options.game.clientDifficulty = loadDifficultyLevel(_options.game.configurationPath, _options.game.clientDifficulty);
    // The configuration names one of the levels the player can pick: any row
    // of the difficulty table but the last.
    if (_options.game.clientDifficulty < 0 || _options.game.clientDifficulty > _services.game.difficultyOptions.count() - 2) {
        throw ValidationException("Invalid difficulty level in configuration: " + std::to_string(_options.game.clientDifficulty));
    }
    _options.game.mouse = MouseOptions::load(_options.game.configurationPath);
    initConsole();
    initTables();
    resetGalaxyMap();
    initLocalServices();
    setSceneSurfaces();
    setCursorType(CursorType::Default);

    _moduleNames = _services.resource.director.moduleNames();
    _saveNames = _services.resource.director.saveNames();

    // No movie plays at start-up: the main menu opens, and its music starts,
    // at once.
    openMainMenu();
}

// The number of strings a dialogue token may give, strref1 to strref4.
static constexpr int kStringTokenStrRefCount = 4;

void Game::initTables() {
    auto &twoDas = _services.resource.twoDas;
    const bool tsl = isTSL();

    // TSL's table suffixes its colour columns with _pc.
    auto effects = getRequiredTwoDA(twoDas, "videoeffects");
    _videoEffects.clear();
    for (int row = 0; row < effects->getRowCount(); ++row) {
        VideoEffectRow effect;
        effect.scanNoise = effects->getInt(row, "enablescannoise") != 0;
        effect.saturation = effects->getInt(row, "enablesaturation") != 0;
        effect.clairvoyance = effects->getInt(row, "enableclairvoyance") != 0 ||
                              effects->getInt(row, "enableclairvoyancefull") != 0;
        effect.forceSight = effects->getInt(row, "enableforcesight") != 0;
        effect.fury = effects->getInt(row, "enablefury");
        effect.modulation = glm::vec3(
            effects->getFloat(row, tsl ? "modulationred_pc" : "modulationred"),
            effects->getFloat(row, tsl ? "modulationgreen_pc" : "modulationgreen"),
            effects->getFloat(row, tsl ? "modulationblue_pc" : "modulationblue"));
        effect.saturationAmount = effects->getFloat(row, tsl ? "saturation_pc" : "saturation");
        _videoEffects.push_back(effect);
    }

    auto tokens = getRequiredTwoDA(twoDas, "stringtokens");
    _stringTokens.clear();
    for (int row = 0; row < tokens->getRowCount(); ++row) {
        StringTokenRow token;
        for (int i = 0; i < kStringTokenStrRefCount; ++i) {
            token.strRefs[i] = tokens->getInt(row, "strref" + std::to_string(i + 1), -1);
        }
        token.defaultStrRef = tokens->getInt(row, "default", -1);
        token.action = tokens->getInt(row, "actioncode", -1);
        _stringTokens.emplace(tokens->getString(row, "token"), token);
    }
    initInputEvents();

    // Each game reads its own message columns; the pages run while the table
    // has a column for them.
    auto tutorials = getRequiredTwoDA(twoDas, "tutorial");
    const std::string messagePrefix = tsl ? "Message_PC" : "Message";
    std::vector<std::string> messageColumns;
    for (int page = 0;; ++page) {
        auto column = messagePrefix + std::to_string(page);
        if (tutorials->getColumnIndex(column) == -1) {
            break;
        }
        messageColumns.push_back(std::move(column));
    }
    _tutorials.clear();
    for (int row = 0; row < tutorials->getRowCount(); ++row) {
        TutorialRow tutorial;
        for (const auto &column : messageColumns) {
            tutorial.messages.push_back(tutorials->getIntOpt(row, column));
        }
        tutorial.icon = tutorials->getString(row, "Icon");
        _tutorials.push_back(std::move(tutorial));
    }

    auto feedback = getRequiredTwoDA(twoDas, "feedbacktext");
    _feedbackTexts.clear();
    for (int row = 0; row < feedback->getRowCount(); ++row) {
        _feedbackTexts.push_back(feedback->getIntOpt(row, "strref"));
    }

    _trapTypes = Trap::readTypes(twoDas);
    _areaMusic.init(isTSL());
    if (isTSL()) {
        _areaMusic.setLoadMusicHandover([this](const std::string &resRef) {
            return takeLoadMusic(resRef);
        });
    }
    _party.init();
}

std::optional<int> Game::feedbackTextStrRef(int row) const {
    return row >= 0 && row < static_cast<int>(_feedbackTexts.size()) ? _feedbackTexts[row] : std::nullopt;
}

const TrapTypeRow &Game::trapType(int type) const {
    static const TrapTypeRow kBlankType;
    return type >= 0 && type < static_cast<int>(_trapTypes.size()) ? _trapTypes[type] : kBlankType;
}

void Game::initJournalNotifications() {
    _journal.setOnQuestChanged([this](const Journal::EntryChange &change) {
        submitStatusSummary(StatusSummaryCategory::Journal);
        awardPlotXPByIndex(change.plotIndex, change.xpPercentage);
    });
}

void Game::registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler) {
    static const std::set<std::string> cheatCommands {
        "playanim", "warp", "kill", "additem", "givexp", "givegold",
        "spawncreature", "spawncompanion", "setfaction", "setposition",
        "professionaltools", "killroom", "setability", "setskill",
        "addfeat", "removefeat", "addspell", "removespell",
        "castspellatobject", "opendoor", "closedoor"};
    bool marksCheatUsed = cheatCommands.count(name) != 0;
    _console.registerCommand(
        name, description,
        [this, handler, marksCheatUsed](const ConsoleArgs &args) {
            (this->*handler)(args);
            if (marksCheatUsed) {
                _cheatUsed = true;
            }
        });
}

void Game::initConsole() {
    registerConsoleCommand("info", "information on selected object", &Game::consoleInfo);
    registerConsoleCommand("listglobals", "list global variables", &Game::consoleListGlobals);
    registerConsoleCommand("listlocals", "list local variables", &Game::consoleListLocals);
    registerConsoleCommand("runscript", "run script", &Game::consoleRunScript);
    registerConsoleCommand("listanim", "list animations of selected object", &Game::consoleListAnim);
    registerConsoleCommand("playanim", "play animation on selected object", &Game::consolePlayAnim);
    registerConsoleCommand("warp", "warp to a module", &Game::consoleWarp);
    registerConsoleCommand("camera", "select camera (free)", &Game::consoleCamera);
    registerConsoleCommand("campos", "set free camera position", &Game::consoleCamPos);
    registerConsoleCommand("camlook", "aim free camera at a point", &Game::consoleCamLook);
    registerConsoleCommand("camstatus", "print free camera viewpoint commands", &Game::consoleCamStatus);
    registerConsoleCommand("openmenu", "open the main menu or an in-game menu tab", &Game::consoleOpenMenu);
    registerConsoleCommand("openchargen", "open a character-generation screen", &Game::consoleOpenCharacterGeneration);
    registerConsoleCommand("skipmovie", "skip the active movie", &Game::consoleSkipMovie);
    registerConsoleCommand("showbark", "show a timed HUD bark message", &Game::consoleShowBark);
    registerConsoleCommand("showpopup", "show the confirmation popup with an optional icon", &Game::consoleShowPopup);
    registerConsoleCommand("showgallerymode", "open a deterministic gameplay-mode gallery fixture", &Game::consoleShowGalleryMode);
    registerConsoleCommand("graphics", "toggle 3D scene rendering: graphics on|off", &Game::consoleGraphics);
    registerConsoleCommand("seed", "reseed the shared random generator: seed <number>", &Game::consoleSeed);
    registerConsoleCommand("showhud", "open the third-person gameplay HUD for a scripted capture", &Game::consoleShowHUD);
    registerConsoleCommand("showtransition", "show an area-transition banner for a scripted capture", &Game::consoleShowTransition);
    registerConsoleCommand("opencontainer", "open the container screen on the party leader for a scripted capture", &Game::consoleOpenContainer);
    registerConsoleCommand("selectdialogoption", "select a dialog option without activating it for a scripted capture", &Game::consoleSelectDialogOption);
    registerConsoleCommand("kill", "kill selected object", &Game::consoleKill);
    registerConsoleCommand("additem", "add item to selected object", &Game::consoleAddItem);
    registerConsoleCommand("givexp", "give experience to selected creature", &Game::consoleGiveXP);
    registerConsoleCommand("givegold", "give credits to the party", &Game::consoleGiveGold);
    registerConsoleCommand("showaabb", "toggle rendering AABB", &Game::consoleShowAABB);
    registerConsoleCommand("showwalkmesh", "toggle rendering walkmesh", &Game::consoleShowWalkmesh);
    registerConsoleCommand("showtriggers", "toggle rendering triggers", &Game::consoleShowTriggers);
    registerConsoleCommand("spawncreature", "spawn a creature", &Game::consoleSpawnCreature);
    registerConsoleCommand("spawncompanion", "spawn a companion", &Game::consoleSpawnCompanion);
    registerConsoleCommand("addavailablenpc", "add an NPC to the party selection roster", &Game::consoleAddAvailableNpc);
    registerConsoleCommand("selectobjectbyid", "select an object by id", &Game::consoleSelectObjectById);
    registerConsoleCommand("selectobjectbytag", "select an object by tag", &Game::consoleSelectObjectByTag);
    registerConsoleCommand("selectleader", "select the party leader", &Game::consoleSelectLeader);
    registerConsoleCommand("setfaction", "change faction of a creature", &Game::consoleSetFaction);
    registerConsoleCommand("setposition", "change position of a creature", &Game::consoleSetPosition);
    registerConsoleCommand("professionaltools", "add various combat items to the inventory", &Game::consoleProfessionalTools);
    registerConsoleCommand("killroom", "kill all hostile creatures in a room of the selected object", &Game::consoleKillRoom);
    registerConsoleCommand("autoskipenable", "enable auto-skip for conversations", &Game::consoleAutoSkipEnable);
    registerConsoleCommand("autoskipentries", "add a sequence of entries to skip", &Game::consoleAutoSkipEntries);
    registerConsoleCommand("autoskipreplies", "add a sequence of replies to pick", &Game::consoleAutoSkipReplies);
    registerConsoleCommand("startconversation", "start a conversation with the selected object or a DLG resref", &Game::consoleStartConversation);
    registerConsoleCommand("cutsceneattack", "attack an object by id with a pre-determined animation and result", &Game::consoleCutsceneAttack);
    registerConsoleCommand("setability", "set ability value (strength, dexterity, etc.)", &Game::consoleSetAbility);
    registerConsoleCommand("setskill", "set skill value (computer use, repair, etc.)", &Game::consoleSetSkill);
    registerConsoleCommand("addfeat", "add feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("removefeat", "remove feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("addspell", "add spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("removespell", "remove spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("castspellatobject", "cast spell at object", &Game::consoleCastSpellAtObject);
    registerConsoleCommand("opendoor", "open a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("closedoor", "close a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("listgames", "list savegames", &Game::consoleListGames);
    registerConsoleCommand("loadgame", "load a savegame", &Game::consoleLoadGame);
    registerConsoleCommand("savegame", "save to a semantic slot", &Game::consoleSaveGame);
    registerConsoleCommand("startpazaak", "start a development Pazaak match", &Game::consoleStartPazaak);
    registerConsoleCommand("showpath", "show debug overlay for pathfinding", &Game::consoleShowPath);

    if (_options.game.developer) {
        registerConsoleCommand("minigameinfo", "print minigame metadata for current area", &Game::consoleMiniGameInfo);
        registerConsoleCommand("startswoop", "enter the developer swoop race mode for the current area", &Game::consoleStartSwoop);
        registerConsoleCommand("stopswoop", "exit the developer swoop race mode", &Game::consoleStopSwoop);
        registerConsoleCommand("swoopstate", "print the current swoop race progress/lateral state", &Game::consoleSwoopState);
        registerConsoleCommand("startswooprace", "enter a swoop module from the current one and auto-start the race", &Game::consoleStartSwoopRace);
        registerConsoleCommand("finishswoop", "finish the lifecycle swoop race (forced success) and return to origin", &Game::consoleFinishSwoop);
        registerConsoleCommand("startturret", "enter the turret minigame for the current area", &Game::consoleStartTurret);
        registerConsoleCommand("startturretgame", "enter a turret module from the current one and auto-start the minigame", &Game::consoleStartTurretGame);
        registerConsoleCommand("stopturret", "exit the turret minigame", &Game::consoleStopTurret);
        registerConsoleCommand("turretstate", "print the current turret aim/health/enemy state", &Game::consoleTurretState);
        registerConsoleCommand("showimgui", "open imgui demo", &Game::consoleShowImGui);
    }
}

std::shared_ptr<Spell> Game::getSpell(SpellType type) const {
    return _services.game.spells.get(type);
}

void Game::initLocalServices() {
    auto routines = std::make_unique<Routines>(_gameId, this, &_services);
    routines->init();
    _routines = std::move(routines);

    _scriptRunner = std::make_unique<ScriptRunner>(*_routines, _services.resource.scripts);

    if (!_saveSeams.captureScreenshot) {
        _saveSeams.captureScreenshot = [this]() {
            return captureSaveScreenshot();
        };
    }

    _map = std::make_unique<Map>(*this, _services);
}

void Game::setSceneSurfaces() {
    auto walkable = _services.game.surfaces.getWalkableSurfaces();
    auto walkcheck = _services.game.surfaces.getWalkcheckSurfaces();
    auto lineOfSight = _services.game.surfaces.getLineOfSightSurfaces();
    for (auto &name : _services.scene.graphs.sceneNames()) {
        auto &scene = _services.scene.graphs.get(name);
        scene.setWalkableSurfaces(walkable);
        scene.setWalkcheckSurfaces(walkcheck);
        scene.setLineOfSightSurfaces(lineOfSight);
    }
}

bool Game::handle(const input::Event &event) {
    // Either Shift is the alternate actions key: pressing one holds it and
    // releasing one lets it go. KotOR does not take it while the container
    // screen is open, so it stays as it was when the screen opened.
    if ((event.type == input::EventType::KeyDown || event.type == input::EventType::KeyUp) &&
        (event.key.code == input::KeyCode::LeftShift || event.key.code == input::KeyCode::RightShift) &&
        (isTSL() || _screen != Screen::Container)) {
        _alternateActionsHeld = event.type == input::EventType::KeyDown;
    }
    if (_playerInputBlocked && event.type != input::EventType::KeyUp &&
        event.type != input::EventType::MouseButtonUp) {
        return true;
    }
    if (_credits) {
        _credits->handle(event);
        return true;
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        // The drawn pointer keeps following the mouse while the popup takes
        // the input.
        if (event.type == input::EventType::MouseMotion) {
            _pointer->setPosition({event.motion.x, event.motion.y});
        } else if ((event.type == input::EventType::MouseButtonDown || event.type == input::EventType::MouseButtonUp) &&
                   event.button.button == input::MouseButton::Left) {
            _pointer->setPressed(event.type == input::EventType::MouseButtonDown);
        }
        _confirmPopup->handle(event);
        return true;
    }

    switch (event.type) {
    case input::EventType::KeyDown:
        if (handleKeyDown(event.key)) {
            return true;
        }
        break;
    case input::EventType::MouseMotion:
        if (handleMouseMotion(event.motion)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonUp:
        if (handleMouseButtonUp(event.button)) {
            return true;
        }
        break;
    default:
        break;
    }

    if (!_movie) {
        auto gui = getScreenGUI();
        if (gui && gui->handle(event)) {
            return true;
        }
        switch (_screen) {
        case Screen::InGame: {
            if (_party.handle(event)) {
                return true;
            }
            auto camera = getActiveCamera();
            if (camera && camera->handle(event)) {
                return true;
            }
            if (_module->handle(event)) {
                return true;
            }
            break;
        }
        case Screen::SwoopRace:
            if (_swoopRace.handle(event)) {
                return true;
            }
            break;
        case Screen::Turret:
            if (_turret.handle(event)) {
                return true;
            }
            break;
        default:
            break;
        }
    }

    return false;
}

bool Game::consumeTimingDiscontinuity() {
    bool discontinuity = _timingDiscontinuity;
    _timingDiscontinuity = false;
    return discontinuity;
}

void Game::update(float frameTime) {
    // The death sequence sets how fast the world, its presentation and the
    // fade run this frame. A pending end keeps the death GUI up while its
    // delay lasts, then returns to the main menu once the game is not over.
    updateDeathSequence();
    // Every GUI's pulse runs through pauses, whatever the screen, and slows
    // with the world under the sequel's gameover panel.
    _services.gui.guis.updatePulsingAlpha(frameTime * _deathTimeScale);
    if (_endGamePending) {
        if (_endGameDelay <= 0.0f) {
            if (!_gameOver) {
                openMainMenu();
                return;
            }
        } else if (_showEndGameGui && !isDeathMessageDisplayed()) {
            displayDeathMessage();
        } else if (!isTSL()) {
            _endGameDelay -= frameTime * _gameSpeed * _deathTimeScale;
        }
    }
    // A movie runs from the frame after it is started, so the code that
    // started it carries on as if it had already finished. While it runs,
    // the fade is frozen and takes no requests.
    _globalFade.setMovieOverride(static_cast<bool>(_movie));
    // Presentation advances once, before callbacks can replace its request.
    // The driver rebases frameTime after synchronous loading; movies suspend it.
    _globalFade.update(frameTime * _deathTimeScale);
    _partyKilledAutoPauseDelay = std::max(0.0f, _partyKilledAutoPauseDelay - frameTime);
    updateDeferredAutoPause(frameTime);
    float dt = frameTime * _gameSpeed * _deathTimeScale;
    if (_movie) {
        _worldClockSample.reset();
        _areaMusic.updateMusicFade();
        updateMovie(dt);
        return;
    }
    updateMusic();

    // Requests made by scripts, console handlers or UI code in the previous
    // update execute only after those call stacks have unwound. This precedes
    // deferred module transition handling, so save+transition in one script
    // deterministically captures the source module first.
    processPendingSave();

    if (_screen == Screen::PazaakBoard && _pazaakSession) {
        static constexpr float kPazaakOpponentEventDelay = 0.45f;
        if (_pazaakSession->advanceResultPresentation(dt)) {
            if (_pazaakBoard) {
                _pazaakBoard->refresh();
            }
            completePazaakIfReady();
        } else if (_pazaakSession && !_pazaakSession->presentationPending()) {
            _pazaakOpponentEventElapsed += dt;
            if (_pazaakOpponentEventElapsed >= kPazaakOpponentEventDelay) {
                _pazaakOpponentEventElapsed = 0.0f;
                PazaakOpponentEvent event = _pazaakSession->advanceOpponentEvent();
                if (event != PazaakOpponentEvent::None) {
                    if (_pazaakBoard) {
                        _pazaakBoard->refresh();
                    }
                    completePazaakIfReady();
                }
            }
        }
    }

    if (!_nextModule.empty()) {
        loadNextModule();
    }
    // Sounds wait while a movie runs and resume once the frame's module
    // transition, if any, has been made.
    _services.audio.mixer.setSoundsHeldForMovie(static_cast<bool>(_movie), nullptr);
    syncClientCombatMode();
    if (_screen == Screen::InGame && _globalFade.opacity() <= 0.0f) {
        updatePassiveSelection(frameTime);
        // Each member's client combat mode follows its own targets. Leaving it
        // lets the next enemy sighting count at once.
        for (int i = 0; i < _party.getSize(); ++i) {
            auto member = _party.getMember(i);
            if (!member) {
                continue;
            }
            const auto stored = _party.members()[i].lastTarget.resolve();
            std::optional<std::reference_wrapper<const Object>> storedTarget;
            if (stored) {
                storedTarget = *stored;
            }
            if (member->updateClientCombatMode(storedTarget, i == 0 && _deadTargetHold > 0.0f)) {
                _enemySightingHold = -1.0f;
            }
        }
        syncClientCombatMode();
    }
    updateCamera(dt);
    if (_module) {
        updateLeaderVideoEffect();
    }

    // A paused minigame stands still.
    if (_swoopRace.isActive() && !_paused) {
        _swoopRace.update(dt);

        // Non-blocking auto-finish: when a lifecycle race reaches the finish
        // threshold, force success and return to the origin module. Plain dev
        // races (no lifecycle) keep riding so the dev stays in control.
        if (_swoopLifecycle.active && _swoopRace.finishReached()) {
            debug(str(boost::format("swoop: auto-finish progress=%.1f finish=%.1f forcedSuccess=yes returning=%s")
                      % _swoopRace.progress()
                      % _swoopRace.finishProgress()
                      % _swoopLifecycle.originModule));
            finishSwoopLifecycle(/*success=*/true);
        }
    }

    if (_turret.isActive() && !_paused) {
        _turret.update(dt);

        // Non-blocking auto-finish: a lifecycle session ends as soon as the
        // turret reaches a win or a loss and returns to the origin module. Plain
        // dev sessions (no lifecycle) keep running so the dev stays in control.
        if (_turretLifecycle.active && _turret.finished()) {
            debug(str(boost::format("turret: auto-finish outcome=%s hp=%d/%d returning=%s")
                      % turretOutcomeName(_turret.outcome())
                      % _turret.hitPoints()
                      % _turret.maxHitPoints()
                      % _turretLifecycle.originModule));
            finishTurretLifecycle(_turret.outcome());
        }
    }

    if (_module && _module->area()) {
        _party.updateFollowPath();
    }
    updateTemporaryDeath();

    // The world keeps running behind the death sequence.
    bool updModule = !_movie && _module &&
                     (_screen == Screen::InGame || _screen == Screen::Conversation || _screen == Screen::Death);
    const auto clockSample = _services.system.clock.micros();
    const auto elapsed = _worldClockSample ? clockSample - *_worldClockSample : 0;
    _worldClockSample = clockSample;
    if (updModule && !_paused) {
        _floatingText.update(dt);
        updateHostileHilites(dt);
        _attackMashTime = std::max(0.0f, _attackMashTime - dt);
        const double elapsedSeconds =
            static_cast<double>(elapsed) * static_cast<double>(_gameSpeed * _deathTimeScale) / 1000000.0;
        // While time is stopped the world clock waits and only the excluded
        // objects' clock runs.
        if (_timeStopped) {
            advanceTimeStopClock(elapsedSeconds);
        } else {
            advanceWorldTime(elapsedSeconds);
        }
        advancePlayedTime(dt);
        // Inventory and equipped items are not necessarily area objects.
        // Refresh their readiness before scripts can query item talents.
        for (const auto &[id, object] : _objectById) {
            if (auto *item = dyn_cast<Item>(object.get())) {
                if (item->isRuntimeLive()) {
                    item->refreshSpellReadiness();
                }
            }
        }
        auto updatedModule = _module;
        auto generation = _runtimeSessionGeneration;
        updatedModule->update(dt);
        // A movie started in the update holds the rest of the world, and the
        // arrival, until it ends.
        auto current = [&]() {
            return _module == updatedModule && _runtimeSessionGeneration == generation && !_movie;
        };
        if (current()) {
            _combat.update(dt);
        }
        // Projectiles in flight stand still while time is stopped.
        if (current() && !_timeStopped) {
            _services.game.projectiles.update(dt, *this, _services);
        }
        if (current()) {
            updatedModule->dispatchDueSavedEvents();
        }
        if (current()) {
            settleFadeArrival();
        }
    }
    // Due events are delivered on every frame the module is live, also while
    // the game is paused or a menu is open over it. Only the world clock
    // waits then, so what comes due is an event posted meanwhile with no delay.
    // While the world is held, creatures new to it still run their creation
    // script and every object its actions, with the clock stopped; an event
    // an action posts is delivered in the same frame.
    const bool moduleLive = updModule || (!_movie && _module && isMenuOverModule(_screen));
    if (moduleLive && !(updModule && !_paused)) {
        auto heldModule = _module;
        auto generation = _runtimeSessionGeneration;
        auto current = [&]() {
            return _module == heldModule && _runtimeSessionGeneration == generation && !_movie;
        };
        heldModule->dispatchDueSavedEvents();
        if (holdsWorld() && current()) {
            heldModule->runObjectActions();
            if (current()) {
                heldModule->dispatchDueSavedEvents();
            }
        }
    }
    // The room view is redone on every frame the area is there to show,
    // paused, under a menu or in a conversation alike.
    if (_module && _module->area() && !_movie) {
        _module->area()->updateRoomVisibility();
    }
    // A companion brought into being outside the area, such as one the party
    // selection screen shows, runs its creation script on the next frame,
    // whether or not the world is paused.
    if (moduleLive && _module && !_movie) {
        _module->runSpawnScriptsOutsideArea();
    }

    if (_screen == Screen::SwoopRace || _screen == Screen::Turret) {
        settleFadeArrival();
    }

    if (isConversationActive()) {
        clearPlayerHostileActions();
    }
    syncClientCombatMode();
    commitTutorialWindow();
    auto gui = getScreenGUI();
    if (gui) {
        gui->update(dt);
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->update(dt);
    }
    // The credits run in real time and hold still while the game is paused.
    if (_credits) {
        if (!_paused) {
            _credits->update(frameTime);
        }
        if (_credits->isFinished()) {
            _credits.reset();
        }
    }
    updateSoloModeQuery();
    updateSceneGraph(dt);
    if (!_paused) {
        updateDrawDebug(dt);
    }
    if (_showImGui) {
        updateImGui(dt);
    }
}

void Game::render() {
    if (_movie) {
        _movie->render();
    } else {
        renderScene();
        renderGUI();
    }
}

bool Game::handleKeyDown(const input::KeyEvent &event) {
    if (event.repeat)
        return false;

    if (_screen == Screen::InGame && handleFreeLookKey(event)) {
        return true;
    }
    if (_gameOver && handleDeathSequenceKey(event)) {
        return true;
    }

    if (handleDeveloperKeyDown(event)) {
        return true;
    }

    switch (event.code) {
    case input::KeyCode::F4:
        if (_screen == Screen::InGame) {
            requestQuickSave();
            return true;
        }
        break;

    case input::KeyCode::Minus:
        if (_options.game.developer && _gameSpeed > 1.0f) {
            _gameSpeed = glm::max(1.0f, _gameSpeed - 1.0f);
            return true;
        }
        break;

    case input::KeyCode::Equals:
        if (_options.game.developer && _gameSpeed < 8.0f) {
            _gameSpeed = glm::min(8.0f, _gameSpeed + 1.0f);
            return true;
        }
        break;

    case input::KeyCode::V:
        if (_options.game.developer && _screen == Screen::InGame) {
            toggleInGameCameraType();
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

bool Game::handleDeveloperKeyDown(const input::KeyEvent &event) {
    if (!_options.game.developer || _screen != Screen::InGame) {
        return false;
    }
    if (!isDeveloperOverlayChord(event)) {
        return false;
    }

    switch (event.code) {
    case input::KeyCode::D:
        _developerOverlay.visible = !_developerOverlay.visible;
        return true;
    case input::KeyCode::T:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.triggers = true;
        } else {
            _developerOverlay.triggers = !_developerOverlay.triggers;
        }
        return true;
    case input::KeyCode::A:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
        } else {
            _developerOverlay.actorLabels = !_developerOverlay.actorLabels;
        }
        return true;
    case input::KeyCode::L:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else if (!_developerOverlay.actorLabels) {
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else {
            _developerOverlay.longActorLabels = !_developerOverlay.longActorLabels;
        }
        return true;
    case input::KeyCode::W:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.watchedValues = true;
        } else {
            _developerOverlay.watchedValues = !_developerOverlay.watchedValues;
        }
        return true;
    default:
        return false;
    }
}

bool Game::handleMouseMotion(const input::MouseMotionEvent &event) {
    _pointer->setPosition({event.x, event.y});
    return false;
}

bool Game::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    // Clicks do nothing in free-look.
    if (_freeLook && _screen == Screen::InGame) {
        return true;
    }
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _pointer->setPressed(true);
    if (_movie) {
        _movie->finish();
        return true;
    }
    return false;
}

bool Game::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _pointer->setPressed(false);
    return false;
}

Game::PreparedDestinationModule Game::prepareDestinationModule(
    const std::string &name,
    bool initialSaveRestore,
    std::shared_ptr<const resource::SaveWorkingState> workingState) {
    PreparedDestinationModule prepared;
    prepared.resources = _services.resource.director.prepareModuleLoad(
        name, std::move(workingState));
    if (!prepared.resources || !prepared.resources->structurallyValidated()) {
        throw ValidationException("Destination module was not structurally validated");
    }
    prepared.ifo = prepared.resources->moduleIfo();
    prepared.are = prepared.resources->areaAre();
    prepared.git = prepared.resources->areaGit();
    prepared.name = prepared.resources->moduleName();
    // GIT.UseTemplates is the world-representation discriminator.
    // Mod_IsSaveGame describes the surrounding IFO but cannot turn a
    // template GIT into an authoritative instance graph (or vice versa).
    const bool authoritativeSavedWorld =
        prepared.git &&
        resource::generated::parseGIT(*prepared.git).UseTemplates == 0;
    prepared.context = resolveModuleLoadContext(
        initialSaveRestore, authoritativeSavedWorld);
    validatePreparedDestination(prepared);
    return prepared;
}

void Game::validatePreparedDestination(
    const PreparedDestinationModule &prepared) const {
    if (!prepared.ifo || !prepared.are || !prepared.git) {
        throw ResourceNotFoundException(
            "Prepared destination is missing IFO/ARE/GIT state");
    }
    auto ifo = resource::generated::parseIFO(*prepared.ifo);
    if (ifo.Mod_Entry_Area.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }
    (void)resource::generated::parseARE(*prepared.are);
    (void)resource::generated::parseGIT(*prepared.git);

    // Initial disk restoration may legitimately fall back to an installed
    // module when the save has no archived current-module graph (#325). Only a
    // non-template GIT owns the authoritative ModuleGraph namespace;
    // template-local numbers are not saved identities.
    if (!restoresSavedWorld(prepared.context)) {
        return;
    }

    const auto identityContext = SerializedIdentityContext::moduleGraph(
        prepared.resources->moduleName());
    std::map<uint32_t, std::string> claims;
    auto validateClaims = [&](const resource::Gff &record,
                              SerializedGraphRoot root,
                              const std::string &prefix) {
        for (auto claim : collectSerializedObjectIdClaims(
                 record, identityContext, root)) {
            if (claim.id == kSavedRuntimeModuleObjectId ||
                claim.id == std::numeric_limits<uint32_t>::max()) {
                throw ValidationException(
                    "Invalid authoritative saved ObjectId " +
                    std::to_string(claim.id) + " at " + prefix + claim.path);
            }
            auto [found, inserted] = claims.emplace(
                claim.id, prefix + claim.path);
            if (!inserted) {
                throw ValidationException(
                    "Duplicate authoritative saved ObjectId " +
                    std::to_string(claim.id) + " at " + found->second +
                    " and " + prefix + claim.path);
            }
        }
    };
    validateClaims(
        *prepared.ifo, SerializedGraphRoot::ModuleIfo, "module/");
    validateClaims(
        *prepared.git, SerializedGraphRoot::AreaGit, "area/");

    uint32_t nextObjectId = kFirstRuntimeObjectId;
    if (prepared.ifo->readDword(nextObjectId, "Mod_NextObjId0") &&
        nextObjectId != 0 && nextObjectId < kFirstRuntimeObjectId) {
        throw ValidationException("Invalid Mod_NextObjId0");
    }
    uint64_t nextEffectId = 0;
    if (prepared.ifo->readDword64(nextEffectId, "Mod_Effect_NxtId") &&
        (nextEffectId == kUnassignedEffectId ||
         nextEffectId == std::numeric_limits<EffectId>::max())) {
        throw ValidationException("Invalid Mod_Effect_NxtId");
    }
}

bool Game::loadModule(
    const std::string &name,
    std::string entry,
    bool initialSaveRestore) {
    info("Preparing module '" + name + "'");
    PreparedDestinationModule prepared;
    try {
        prepared = prepareDestinationModule(
            name,
            initialSaveRestore,
            _services.resource.director.committedSaveWorkingState());
    } catch (const std::exception &e) {
        error("Failed preparing module '" + name + "': " + e.what());
        return false;
    }
    return loadPreparedModule(
        std::move(prepared),
        std::move(entry),
        initialSaveRestore,
        /*resourcesCommitted=*/false);
}

// Before anything is stored, leaving the module clears the actions of the
// creature the player controls and of every roster creature and puppet in the
// world, each that can be commanded; what an uncommandable creature was doing
// goes with it.
void Game::clearDepartingActions() {
    const size_t npcCount = isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount;
    for (size_t npc = 0; npc < npcCount; ++npc) {
        if (auto creature = _party.rosterCreature({RosterKind::Npc, static_cast<int>(npc)})) {
            creature->clearAllActions(true);
        }
    }
    if (isTSL()) {
        for (size_t puppet = 0; puppet < Party::kMaxPuppetCount; ++puppet) {
            if (auto creature = _party.rosterCreature({RosterKind::Puppet, static_cast<int>(puppet)})) {
                creature->clearAllActions(true);
            }
        }
    }
    if (auto player = _party.player()) {
        player->clearAllActions(true);
    }
}

// Resident party creatures cross the transition as the same objects, but
// their live action and timer objects still belong to the outgoing Area.
// Capture the save-facing forms of what is left in their queues now and
// reconstruct them only after the destination is authoritative.
std::vector<Game::PartyTransitionState> Game::captureResidentPartyActions() {
    std::vector<PartyTransitionState> states;
    const auto &residentCreatures = _module->area()->getObjectsByType(ObjectType::Creature);
    std::set<const Creature *> captured;
    for (const auto &object : _party.runtimeObjects()) {
        auto creature = std::dynamic_pointer_cast<Creature>(object);
        if (!creature || !captured.insert(creature.get()).second ||
            std::find(residentCreatures.begin(), residentCreatures.end(), creature) == residentCreatures.end()) {
            continue;
        }
        PartyTransitionState state;
        state.creature = creature;
        state.actions.actions = creature->saveActionSnapshot();
        for (auto &action : state.actions.actions) {
            // Bind references while their source registry is authoritative.
            // Preserve the exact runtime incarnation rather than
            // reinterpreting its numeric ID.
            state.actionReferencesBound.push_back(action.bindObjectReferences(*this));
        }
        // Pending delayed commands are events of the outgoing module. They
        // stay in its queue and are stored with it.
        states.push_back(std::move(state));
    }
    return states;
}

// Destination structure and the source snapshot are both viable. Technical
// teardown belongs on the commit side of that boundary: a rejected destination
// or failed snapshot must not abort an otherwise valid gameplay session.
void Game::endSourceSession(const std::string &destination) {
    abortPazaak();
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
        _cameraType = _savedCameraType;
    }
    if (_turret.isActive()) {
        _turret.stop();
        _cameraType = _savedCameraType;
    }
    if (_swoopLifecycle.active) {
        _swoopLifecycle = MinigameLifecycle();
    }
    if (_turretLifecycle.active) {
        _turretLifecycle = MinigameLifecycle();
    }
    if (_pendingTurret.active && !boost::iequals(_pendingTurret.targetModule, destination)) {
        debug(str(boost::format("turret: scheduled session for '%s' dropped, loading '%s' instead") %
                  _pendingTurret.targetModule % destination));
        _pendingTurret = PendingTurretRequest();
    }
    if (_screen == Screen::Conversation && _conversation) {
        _conversation->cleanupForModuleTransition();
    }

    if (_module) {
        // The source snapshot is already frozen. Module retirement now owns
        // the full Area-departure boundary itself.
        retireActiveModuleRuntime();
    }

    // Do not carry a displayed or pending batch, indicator, or GUI controls
    // across module teardown. OnLoad events start a new batch for the
    // destination module.
    _floatingText.reset();
    _statusSummary.reset();
    if (_hud) {
        _hud->resetStatusSummaryPresentation();
    }
}

// The module itself needs a transient runtime identity even though it is not
// serialized. Keep that allocation clear of identities explicitly owned by the
// saved IFO graph (notably the Area).
void Game::publishDestinationModule(const std::string &name, const resource::Gff &ifo, bool restoringSavedWorld) {
    if (restoringSavedWorld) {
        reserveSavedObjectIds(ifo, SerializedIdentityContext::moduleGraph(name), SerializedGraphRoot::ModuleIfo);
    }
    std::shared_ptr<Module> destinationModule;
    std::vector<std::shared_ptr<Object>> noObsolete;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            destinationModule = restoringSavedWorld ? newSavedModule() : newModule();
            if (restoringSavedWorld) {
                registerSavedModuleReferenceTarget(destinationModule, SerializedIdentityContext::moduleGraph(name));
            }
        },
        [&]() noexcept {
            _module = std::move(destinationModule);
        });
}

// Publish the exact runtime incarnations already bound in their source graph.
// Do not look up their serialized numbers again in the destination.
void Game::restorePartyTransitionState(std::vector<PartyTransitionState> &states) {
    for (auto &state : states) {
        auto creature = state.creature.resolve();
        if (!creature) {
            continue;
        }
        creature->_savedEffects.clear();
        creature->_savedActionQueue = std::move(state.actions);
        creature->_savedActionReferencesBound = std::move(state.actionReferencesBound);
        creature->_savedRuntimeParsed = true;
        creature->_savedRuntimePublished = false;
        creature->_actions.clear();
        creature->_publishedSavedActions.clear();
        creature->refillForceShieldPools();
        // A member arrives with its hand items unlit and unheld.
        creature->resetHandPower();
    }
}

// The TSL globals that count a human, beast or droid confusion power as at work.
static constexpr std::array<const char *, 3> kConfusionActiveGlobals {"000_Human_Conf_Active", "000_Beast_Conf_Active", "000_Droid_Conf_Active"};

bool Game::loadPreparedModule(
    PreparedDestinationModule prepared,
    std::string entry,
    bool initialSaveRestore,
    bool resourcesCommitted,
    std::shared_ptr<const resource::SaveWorkingState> sourceWorkingState) {
    const std::string name = prepared.name;
    info("Loading module '" + name + "'");
    _transitionInProgress = true;
    struct TransitionGuard {
        bool &value;
        ~TransitionGuard() { value = false; }
    } transitionGuard {_transitionInProgress};
    // Equipment read during the load waits for its place in it; none of it
    // outlives the load.
    struct EquippedOnLoadGuard {
        std::vector<EquippedOnLoad> &kept;
        ~EquippedOnLoadGuard() { kept.clear(); }
    } equippedOnLoadGuard {_equippedOnLoad};

    // Restoring a save is the only load an authored script may see as such,
    // and only while it runs: the flag falls away on completion, on failure
    // and on the way out of any exception.
    _loadingFromSaveGame = initialSaveRestore;
    struct LoadFromSaveGuard {
        bool &value;
        ~LoadFromSaveGuard() { value = false; }
    } loadFromSaveGuard {_loadingFromSaveGame};

    std::vector<PartyTransitionState> partyTransitionState;

    // Exit scripts are part of the source module's last observable state. The
    // resulting working state remains a candidate until the resource/runtime
    // commit below; a capture failure leaves the source graph authoritative.
    if (_module) {
        // In TSL leaving a module counts no confusion as still at work, so
        // each confusion power is offered again.
        if (isTSL()) {
            for (const auto &global : kConfusionActiveGlobals) {
                setGlobalNumber(global, 0);
            }
        }
        try {
            _module->area()->runOnExitScript();
        } catch (const std::exception &e) {
            error("Source module exit script failed: " + std::string(e.what()));
            return false;
        }
        clearDepartingActions();
        sourceWorkingState = prepareCurrentModuleWorkingState();
        if (!sourceWorkingState) {
            return false;
        }

        try {
            partyTransitionState = captureResidentPartyActions();
        } catch (const std::exception &e) {
            error(
                "Unable to snapshot Party transition state: " +
                std::string(e.what()));
            return false;
        }

        // Re-entering the same module must inspect the snapshot just captured,
        // not the previously committed visit. Other destinations are
        // unaffected because the candidate changed only the source archive.
        if (boost::iequals(_module->name(), name)) {
            try {
                prepared = prepareDestinationModule(
                    name, initialSaveRestore, sourceWorkingState);
            } catch (const std::exception &e) {
                error("Failed preparing snapshotted module '" + name +
                      "': " + e.what());
                return false;
            }
        }
    }

    bool loaded = false;

    const auto sourceScreen = _screen;
    bool commitStarted = false;
    try {
        withLoadingScreen(name, [this,
                                 &prepared,
                                 &name,
                                 &entry,
                                 initialSaveRestore,
                                 resourcesCommitted,
                                 &sourceWorkingState,
                                 &partyTransitionState,
                                 &commitStarted,
                                 &loaded]() {
            try {
                commitStarted = true;
                _globalFade.request(GlobalFade::Direction::Out);
                endSourceSession(name);

                if (!resourcesCommitted) {
                    _services.resource.director.commitModuleLoad(
                        std::move(prepared.resources));
                }

                // Resource publication is the only fallible destination commit.
                // Adopt the frozen source snapshot only after it succeeds, so a
                // destination that never publishes does not alter working state.
                if (sourceWorkingState) {
                    _services.resource.director.adoptSaveWorkingState(
                        std::move(sourceWorkingState));
                }

                loadInGameMenus();

                if (_loadScreen) {
                    _loadScreen->setProgress(50);
                }
                render();

                _services.scene.graphs.get(kSceneMain).clear();
                // A module starts with no conversation under way.
                _dialogUnderWay = false;

                const auto &ifo = prepared.ifo;
                ModuleLoadContext context = prepared.context;
                bool restoringSavedWorld = restoresSavedWorld(context);

                // Every module start takes its hour length from the module. A
                // restored save has already set the calendar day and time of day
                // from its own module record; any other start (a new game or a
                // transition) keeps the day and time of day the world had and
                // counts them in the destination's hours.
                if (!initialSaveRestore) {
                    restoreWorldTime(*ifo, worldTimeDay(), worldTimeOfDay());
                }

                publishDestinationModule(name, *ifo, restoringSavedWorld);
                _fadeArrival = _globalFade.beginArrival();
                _fadeArrivalModule = _module;
                auto destination = _module;
                auto arrival = _fadeArrival;
                auto stillCurrent = [&]() {
                    return destination == _module && arrival == _fadeArrival;
                };
                destination->load(
                    name,
                    *ifo,
                    *prepared.are,
                    *prepared.git,
                    restoringSavedWorld);
                if (!stillCurrent()) {
                    return;
                }
                _loadedModules.insert(std::make_pair(name, destination));

                // Structural construction is complete and the destination module
                // is now the authoritative script caller. Authored gameplay begins
                // here; failures from this point are terminal rather than rolled
                // back as if arbitrary NWScript mutation were transactional. The
                // creatures run their creation scripts on the area's first update,
                // after the events the load has queued.

                // Only a session without a player character gets the default
                // party; one whose controlled companion was taken out keeps its
                // parked player character.
                if (_party.isEmpty() && !_party.actualPlayer()) {
                    loadDefaultParty();
                }
                // A follower whose creature was destroyed is still listed with
                // the party, and comes back from its roster record.
                _party.respawnVacantFollowers();

                // The game makes restored effects, actions and events visible before
                // authored OnLoad/OnEnter scripts inspect or mutate them. Binding
                // remains explicit and graph-local; only publication moves ahead
                // of the entry hooks.
                bindSavedRuntimeState();

                restorePartyTransitionState(partyTransitionState);
                publishSavedRuntimeState();

                // Whether the world came from persisted state decides what gets
                // restored, not whether the module's authored entry hook runs.
                // Content relies on that hook every time it is entered, including
                // when revisiting a module whose world state is restored. It runs
                // as an event after the area's creature entries.
                destination->signalLoaded();

                // The party is formed anew in the destination: a place that names
                // no one does not come along, and the first member there leads.
                _party.removeVacantMembers();
                signalPartyEquipment();
                destination->loadParty(entry, preservesSavedPlacement(context));
                if (!stillCurrent()) {
                    return;
                }
                signalKeptEquippedOnLoad();

                info("Module '" + name + "' loaded successfully");

                if (_loadScreen) {
                    _loadScreen->setProgress(100);
                }
                render();

                // The area's music replaces any menu music. It starts on arrival.
                playMusic("");
                _areaMusic.load(_module->area());

                _globalFade.finishLoading(arrival);
                if (!isConversationActive()) {
                    openInGame();
                }
                loaded = true;
            } catch (const std::exception &e) {
                error("Failed loading module '" + name + "': " + std::string(e.what()));
                // Source retirement is the commit boundary for an ordinary
                // transition, just as resetGame is for a disk load. Authored or
                // otherwise post-commit failure cannot resurrect the old world;
                // retire the partial destination and land somewhere deliberate.
                retireToMainMenu();
            }
        });
    } catch (const std::exception &e) {
        error("Module load escaped its failure boundary for '" + name +
              "': " + e.what());
        if (!commitStarted) {
            // Loading-screen preparation precedes runtime/resource commit. Its
            // presentation work is reversible without a parallel scene: the
            // authoritative source world remains the current session.
            _screen = sourceScreen;
        } else {
            // A terminal-path dependency failed while retiring the published
            // destination. Do not expose a blank or resurrected old world.
            retireToMainMenu();
        }
        return false;
    }

    // However long that took, none of it was time the game world lived
    // through. Whoever drives the frame clock has to start a new epoch before
    // the next update, or the whole load lands on the world as one enormous
    // step. The world clock does not run during a load either.
    _timingDiscontinuity = true;
    _worldClockSample.reset();
    return loaded;
}

void Game::retireActiveModuleRuntime() {
    _globalFade.invalidateModule();
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
    _lastTarget.reset();
    _nearestObjects.clear();
    // Leaving a module forgets both sightings and both sighting holds, and the
    // autopause delay window and any deferred autopause.
    _autoPauseDelayWindow = 0.0f;
    _deferredAutoPauseDelay = 0.0f;
    _enemySighted = false;
    _mineSighted = false;
    _enemySightingHold = 0.0f;
    _mineSightingHold = 0.0f;
    _lastRenderedSceneOutput = nullptr;
    _runtimeSessionPlayable = false;

    // This is also the failed-destination cleanup boundary. If construction
    // attached only part of the retained party, Area retirement inspects
    // actual residency and safely ignores every not-yet-attached object.
    retireActiveAreaRuntime();

    std::set<uint32_t> sessionObjectIds;
    for (const auto &object : _party.runtimeObjects()) {
        if (object) sessionObjectIds.insert(object->id());
    }

    _combat.reset();
    // Leaving a module ends the player's pause and a time stop, and the stop's
    // exclusions go with it. A Time Stop effect applied again afterwards, a
    // restored one included, stops time anew.
    setPaused(false);
    _timeStopped = false;
    _timeStopExclusions.clear();
    _module.reset();
    _loadedModules.clear();
    std::vector<std::shared_ptr<Object>> retiredObjects;
    for (const auto &[id, object] : _objectById) {
        if (sessionObjectIds.count(id) == 0) {
            retiredObjects.push_back(object);
        }
    }
    for (const auto &object : retiredObjects) {
        destroyRuntimeObjectGraph(object);
    }
    retireSavedObjectGraph();
}

void Game::retireActiveAreaRuntime() {
    _services.game.projectiles.retireAreaRuntime();
    _areaMusic.unload();
    auto module = _module;
    auto area = module ? module->area() : nullptr;
    if (area) {
        area->retirePartyAreaRuntime();
    }
}

void Game::retireSavedObjectGraph() {
    ++_savedGraphGeneration;
    _reservedSavedObjectIds.clear();
    _reservedSavedIdentityNamespace.reset();
    _reservedSavedObjectIdClaims.clear();
    _objectBySavedId.clear();
    _savedIdByObject.clear();
}

void Game::retireRuntimeSession() {
    // Stable-frame save execution is synchronous and cannot ordinarily overlap
    // retirement. A re-entrant retirement from an injected/service callback is
    // an invariant violation; the local request owner in processPendingSave()
    // still remains responsible for terminalization.
    if (_saveInProgress) {
        error("Runtime session retirement re-entered synchronous save execution");
    }
    if (_pendingSave) {
        auto request = std::move(*_pendingSave);
        _pendingSave.reset();
        SaveResult cancelled;
        cancelled.status = SaveStatus::Cancelled;
        cancelled.message = "Runtime session retired before save execution";
        finalizeSaveRequest(request, std::move(cancelled));
    }
    ++_runtimeSessionGeneration;
    _globalFade.resetSession();
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
    _runtimeSessionPlayable = false;
    _screen = Screen::None;
    _lastRenderedSceneOutput = nullptr;

    abortPazaak();
    _lastPazaakResult.reset();
    _pazaakDevelopmentSelectedObjectOverride.reset();
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
    }
    if (_turret.isActive()) {
        _turret.stop();
    }
    _pendingTurret = PendingTurretRequest();
    _swoopLifecycle = MinigameLifecycle();
    _turretLifecycle = MinigameLifecycle();

    if (_conversation) {
        _conversation->cleanupForModuleTransition();
        _conversation = nullptr;
    }

    _services.audio.mixer.stopAll();
    _music.reset();
    _loadMusic.reset();
    _loadComesWithMovies = false;
    _movie.reset();
    _musicResRef.clear();
    while (!_moduleTransitionMovies.empty()) {
        _moduleTransitionMovies.pop();
    }
    setCursorType(CursorType::Default);
    _cameraType = CameraType::ThirdPerson;
    _savedCameraType = CameraType::ThirdPerson;
    _freeLook = false;
    disableVideoEffect();
    _scriptVideoEffectHeld = false;
    _playerInputBlocked = false;
    _postDialogCharacterSwitch.reset();
    _paused = false;
    _timeStopped = false;
    _timeStopExclusions.clear();
    _timeStopMilliseconds = 0;
    _timeStopFraction = 0.0;
    _clientCombatMode = false;
    _clientCombatLeader.reset();
    _clientCombatArea.reset();
    _keepStealthInDialog = false;
    _relativeMouseMode = false;

    _statusSummary.reset();
    if (_hud) {
        _hud->resetStatusSummaryPresentation();
    }

    // Drop GUI-owned object selections, conversation participants and
    // container/party bindings before releasing the runtime graph.
    _hud.reset();
    _inGame.reset();
    _dialog.reset();
    _computer.reset();
    _container.reset();
    _partySelect.reset();

    if (_map) {
        _map->retireRuntimeSession();
    }

    // Full-session teardown can follow a partially failed destination load.
    // Retire retained Area residency before Party bindings or the owning
    // Module/Area/Pathfinder disappear. A prior failure cleanup is harmless.
    retireActiveAreaRuntime();

    _combat.reset();
    _party.retireRuntimeSession();

    _services.scene.graphs.get(kSceneMain).clear();
    _module.reset();
    _loadedModules.clear();

    // Storage retained by actions, effects or presentation holders remains
    // allocated, but every exact runtime incarnation becomes semantically dead
    // before the registry is emptied and numeric IDs may restart.
    while (!_objectById.empty()) {
        unregisterRuntimeObject(_objectById.begin()->second);
    }
    retireSavedObjectGraph();
    _publishedRuntimeObjectIds.clear();
    _nextObjectId = kFirstRuntimeObjectId;
    _effectIds.reset();
    _worldTimeMilliseconds = 0;
    _minutesPerHour = 5;
    _worldTimeFraction = 0.0;
    _worldClockSample.reset();

    _nextModule.clear();
    _nextEntry.clear();
    _atStableSavePoint = false;
}

void Game::resetGame() {
    _temporaryDeathRecovery.reset(static_cast<std::uint32_t>(_services.system.clock.millis()));
    _gameOver = false;
    _endGamePending = false;
    _endGameDelay = 0.0f;
    _showEndGameGui = true;
    _lastPartyMemberTempKilled = script::kObjectInvalid;
    _deathTimeScale = 1.0f;
    // A load started from the death panel's own button comes through here, so
    // the death GUIs are taken down rather than destroyed.
    if (_deathMessage) {
        _deathMessage->hide();
    }
    if (_deathDisplay) {
        _deathDisplay->dismiss();
    }
    _lastTarget.reset();
    _lastTargetLook = false;
    _hostileHilites.clear();
    _nearestObjects.clear();
    _tutorialPending.reset();
    if (_tutorial) {
        if (_confirmPopup) {
            _confirmPopup->hide();
        }
        finishTutorialWindow(false);
    }
    retireRuntimeSession();

    _quitRequested = false;
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();
    _customTokens.clear();
    _saveResourceShadows.clear();

    _party.reset();
    _journal.reset();
    _floatingText.reset();
    _cheatUsed = false;
    _playedTimeFraction = 0.0;
    _services.resource.director.onNewGame();
}

/**
 * Resolve and validate a save without disturbing the running game.
 *
 * Every read goes through the unpublished candidate rather than the director,
 * because the director still answers for the committed session: consulting it
 * here would validate the save that is already loaded. Records that the loader
 * treats as mandatory are proven now, so the failures that used to strand a
 * half-torn-down session are raised while the old one is still authoritative.
 */
Game::PreparedSaveLoad Game::prepareSaveLoad(const resource::SaveSlotDescriptor &slot) {
    PreparedSaveLoad prepared;
    prepared.session = _services.resource.director.prepareGameLoad(slot);

    prepared.saveInfo = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("savenfo", ResType::Res)));
    if (!prepared.saveInfo) {
        throw ResourceNotFoundException("saveinfo.res not found");
    }
    prepared.nfo = resource::parseNFO(*prepared.saveInfo);

    prepared.globalVars = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("globalvars", ResType::Res)));
    if (!prepared.globalVars) {
        throw ResourceNotFoundException("globalvars.res not found");
    }
    (void)resource::parseGVT(*prepared.globalVars);

    // Optional save-wide records are decoded while the candidate is still private.
    prepared.partyTable = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("partytable", ResType::Res)));
    prepared.inventory = decodeSaveGff(
        prepared.session->findWorking(ResourceId("inventory", ResType::Res)));
    prepared.destination = prepareDestinationModule(
        prepared.nfo.lastModule,
        /*initialSaveRestore=*/true,
        prepared.session->workingState());
    if (prepared.destination.context ==
        ModuleLoadContext::InitialTemplateRestore) {
        prepared.playerInfo = decodeSaveGff(
            prepared.session->findMetadata(ResourceId("pifo", ResType::Ifo)));
        if (!prepared.playerInfo ||
            prepared.playerInfo->getList("Mod_PlayerList").empty()) {
            throw ValidationException(
                "Template-world save restore requires pifo.ifo player state");
        }
        auto params = prepared.saveInfo->findStruct("AUTOSAVEPARAMS");
        if (!params) {
            throw ValidationException(
                "Template-world save restore requires AUTOSAVEPARAMS");
        }
        PreparedSaveLoad::AutosaveRestoreState autosave;
        autosave.startWaypoint = params->getString("STARTWAYPOINT");
        if (autosave.startWaypoint == "*") {
            autosave.startWaypoint.clear();
        }
        autosave.pauseDay = params->getUint("TIME_PAUSEDAY");
        autosave.pauseTime = params->getUint("TIME_PAUSETIME");
        autosave.loadMusic = params->getString("LOADMUSIC");
        prepared.autosave = std::move(autosave);
    }
    validatePartyLoad(prepared.partyTable.get());

    return prepared;
}

bool Game::loadGame(const resource::SaveSlotDescriptor &slot) {
    info(str(boost::format("Loading savegame '%s'") % slot.directory.filename().string()));

    // Resolve and validate the replacement before anything is given up. A
    // throw here leaves the current session and its mounts untouched, so the
    // player keeps playing instead of being left with nothing to render.
    auto prepared = prepareSaveLoad(slot);

    try {
        // Commit. The old runtime and the old mounts retire together, and only
        // then does the candidate become authoritative: no runtime ever
        // observes the other session's resources.
        // A save loads with no loading-screen music, except an autosave of the
        // world's template, which plays the one it recorded.
        _loadMusicResRef.clear();
        if (prepared.autosave) {
            _loadMusicResRef = prepared.autosave->loadMusic;
        }
        resetGame();
        _services.resource.director.commitGameLoad(std::move(prepared.session));
        return restoreSaveLoad(std::move(prepared));
    } catch (const std::exception &e) {
        // Past the commit boundary the previous session no longer exists and
        // cannot be restored. Retire whatever was half-built and land on a
        // deliberate screen rather than the blank one an abandoned session
        // leaves behind.
        error("Failed restoring savegame '" +
              slot.directory.filename().string() + "': " + std::string(e.what()));
        retireToMainMenu();
        return false;
    }
}

bool Game::restoreSaveLoad(PreparedSaveLoad prepared) {
    const NFO &nfo = prepared.nfo;
    _cheatUsed = nfo.cheatUsed;
    captureSaveResourceShadow({SaveResourceKind::Nfo, {}}, *prepared.saveInfo);

    // The exact plan inspected before reset now replaces the active module
    // owners. A file changing between preparation and this point is a
    // post-commit failure and follows #325's deliberate terminal policy.
    _services.resource.director.commitModuleLoad(
        std::move(prepared.destination.resources));

    // Restore the save-wide faction table before any module objects can query
    // disposition. A missing or malformed optional FAC starts from fresh base
    // data; it must never preserve relationships from the previous save.
    std::optional<IReputes::State> reputesState;
    try {
        if (auto reputesGff = decodeSaveGff(
                _services.resource.director.findSaveWorking(
                    ResourceId("repute", ResType::Fac)))) {
            captureSaveResourceShadow(
                {SaveResourceKind::FactionTable, {}}, *reputesGff);
            reputesState = _services.game.reputes.parse(*reputesGff);
        }
    } catch (const std::exception &e) {
        warn("Game: invalid repute.fac: " + std::string(e.what()));
    }
    if (!reputesState) {
        reputesState = _services.game.reputes.baseState();
    }
    _services.game.reputes.replace(std::move(*reputesState));

    // Deserialize global variables, proven present while the candidate was
    // still unpublished.
    deserializeGlobalVariables(*prepared.globalVars);

    // Deserialize the party from records read before retiring the previous session.
    // A save may contain save-wide state without an instantiated module graph;
    // transition autosaves build the module from templates and the player from pifo.ifo.
    const auto &ifo = prepared.destination.ifo;
    replaceCustomTokens(parseCustomTokens(*ifo));
    std::string entry;
    if (restoresSavedWorld(prepared.destination.context)) {
        captureSaveResourceShadow(
            {SaveResourceKind::ModuleIfo, prepared.destination.name}, *ifo);
        const auto moduleIdentityContext =
            SerializedIdentityContext::moduleGraph(prepared.destination.name);
        prepareSavedRuntimeNamespace(*ifo, moduleIdentityContext);

        // Detached save-wide records are deliberately not traversed here:
        // their ObjectId fields do not claim identities in the module graph.
        reserveSavedObjectIds(
            *prepared.destination.git,
            moduleIdentityContext,
            SerializedGraphRoot::AreaGit);
        deserializeParty(*ifo, prepared.partyTable, moduleIdentityContext);
    } else {
        if (!prepared.autosave || !prepared.playerInfo) {
            throw ValidationException(
                "Template-world save restore state was not prepared");
        }
        restoreWorldTime(
            *ifo, prepared.autosave->pauseDay, prepared.autosave->pauseTime);
        deserializeParty(
            *prepared.playerInfo,
            prepared.partyTable,
            SerializedIdentityContext::detachedRecord("pifo.ifo"));
        entry = prepared.autosave->startWaypoint;
    }

    // Party creation clears the detached companion ActionList before binding
    // its representation into the active area.
    auto discardDetachedRosterActions = [](const std::shared_ptr<Creature> &creature) {
        if (!creature) return;
        creature->_savedActionQueue = SavedActionQueue {};
        creature->_savedActionReferencesBound.clear();
        creature->_actions.clear();
        creature->_publishedSavedActions.clear();
    };
    for (const auto &member : _party.members()) {
        if (member.creature && member.creature != _party.player() &&
            member.creature != _party.actualPlayer()) {
            discardDetachedRosterActions(member.creature);
        }
    }
    if (isTSL()) {
        for (int puppet : _party.persistedState().puppetIds) {
            discardDetachedRosterActions(
                _party.getAvailablePuppet(puppet, true));
        }
    }

    // Once the player is loaded, deserialize player's inventory.
    if (prepared.inventory) {
        captureSaveResourceShadow(
            {SaveResourceKind::Inventory, {}}, *prepared.inventory);
        deserializeInventory(*prepared.inventory);
    }

    return loadPreparedModule(
        std::move(prepared.destination),
        std::move(entry),
        /*initialSaveRestore=*/true,
        /*resourcesCommitted=*/true);
}

void Game::validatePartyLoad(const resource::Gff *partyTable) const {
    if (!partyTable) {
        return;
    }

    const auto state = parsePartyTable(*partyTable).party;
    const auto validNpc = [this](int npc) {
        const int count = isTSL()
                              ? static_cast<int>(Party::kK2NpcCount)
                              : static_cast<int>(Party::kK1NpcCount);
        return npc >= 0 && npc < count;
    };
    if (state.controlledNpc != -1 && !validNpc(state.controlledNpc)) {
        throw ValidationException("PartyTable controlled NPC is out of range");
    }

    std::set<int> members;
    for (int npc : state.memberIds) {
        if (npc != kNpcPlayer && !validNpc(npc)) {
            throw ValidationException("PartyTable member is out of range");
        }
        if (!members.insert(npc).second) {
            throw ValidationException("PartyTable contains a duplicate member");
        }
    }
    if (state.leader != -1 && state.leader != kNpcPlayer &&
        members.count(state.leader) == 0) {
        throw ValidationException("PartyTable leader is not an active member");
    }

    std::set<int> puppets;
    for (int puppet : state.puppetIds) {
        if (!isTSL() || puppet < 0 ||
            puppet >= static_cast<int>(Party::kMaxPuppetCount)) {
            throw ValidationException("PartyTable puppet is out of range");
        }
        if (!puppets.insert(puppet).second) {
            throw ValidationException("PartyTable contains a duplicate puppet");
        }
    }
}

std::map<int, std::string> Game::parseCustomTokens(
    const resource::Gff &ifoGff) const {

    std::map<int, std::string> result;
    for (const auto &entry : ifoGff.getList("Mod_Tokens")) {
        uint32_t token = 0;
        if (!entry->readDword(token, "Mod_TokensNumber") ||
            token > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        result[static_cast<int>(token)] = entry->getString("Mod_TokensValue");
    }
    return result;
}

void Game::replaceCustomTokens(std::map<int, std::string> tokens) {
    _customTokens = std::move(tokens);
}

void Game::deserializeGlobalVariables(resource::Gff &gvtGff) {
    captureSaveResourceShadow({SaveResourceKind::GlobalVars, {}}, gvtGff);
    GVT gvt = resource::parseGVT(gvtGff);
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();

    for (auto &[name, value] : gvt.strings) {
        setGlobalString(name, value);
    }

    for (auto &[name, value] : gvt.booleans) {
        setGlobalBoolean(name, value);
    }

    for (auto &[name, value] : gvt.numbers) {
        setGlobalNumber(name, value);
    }

    for (auto &[name, value] : gvt.locations) {
        auto &[pos, rot] = value;
        setGlobalLocation(name, std::make_shared<Location>(pos, rot));
    }
}

void Game::deserializeParty(
    resource::Gff &ifoGff,
    const std::shared_ptr<Gff> &ptGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    resetGalaxyMap();

    std::shared_ptr<Gff> pcGff;
    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (!players.empty()) {
        const int controlledNpc =
            ptGff ? parsePartyTable(*ptGff).party.controlledNpc : -1;
        // K2 may mark the controlled module creature primary even while
        // pc.utc holds a distinct canonical player. PARTYTABLE is authoritative.
        if (controlledNpc != -1) {
            try {
                pcGff = decodeSaveGff(
                    _services.resource.director.findSaveWorking(ResourceId("pc", ResType::Utc)));
            } catch (const std::exception &e) {
                warn("Game: invalid pc.utc: " + std::string(e.what()));
            }
        }
    }

    publishPartyRuntimeState(ifoGff, ptGff, pcGff, moduleIdentityContext);
}

void Game::publishPartyRuntimeState(
    resource::Gff &ifoGff,
    const std::shared_ptr<resource::Gff> &ptGff,
    const std::shared_ptr<resource::Gff> &pcGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    if (ptGff) {
        captureSaveResourceShadow({SaveResourceKind::PartyTable, {}}, *ptGff);
        deserializeGalaxyMap(*ptGff);
        uint32_t gold = 0;
        if (ptGff->readDword(gold, "PT_GOLD")) {
            _party.takeGold(_party.gold());
            _party.giveGold(gold);
        }

        uint32_t xp = 0;
        if (ptGff->readDword(xp, "PT_XP_POOL")) {
            // Restore the party pool before any member is added; members keep
            // their own saved experience and companions catch up with the pool.
            _party.setXP(xp);
        }

        replacePartyTable(parsePartyTable(*ptGff));
        deserializePazaakPartyTable(*ptGff);
    }

    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (players.empty()) {
        return;
    }

    auto modulePlayer = newCreature(*players.front(), moduleIdentityContext);
    modulePlayer->captureSaveRecord(
        *players.front(), moduleIdentityContext, {SaveRecordOriginKind::ModulePlayer, {}});
    if (modulePlayer->tag().empty()) {
        modulePlayer->setTag(kObjectTagPlayer);
    }

    auto actualPlayer = modulePlayer;
    const auto &partyState = _party.persistedState();
    // PT_CONTROLLED_NP, not Mod_IsPrimaryPlr, defines whether pc.utc is the
    // canonical player distinct from the currently controlled module creature.
    if (partyState.controlledNpc != -1 && pcGff) {
        const auto pcIdentityContext =
            SerializedIdentityContext::detachedRecord("pc.utc");
        actualPlayer = newCreature(*pcGff, pcIdentityContext);
        actualPlayer->captureSaveRecord(
            *pcGff, pcIdentityContext, {SaveRecordOriginKind::PrimaryPlayerUtc, {}});
    }

    _party.setPlayer(modulePlayer);
    _party.setActualPlayer(actualPlayer);
    if (partyState.controlledNpc != -1 &&
        !_party.bindRosterCreature(
            {RosterKind::Npc, partyState.controlledNpc}, modulePlayer)) {
        throw ValidationException(
            "Controlled NPC could not bind its logical roster slot");
    }

    if (ptGff) {
        deserializePartyMembers(*ptGff);
        deserializeJournal(*ptGff);
    } else {
        _party.addMember(kNpcPlayer, actualPlayer);
    }
}

Game::PartyTable Game::parsePartyTable(const resource::Gff &ptGff) const {
    PartyTable table;
    Party::PersistedState &state = table.party;
    state.pcName = ptGff.getString("PT_PCNAME");
    // GFF labels are capped at sixteen bytes. KotOR II stores the
    // component count under this exact truncated label.
    state.itemComponent = ptGff.getUint("PT_ITEM_COMPONEN");
    state.itemChemical = ptGff.getUint("PT_ITEM_CHEMICAL");
    state.swoopUpgrades[0] = ptGff.getUint("PT_SWOOP1");
    state.swoopUpgrades[1] = ptGff.getUint("PT_SWOOP2");
    state.swoopUpgrades[2] = ptGff.getUint("PT_SWOOP3");
    state.playedSeconds = ptGff.getUint("PT_PLAYEDSECONDS");
    uint32_t playedMinutes = ptGff.getUint("PT_PLAYEDMINUTES");
    if (playedMinutes != 0) {
        state.playedSeconds = playedMinutes * 60;
    }
    state.controlledNpc = ptGff.getInt("PT_CONTROLLED_NP", -1);
    state.soloMode = ptGff.getBool("PT_SOLOMODE");

    const auto memberList = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), memberList.size()), 2);
    for (size_t index = 0; index < memberCount; ++index) {
        int npc = memberList[index]->getInt("PT_MEMBER_ID", -1);
        state.memberIds.push_back(npc);
        if (memberList[index]->getBool("PT_IS_LEADER")) {
            state.leader = npc;
        }
    }

    const auto puppetList = ptGff.getList("PT_PUPPETS");
    size_t puppetCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_PUPPETS"), puppetList.size()),
        Party::kMaxPuppetCount);
    for (size_t index = 0; index < puppetCount; ++index) {
        state.puppetIds.push_back(puppetList[index]->getInt("PT_PUPPET_ID", -1));
    }

    const auto availableNpcs = ptGff.getList("PT_AVAIL_NPCS");
    size_t npcCount = std::min(
        availableNpcs.size(), isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount);
    for (size_t npc = 0; npc < npcCount; ++npc) {
        state.npcAvailable[npc] = availableNpcs[npc]->getBool("PT_NPC_AVAIL");
        state.npcSelectable[npc] = availableNpcs[npc]->getBool("PT_NPC_SELECT", true);
    }

    const auto influences = ptGff.getList("PT_INFLUENCE");
    for (size_t npc = 0; npc < std::min(influences.size(), Party::kMaxNpcCount); ++npc) {
        state.influence[npc] = influences[npc]->getInt("PT_NPC_INFLUENCE", -1);
    }

    const auto availablePuppets = ptGff.getList("PT_AVAIL_PUPS");
    for (size_t puppet = 0;
         puppet < std::min(availablePuppets.size(), Party::kMaxPuppetCount);
         ++puppet) {
        state.puppetAvailable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_AVAIL");
        state.puppetSelectable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_SELECT", true);
    }

    state.aiState = ptGff.getInt("PT_AISTATE");
    state.followState = ptGff.getInt("PT_FOLLOWSTATE");
    if (auto galaxy = ptGff.findStruct("GlxyMap")) {
        state.galaxyPointCount = galaxy->getUint("GlxyMapNumPnts");
        uint32_t mask = galaxy->getUint("GlxyMapPlntMsk");
        for (size_t planet = 0; planet < Party::kGalaxyPlanetCount; ++planet) {
            state.planetAvailable[planet] = (mask & (1u << planet)) != 0;
            state.planetSelectable[planet] = (mask & (1u << (planet + 16))) != 0;
        }
        state.selectedPlanet = galaxy->getInt("GlxyMapSelPnt", -1);
    }
    state.mapDisabled = ptGff.getBool("PT_DISABLEMAP");
    state.regenerationDisabled = ptGff.getBool("PT_DISABLEREGEN");
    state.forfeitViolation = ptGff.getInt("FORFEITVIOL");
    state.forfeitConditions = ptGff.getInt("FORFEITCONDS");

    for (const auto &entry : ptGff.getList("PT_DLG_MSG_LIST")) {
        table.dialogMessages.push_back(
            {entry->getString("PT_DLG_MSG_SPKR"), entry->getString("PT_DLG_MSG_MSG")});
    }
    // The saved colour is the highlight flag.
    auto logMessage = [](const Gff &entry, const char *colorLabel, const char *typeLabel, const char *textLabel,
                         MessageLog::Buffer buffer) {
        uint8_t color = 0;
        uint32_t type = 0;
        entry.readByte(color, colorLabel);
        entry.readDword(type, typeLabel);
        return MessageLog::Entry {
            type, color == 1 ? MessageLog::Style::Combat : MessageLog::Style::Normal, entry.getString(textLabel), buffer};
    };
    for (const auto &entry : ptGff.getList("PT_FB_MSG_LIST")) {
        table.logMessages.push_back(
            logMessage(*entry, "PT_FB_MSG_COLOR", "PT_FB_MSG_TYPE", "PT_FB_MSG_MSG", MessageLog::Buffer::Messages));
    }
    for (const auto &entry : ptGff.getList("PT_COM_MSG_LIST")) {
        table.logMessages.push_back(
            logMessage(*entry, "PT_COM_MSG_COOR", "PT_COM_MSG_TYPE", "PT_COM_MSG_MSG", MessageLog::Buffer::Combat));
    }
    // PT_TUT_WND_SHOWN holds one bit per tutorial window, eight windows to a
    // byte. A missing field leaves every tutorial unseen.
    const auto tutorialShown = ptGff.getData("PT_TUT_WND_SHOWN");
    const size_t tutorialBytes = std::min(
        tutorialShown.size(), isTSL() ? Party::kTutorialShownBytes : Party::kK1TutorialShownBytes);
    for (size_t byte = 0; byte < tutorialBytes; ++byte) {
        const auto bits = static_cast<uint8_t>(tutorialShown[byte]);
        for (int bit = 0; bit < 8; ++bit) {
            if ((bits & (1 << bit)) != 0) {
                state.tutorialsShown.insert(static_cast<int>(byte) * 8 + bit);
            }
        }
    }
    return table;
}

void Game::replacePartyTable(PartyTable table) {
    // The saved message lists are appended back to the log's lists.
    for (auto &message : table.dialogMessages) {
        _messageLog.addDialog(std::move(message.speaker), std::move(message.text));
    }
    for (auto &message : table.logMessages) {
        _messageLog.add(message.type, message.style, std::move(message.text), message.buffer);
    }
    _party.loadPersistedState(std::move(table.party));
}

void Game::resetGalaxyMap() {
    // K1 takes its planet count from content; K2 ignores the table and always
    // carries sixteen rows.
    auto planetary = _services.resource.twoDas.get("planetary");
    _party.galaxyMap().reset(_gameId, planetary ? planetary->getRowCount() : 0);
}

void Game::deserializeGalaxyMap(resource::Gff &ptGff) {
    _party.galaxyMap().loadFromPartyTable(ptGff);
}

void Game::deserializePazaakPartyTable(resource::Gff &ptGff) {
    const auto &pazaakCards = ptGff.getList("PT_PAZAAKCARDS");
    const auto &pazaakSide = ptGff.getList("PT_PAZSIDELIST");
    // Each title stores its own number of ownership entries, so the saved table
    // is accepted at either authored length and the card-type ID range follows
    // from it.
    size_t expectedCards = isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount;
    size_t cardTypes = expectedCards - 1;
    if (pazaakCards.size() == expectedCards &&
        pazaakSide.size() == Party::kK1PazaakSideDeckSize) {
        Party::PazaakCardCounts counts {};
        Party::PazaakSideDeck sideDeck;
        bool valid = true;
        for (size_t i = 0; i < expectedCards; ++i) {
            counts[i] = pazaakCards[i]->getInt("PT_PAZAAKCOUNT", -1);
            valid = valid && counts[i] >= 0 && counts[i] <= 255;
        }
        bool allEmpty = true;
        bool allSelected = true;
        Party::PazaakCardCounts selectedCounts {};
        for (size_t i = 0; i < sideDeck.size(); ++i) {
            sideDeck[i] = pazaakSide[i]->getInt("PT_PAZSIDECARD", -2);
            allEmpty = allEmpty && sideDeck[i] == -1;
            bool owned = sideDeck[i] >= 0 && static_cast<size_t>(sideDeck[i]) < cardTypes;
            allSelected = allSelected && owned;
            if (owned) {
                ++selectedCounts[sideDeck[i]];
            }
        }
        valid = valid && (allEmpty || allSelected);
        if (allSelected) {
            for (size_t i = 0; i < cardTypes; ++i) {
                valid = valid && selectedCounts[i] <= counts[i];
            }
        }
        if (valid) {
            _party.setPazaakData(std::move(counts), std::move(sideDeck), expectedCards);
        } else {
            warn("Game: invalid Pazaak state in PARTYTABLE.res");
        }
    } else if (!pazaakCards.empty() || !pazaakSide.empty()) {
        warn("Game: invalid Pazaak list sizes in PARTYTABLE.res");
    }

}

void Game::saveNpcState(int npc) {
    // The game bounds the roster flat, then treats an empty slot as nothing to
    // save rather than an error.
    if (npc < 0 || npc >= static_cast<int>(Party::kMaxNpcCount)) {
        return;
    }
    auto creature = _party.getAvailableMember(npc);
    if (!creature) {
        return;
    }

    saveRosterState({RosterKind::Npc, npc}, *creature);
}

void Game::saveRosterState(
    const RosterIdentity &identity,
    const Creature &creature) {
    if (!_party.isRosterIdentityValid(identity)) {
        throw ValidationException("Roster slot is outside the title range");
    }
    const std::string prefix =
        identity.kind == RosterKind::Npc ? "availnpc" : "availpup";
    auto committed = _services.resource.director.committedSaveWorkingState();
    if (!committed) {
        committed = std::make_shared<const resource::SaveWorkingState>();
    }
    auto candidate =
        resource::SaveWorkingStateCandidate::fromCommitted(std::move(committed));
    candidate.put(
        ResourceId(prefix + std::to_string(identity.slot), ResType::Utc),
        SaveWideSnapshotBuilder::availableNpcRecord(*this, creature));
    _services.resource.director.adoptSaveWorkingState(candidate.freeze());
}

std::shared_ptr<Creature> Game::materializeRosterCreature(
    const RosterIdentity &identity) {
    if (!_party.isRosterAvailable(identity)) return nullptr;
    if (auto existing = _party.rosterCreature(identity)) return existing;

    const std::string prefix =
        identity.kind == RosterKind::Npc ? "availnpc" : "availpup";
    const std::string name = prefix + std::to_string(identity.slot);
    std::shared_ptr<Gff> record;
    try {
        record = decodeSaveGff(
            _services.resource.director.findSaveWorking(
                ResourceId(name, ResType::Utc)));
    } catch (const std::exception &e) {
        warn("Game: invalid " + name + ".utc: " + std::string(e.what()));
        return nullptr;
    }
    if (!record) {
        warn("Game: missing " + name + ".utc");
        return nullptr;
    }

    const auto context =
        SerializedIdentityContext::detachedRecord(name + ".utc");
    auto creature = newCreature(*record, context);
    try {
        creature->captureSaveRecord(
            *record,
            context,
            {identity.kind == RosterKind::Npc
                 ? SaveRecordOriginKind::AvailableNpc
                 : SaveRecordOriginKind::AvailablePuppet,
             std::to_string(identity.slot)});
        if (!_party.bindRosterCreature(identity, creature)) {
            throw ValidationException("Could not bind materialized roster creature");
        }
        // Initial restoration performs one graph-wide bind/publication after
        // every object exists. A lazy GetNPCObject call in an
        // already playable session must complete the same detached-record
        // publication for this one newly materialized object immediately.
        if (_runtimeSessionPlayable) {
            creature->resolveSavedReferences(
                [this, context](uint32_t id) {
                    return resolveSerializedObjectReference(id, context);
                });
            creature->bindSavedRuntimeState();
            creature->publishSavedRuntimeState();
        }
    } catch (...) {
        destroyRuntimeObjectGraph(creature);
        throw;
    }
    return creature;
}

bool Game::killRosterCreature(const RosterIdentity &identity) {
    auto creature = _party.rosterCreature(identity);
    if (!creature) {
        _party.clearRosterCreature(identity);
        return false;
    }
    // Its place in the party stays, naming nothing, and the events still
    // pending for it go with it.
    _party.vacateMemberEntries(*creature);
    if (_module) {
        _module->dropPendingEvents(*creature);
    }
    if (_module && _module->area()) {
        _module->area()->retirePartyMemberAreaRuntime(creature);
    }
    destroyRuntimeObjectGraph(creature);
    return true;
}

void Game::deserializePartyMembers(resource::Gff &ptGff) {
    auto savedMembers = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), savedMembers.size()), 2);
    savedMembers.resize(memberCount);
    auto leader = std::find_if(savedMembers.begin(), savedMembers.end(), [](auto &member) {
        return member->getBool("PT_IS_LEADER");
    });

    auto addMember = [&](resource::Gff &memberGff) {
        int32_t npc = -1;
        if (!memberGff.readInt(npc, "PT_MEMBER_ID")) {
            warn("Game: missing PT_MEMBER_ID");
            return;
        }
        if (_party.isMember(npc)) {
            return;
        }

        const bool controlled =
            npc == _party.persistedState().controlledNpc &&
            _party.player() != _party.actualPlayer();
        auto member = controlled ? _party.player() : _party.getAvailableMember(npc, true);
        if (!member) {
            warn("Game: NPC is not available: " + std::to_string(npc));
            return;
        }
        if (!controlled) {
            _party.spawnIntoPlayerFaction(*member);
        }

        _party.addMember(npc, member);
    };

    // Party leader is the first runtime member. A controlled companion is the
    // module player while pc.utc remains the actual player in limbo.
    if (leader != savedMembers.end()) {
        addMember(**leader);
    }

    auto actualPlayer = _party.actualPlayer();
    // While a companion is controlled in its place, the player character is
    // out of the world and no member of the party; PT_MEMBERS lists only the
    // followers.
    const bool canonicalPlayerIsActive = _party.persistedState().controlledNpc == -1;
    if (canonicalPlayerIsActive && actualPlayer && !_party.isMember(*actualPlayer)) {
        _party.addMember(kNpcPlayer, actualPlayer);
    }

    if (_party.player() != actualPlayer &&
        !_party.isMember(_party.persistedState().controlledNpc)) {
        _party.addMember(_party.persistedState().controlledNpc, _party.player());
    }

    for (auto &savedMember : savedMembers) {
        if (leader == savedMembers.end() || savedMember != *leader) {
            addMember(*savedMember);
        }
    }
}

void Game::deserializeJournal(const resource::Gff &ptGff) {
    for (const auto &jnlEntry : ptGff.getList("JNL_Entries")) {
        std::string plotId(jnlEntry->getString("JNL_PlotID"));
        if (plotId.empty()) {
            warn("Game: missing JNL_PlotID");
            continue;
        }
        int state = jnlEntry->getInt("JNL_State");
        uint32_t date = jnlEntry->getUint("JNL_Date");
        uint32_t time = jnlEntry->getUint("JNL_Time");
        _journal.restoreEntry(std::move(plotId), state, date, time);
    }
}

void Game::deserializeInventory(resource::Gff &inventoryGff) {
    std::shared_ptr<Creature> player = _party.actualPlayer();
    if (!player) {
        return;
    }
    player->deserializeOwnedItems(
        inventoryGff,
        SerializedIdentityContext::detachedRecord("inventory.res"),
        SaveRecordOriginKind::PartyInventoryItem,
        false,
        "inventory");
}

bool Game::loadParty() {
    std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
    if (!ifo) {
        throw ResourceNotFoundException("module.ifo not found");
    }

    return true;
}

void Game::loadDefaultParty() {
    _party.initializeNewGameState();
    std::string member1, member2, member3;
    _party.defaultMembers(member1, member2, member3);

    // A member without a template is left out.
    std::shared_ptr<Creature> player =
        member1.empty() ? nullptr : newCreatureFromBlueprint(member1);
    if (player) {
        player->setTag(kObjectTagPlayer);
        // The stand-in is the player character, in the player faction.
        player->setPC(true);
        player->setFaction(Faction::Player);
        player->setImmortal(true);
        _party.addMember(kNpcPlayer, player);
        _party.setPlayer(player);
        _party.setActualPlayer(player);
    }
    if (auto companion = member2.empty() ? nullptr : newCreatureFromBlueprint(member2)) {
        companion->setImmortal(true);
        companion->equip("g_w_dblsbr001");
        _party.addAvailableMember(0, companion);
        _party.addMember(0, companion);
    }
    if (auto companion = member3.empty() ? nullptr : newCreatureFromBlueprint(member3)) {
        companion->setImmortal(true);
        _party.addAvailableMember(1, companion);
        _party.addMember(1, companion);
    }
}

void Game::setCursorType(CursorType type) {
    _pointer->setType(type);
}

void Game::playVideo(const std::string &name, bool streamingMusic) {
    _moduleTransitionMovies = std::queue<std::string>();
    // A script's movie empties the transition list when it ends.
    _loadComesWithMovies = false;
    startVideo(name, streamingMusic);
}

void Game::playMusic(const std::string &resRef) {
    if (_musicResRef == resRef) {
        return;
    }
    if (_music) {
        _music->stop();
        _music.reset();
    }
    _musicResRef = resRef;
    // Menu music suspends the area music.
    if (!resRef.empty()) {
        _areaMusic.stopSounds();
    }
}

// A new game, TSL's character generation and the module picker set the
// loading-screen music from the module's row of the loading-screen table.
void Game::setLoadMusicForModule(const std::string &moduleName) {
    if (!_loadScreen) {
        _loadScreen = tryLoadGUI<LoadingScreen>();
    }
    _loadMusicResRef = _loadScreen ? _loadScreen->musicResRef(moduleName) : "";
}

// Every loading screen shown with music starts it: the menu music stops;
// nothing starts while transition movies are listed for the load; music that
// is there but not playing is resumed; else the set track starts, looping, in
// the streamed music group, at that group's volume.
void Game::startLoadMusic() {
    playMusic("");
    if (_loadComesWithMovies) {
        return;
    }
    if (_loadMusic) {
        if (!_loadMusic->isPlaying()) {
            _loadMusic->resume();
        }
        return;
    }
    if (_loadMusicResRef.empty()) {
        return;
    }
    _loadMusic = _services.audio.mixer.play(
        _services.resource.audioClips.get(_loadMusicResRef),
        AudioType::Music,
        _areaMusic.musicGroupGain(),
        true);
}

// KotOR's loading screen stops the loading music when it goes; both titles
// stop it when the area's sounds start, and TSL when character generation
// ends.
void Game::stopLoadMusic() {
    if (_loadMusic) {
        _loadMusic->stop();
        _loadMusic.reset();
    }
}

// The menu music fades out over 2500 ms; only the sound runs until 750 ms have
// passed, and then the menu music stops at once.
static constexpr uint32_t kMenuMusicFadeMilliseconds = 2500;
static constexpr uint32_t kMenuMusicWaitMilliseconds = 750;

// The wait is real time.
void Game::fadeOutMenuMusic() {
    using namespace std::chrono;
    const auto start = steady_clock::now();
    for (auto elapsed = milliseconds(0); elapsed < milliseconds(kMenuMusicWaitMilliseconds);
         elapsed = duration_cast<milliseconds>(steady_clock::now() - start)) {
        if (_music) {
            _music->setGainScale(1.0f - static_cast<float>(elapsed.count()) / kMenuMusicFadeMilliseconds);
        }
        _services.audio.mixer.render();
        std::this_thread::sleep_for(milliseconds(1));
    }
    playMusic("");
}

// The playing loading music, when it is the track asked for (ignoring case),
// becomes the background music and plays once through.
std::shared_ptr<AudioSource> Game::takeLoadMusic(const std::string &resRef) {
    if (!_loadMusic || !boost::iequals(resRef, _loadMusicResRef)) {
        return nullptr;
    }
    auto music = std::move(_loadMusic);
    music->setLooping(false);
    return music;
}

void Game::renderScene() {
    if (!_module || !_options.graphics.sceneRender) {
        _lastRenderedSceneOutput = nullptr;
        return;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    auto &output = scene.render({_options.graphics.width, _options.graphics.height});
    _lastRenderedSceneOutput = &output;
    _services.graphics.uniforms.setLocals(std::bind(&LocalUniforms::reset, std::placeholders::_1));
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::ndcTexture));
    _services.graphics.context.bindTexture(output);
    _services.graphics.meshRegistry.get(MeshName::quadNDC).draw(_services.graphics.statistic);
}

// Temporary death

// Recovered party members are placed within this radius of where they lie,
// or of their follow point when they are further than kRegroupDistance from
// the leader and the party is not solo.
static constexpr float kRecoveryPlacementRadius = 5.0f;
static constexpr float kRegroupDistance = 40.0f;
// A creature regarding another at this standing or less is hostile to it.
static constexpr int kHighestHostileReputation = 10;
// The death fade holds the view for twelve seconds before a one second fade to
// black; hurrying it fades at once over half a second.
static constexpr float kDeathFadeWait = 12.0f;
static constexpr float kDeathFadeLength = 1.0f;
static constexpr float kHurriedFadeLength = 0.5f;
// The sequel's gameover panel runs the world at a quarter of its speed.
static constexpr float kDeathPanelTimeScale = 0.25f;
// The first title's death message.
static constexpr int kStrRefDeathMessage = 42351;
// A script ending the game with the end-game GUI keeps it up this long: the
// sequel never counts it down, the first title counts it in world time.
static constexpr float kEndGameGuiDelayTSL = 60.0f;
static constexpr float kEndGameGuiDelayKotOR = 5.0f;

void Game::updateTemporaryDeath() {
    // Party members lie down only in a module's area: the main menu and
    // character generation have none.
    if (!_module) {
        return;
    }
    // Readiness and game over are common; the second title also waits while
    // a modal panel is up. Pausing the simulation does not hold recovery back.
    if (_gameOver || _screen == Screen::Loading || (isTSL() && hasModalPanel())) {
        return;
    }
    auto &area = *_module->area();

    std::shared_ptr<Creature> reference;
    bool anyDown = false;
    bool allDown = true;
    for (const Party::Member &member : _party.members()) {
        // The last member's creature anchors the hostile scan; a member without
        // one keeps the party from counting as fallen.
        reference = member.creature;
        if (!reference) {
            allDown = false;
            continue;
        }
        if (reference->isTemporarilyDead()) {
            anyDown = true;
        } else {
            allDown = false;
        }
    }
    if (allDown && !_party.members().empty()) {
        runDeathSequence();
    }

    const auto milliseconds = _services.system.clock.millis();
    const bool recover = _temporaryDeathRecovery.sample(
        static_cast<std::uint32_t>(milliseconds), anyDown, [&]() {
            if (!reference) {
                return false;
            }
            for (const auto &object : area.getObjectsByType(ObjectType::Creature)) {
                const auto creature = dyn_cast<Creature>(object);
                // Dead nonparty candidates are not excluded by the scan.
                if (!creature || _party.isMember(*creature)) {
                    continue;
                }
                if (creature->getReputationToward(*reference) > kHighestHostileReputation) {
                    continue;
                }
                const bool seesMember = std::any_of(
                    _party.members().begin(), _party.members().end(), [&](const Party::Member &member) {
                        return member.creature && creature->perception().seen.count(member.creature->id()) != 0;
                    });
                if (seesMember) {
                    return true;
                }
            }
            return false;
        });
    if (!recover) {
        return;
    }

    // The leader's position is taken before anyone moves. Every member, fallen
    // or not, that ends up too far from it regroups at its follow point.
    const glm::vec3 leaderPosition {_party.getLeader()->position()};
    const auto &members = _party.members();
    for (size_t slot = 0; slot < members.size(); ++slot) {
        const auto &creature = members[slot].creature;
        if (!creature) {
            continue;
        }
        if (creature->isTemporarilyDead()) {
            const glm::vec3 lying {creature->position()};
            creature->setPosition(area.computeSafeLocation(lying, kRecoveryPlacementRadius, *creature, false).value_or(lying));
            area.determineObjectRoom(*creature);
            creature->applyEffect(newEffect<ResurrectionEffect>(0), DurationType::Instant);
            // The creature becomes raiseable only once the resurrection is
            // applied, so a first attempt may be refused; the next recovery
            // tries again.
            creature->setRaiseable(true);
        }
        // Only the follow slots have follow points.
        if (slot >= static_cast<size_t>(kPartyFollowSlots) || _party.isSoloMode()) {
            continue;
        }
        const glm::vec3 offset {creature->position() - leaderPosition};
        if (glm::dot(offset, offset) <= kRegroupDistance * kRegroupDistance) {
            continue;
        }
        const glm::vec3 followPoint {_party.followPoint(static_cast<int>(slot))};
        creature->setPosition(area.computeSafeLocation(followPoint, kRecoveryPlacementRadius, *creature, false).value_or(followPoint));
        area.determineObjectRoom(*creature);
    }
}

bool Game::hasModalPanel() const {
    if (_screen == Screen::InGame) {
        return _confirmPopup && _confirmPopup->isVisible();
    }
    // These exclusive screens own input instead of the world. The complete
    // modal stack of the GUI manager is not exposed to scripts.
    switch (_screen) {
    case Screen::InGameMenu:
    case Screen::Container:
    case Screen::PartySelection:
    case Screen::SaveLoad:
    case Screen::PazaakWager:
    case Screen::PazaakSetup:
    case Screen::PazaakBoard:
    case Screen::Death:
    case Screen::MainMenu:
        return true;
    default:
        return false;
    }
}

void Game::setLastPartyMemberTempKilled(const Creature &creature) {
    if (_party.isMember(creature)) {
        _lastPartyMemberTempKilled = creature.id();
    }
}

// The death sequence ends play: panels close, the title's death message comes
// up, the camera starts orbiting the last party member to fall, and the view
// holds before fading to black. Only a fallen party in a module's area starts it.
void Game::runDeathSequence() {
    _gameOver = true;
    // Every panel closes, a tutorial window about to open included.
    _tutorialPending.reset();
    stopMovement();
    const Camera *view = getActiveCamera();
    const glm::mat4 viewTransform {view ? view->sceneNode()->absoluteTransform() : glm::mat4 {1.0f}};
    exitFreeLook();
    displayDeathMessage();
    auto &camera = *_module->area()->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
    const auto fallen = getObjectById(_lastPartyMemberTempKilled);
    if (fallen && fallen->type() == ObjectType::Creature) {
        camera.startDeathOrbit(fallen, viewTransform);
        _cameraType = CameraType::ThirdPerson;
    }
    _globalFade.request(GlobalFade::Direction::Out, kDeathFadeWait, kDeathFadeLength, glm::vec3 {0.0f});
}

void Game::displayDeathMessage() {
    setRelativeMouseMode(false);
    changeScreen(Screen::Death);
    if (isTSL()) {
        if (!_deathDisplay) {
            _deathDisplay = std::make_unique<DeathDisplay>(*this, _services);
            _deathDisplay->init();
        }
        _deathDisplay->present();
    } else {
        if (!_deathMessage) {
            _deathMessage = std::make_unique<ConfirmPopup>(*this, _services);
            _deathMessage->init();
        }
        // Queue end-game so the GUI isn't destroyed in its own callback.
        _deathMessage->show(_services.resource.strings.getText(kStrRefDeathMessage), nullptr, [this]() { requestEndGame(); });
    }
}

bool Game::isDeathMessageDisplayed() const {
    if (isTSL()) {
        return _deathDisplay && _deathDisplay->isPresented();
    }
    return _deathMessage && _deathMessage->isVisible();
}

// Ending the game with the end-game GUI puts the first title's game over, so
// the world slows as after a party wipe and a fallen party starts no death
// sequence of its own. It plays no movie and stops nothing; the world and its
// scripts run on until the game returns to the main menu.
void Game::endGame(bool showGui) {
    _endGamePending = true;
    _showEndGameGui = showGui;
    _endGameDelay = showGui ? (isTSL() ? kEndGameGuiDelayTSL : kEndGameGuiDelayKotOR) : 0.0f;
    if (showGui && !isTSL()) {
        _gameOver = true;
    }
}

// The credits go over whatever is on screen and take every input while they
// are up; the world and its scripts run on underneath.
void Game::startCreditSequence(bool transparentBackground, const std::string &music) {
    if (_credits) {
        return;
    }
    _credits = std::make_unique<CreditsGUI>(*this, _services, transparentBackground, music);
    _credits->init();
    _areaMusic.stopSounds();
}

void Game::hurryDeathSequence() {
    _globalFade.request(GlobalFade::Direction::Out, 0.0f, kHurriedFadeLength, glm::vec3 {0.0f});
}

// The first title ends the game once the fade to black has finished or an end
// was asked for, and the death message of a scripted end has had its time; its
// world keeps full speed throughout. The sequel's gameover panel slows the
// world for as long as it is up.
void Game::updateDeathSequence() {
    if (isTSL()) {
        _deathTimeScale = _deathDisplay && _deathDisplay->isPresented() ? kDeathPanelTimeScale : 1.0f;
        return;
    }
    if (!_gameOver) {
        return;
    }
    if ((_globalFade.fading() && !_endGamePending) || _endGameDelay > 0.0f) {
        return;
    }
    _gameOver = false;
    _globalFade.stop();
    requestEndGame();
    if (_deathMessage) {
        _deathMessage->hide();
    }
}

Game::LastSaveLaunch Game::launchMostRecentSave() {
    _saveLoad->setMode(SaveLoadMode::LoadAfterDeath);
    _saveLoad->refresh();
    if (_saveLoad->savedGameCount() == 0) {
        return LastSaveLaunch::NoSaves;
    }
    return _saveLoad->launchMostRecentSave() ? LastSaveLaunch::Launched : LastSaveLaunch::Unavailable;
}

// While the game is over and the controlled character lies dead, the menu key
// and the menu page keys hurry the death sequence to black.
bool Game::handleDeathSequenceKey(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Escape:
    case input::KeyCode::U:
    case input::KeyCode::I:
    case input::KeyCode::P:
    case input::KeyCode::K:
    case input::KeyCode::J:
    case input::KeyCode::L:
    case input::KeyCode::M:
    case input::KeyCode::O: {
        auto leader = _party.getLeader();
        if (!leader || !leader->isTemporarilyDead()) {
            return false;
        }
        hurryDeathSequence();
        return true;
    }
    default:
        return false;
    }
}

// END Temporary death

// Free-look toggles on Caps Lock. Neither way works while the leader is in
// combat state; it opens only with nothing queued for the leader and play
// running.
bool Game::handleFreeLookKey(const input::KeyEvent &event) {
    auto leader = _party.getLeader();
    if (event.code == input::KeyCode::CapsLock) {
        if (!leader || leader->isInCombat() || _gameOver) {
            return true;
        }
        if (_freeLook) {
            exitFreeLook();
        } else if (_cameraType == CameraType::ThirdPerson && !leader->hasOrdinaryActionsPending() && !_paused) {
            enterFreeLook();
        }
        return true;
    }
    if (!_freeLook) {
        return false;
    }
    switch (event.code) {
    // Selection, the solo query and the menu keys only leave free-look.
    case input::KeyCode::Q:
    case input::KeyCode::E:
    case input::KeyCode::V:
    case input::KeyCode::Escape:
    case input::KeyCode::Return:
        exitFreeLook();
        return true;
    case input::KeyCode::Tab:
        exitFreeLook();
        if (auto clip = _services.game.guiSounds.getActionAccepted()) {
            _services.audio.mixer.play(std::move(clip), AudioType::Sound);
        }
        return true;
    // The pause key and the menu pages leave free-look and then act.
    case input::KeyCode::Pause:
    case input::KeyCode::U:
    case input::KeyCode::I:
    case input::KeyCode::P:
    case input::KeyCode::K:
    case input::KeyCode::J:
    case input::KeyCode::L:
    case input::KeyCode::M:
    case input::KeyCode::O:
        exitFreeLook();
        return false;
    // Keys free-look does not take.
    case input::KeyCode::Space:
    case input::KeyCode::G:
    case input::KeyCode::X:
    case input::KeyCode::Y:
    case input::KeyCode::F5:
        return true;
    default:
        return false;
    }
}

// Entering turns the leader to where the camera looked and looks from its
// head. Free-look is entered and left only in a module's area.
void Game::enterFreeLook() {
    auto leader = _party.getLeader();
    auto &area = *_module->area();
    _module->player().stopMovement();
    auto &thirdPerson = *area.getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
    auto &firstPerson = *area.getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    leader->setFacing(thirdPerson.facing());
    firstPerson.attach(leader, area.camStyleDefault());
    enableVideoEffect(leader->freeLookVideoEffect());
    _cameraType = CameraType::FirstPerson;
    _freeLook = true;
    // The leader at rest shows its pause or ready loop, never the injured one.
    leader->refreshPauseAnimation();
    setRelativeMouseMode(true);
    area.updateRoomVisibility();
}

void Game::exitFreeLook() {
    if (!_freeLook) {
        return;
    }
    _freeLook = false;
    disableVideoEffect();
    auto &area = *_module->area();
    area.getCamera<FirstPersonCamera>(CameraType::FirstPerson)->detach();
    if (_cameraType == CameraType::FirstPerson) {
        _cameraType = CameraType::ThirdPerson;
    }
    // Out of free-look an injured leader standing in its pause shows the
    // injured pause again.
    if (auto leader = _party.getLeader(); leader && leader->showsPauseReadyAnimation()) {
        leader->refreshPauseAnimation();
    }
    setRelativeMouseMode(false);
    area.updateRoomVisibility();
}

// Free-look ends when play leaves the world, a party member is in combat
// state, or the leader has something queued.
void Game::updateFreeLookExits() {
    if (!_freeLook) {
        return;
    }
    auto leader = _party.getLeader();
    auto &firstPerson = *_module->area()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    bool exit = _screen != Screen::InGame || !leader || !firstPerson.isAttached() ||
                leader->hasOrdinaryActionsPending();
    for (int i = 0; !exit && i < _party.getSize(); ++i) {
        auto member = _party.getMember(i);
        if (member && member->isInCombat()) {
            exit = true;
        }
    }
    if (exit) {
        exitFreeLook();
    }
}

void Game::toggleInGameCameraType() {
    switch (_cameraType) {
    case CameraType::FirstPerson:
        if (_party.getLeader()) {
            _cameraType = CameraType::ThirdPerson;
        }
        break;
    case CameraType::ThirdPerson: {
        _module->player().stopMovement();
        std::shared_ptr<Area> area(_module->area());
        auto thirdPerson = area->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
        auto firstPerson = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
        firstPerson->setPosition(thirdPerson->sceneNode()->origin());
        firstPerson->setFacing(thirdPerson->facing());
        _cameraType = CameraType::FirstPerson;
        break;
    }
    default:
        break;
    }

    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);

    _module->area()->updateRoomVisibility();
}

Camera *Game::getActiveCamera() const {
    if (!_module) {
        return nullptr;
    }
    std::shared_ptr<Area> area(_module->area());
    if (!area) {
        return nullptr;
    }
    return area->getCamera(_cameraType);
}

std::shared_ptr<Object> Game::getObjectById(uint32_t id) const {
    switch (id) {
    case kObjectSelf:
        throw std::invalid_argument("Invalid id: " + std::to_string(id));
    case kObjectInvalid:
        return nullptr;
    default: {
        auto it = _objectById.find(id);
        return it != _objectById.end() ? it->second : nullptr;
    }
    }
}

int Game::scaleDamageForDifficulty(
    int damage,
    const Object &target) const {
    if (damage <= 0 || !_party.isMember(target)) {
        return damage;
    }

    const DifficultyOption &difficulty =
        _services.game.difficultyOptions.get(_options.game.clientDifficulty);
    return static_cast<int>(
        static_cast<float>(damage) * difficulty.damageMultiplier);
}

// The client difficulty settings that shift TSL trap DCs.
static constexpr int kEasyDifficulty = 0;
static constexpr int kDifficultDifficulty = 2;
static constexpr int kTrapDifficultyShift = 5;

int Game::trapDifficultyModifier() const {
    if (!isTSL()) {
        return 0;
    }
    switch (_options.game.clientDifficulty) {
    case kEasyDifficulty:
        return -kTrapDifficultyShift;
    case kDifficultDifficulty:
        return kTrapDifficultyShift;
    default:
        return 0;
    }
}

bool Game::isRuntimeObjectLive(const Object &object) const {
    if (!object.isRuntimeLive()) {
        return false;
    }
    auto found = _objectById.find(object.id());
    return found != _objectById.end() && found->second.get() == &object;
}

bool Game::isRuntimeObjectAttachable(const Object &object) const {
    if (isRuntimeObjectLive(object)) {
        return true;
    }
    if (object._runtimeState != Object::RuntimeState::Constructing ||
        !_stagedRuntimeObjectGraph) {
        return false;
    }
    auto found = _stagedRuntimeObjectGraph->objectById.find(object.id());
    return found != _stagedRuntimeObjectGraph->objectById.end() &&
           found->second.get() == &object;
}

void Game::unregisterRuntimeObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    // Retained storage is no longer an action owner after this boundary.
    // Retire only transient dialogue admissions: arbitrary cancellation scripts
    // must not run while an outgoing or replaced graph is being retired.
    for (const auto &action : object->actions()) {
        if (auto conversation = dyn_cast<StartConversationAction>(action)) {
            conversation->cancel(action, *object);
            action->markCancelled();
        }
    }
    object->_runtimeState = Object::RuntimeState::Retired;
    if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
        // A PartyTable binding denotes a live runtime object, not storage
        // ownership. Pointer-guarded clearing cannot disturb a replacement
        // that reused the same runtime number.
        _party.clearRosterCreature(*creature);
    }
    auto registered = _objectById.find(object->id());
    if (registered != _objectById.end() &&
        registered->second.get() == object.get()) {
        _objectById.erase(registered);
    }

    // Include the structural Module alias, which deliberately has no reverse
    // serialized identity on the Module object itself.
    for (auto it = _objectBySavedId.begin(); it != _objectBySavedId.end();) {
        if (it->second.lock().get() == object.get()) {
            it = _objectBySavedId.erase(it);
        } else {
            ++it;
        }
    }
    _savedIdByObject.erase(object.get());
}

void Game::destroyRuntimeObjectGraph(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    auto graph = collectRuntimeObjectGraph({object});
    // Children cease to exist before their owner. The pointer guard makes this
    // idempotent and protects a newer object if an explicit ID was reused.
    for (auto it = graph.rbegin(); it != graph.rend(); ++it) {
        unregisterRuntimeObject(*it);
    }
}

std::vector<std::shared_ptr<Object>> Game::collectRuntimeObjectGraph(
    const std::vector<std::shared_ptr<Object>> &roots) const {
    std::vector<std::shared_ptr<Object>> pending(roots);
    std::set<const Object *> seen;
    std::vector<std::shared_ptr<Object>> graph;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!current || !seen.insert(current.get()).second) {
            continue;
        }
        graph.push_back(current);
        for (auto &owned : current->ownedRuntimeObjects()) {
            pending.push_back(std::move(owned));
        }
    }
    return graph;
}

void Game::discardStagedRuntimeObjects(
    const std::vector<std::shared_ptr<Object>> &objects) {
    if (!_stagedRuntimeObjectGraph) {
        throw ValidationException("No staged runtime object graph is active");
    }
    auto &staged = *_stagedRuntimeObjectGraph;
    for (const auto &object : objects) {
        if (!object) {
            continue;
        }
        auto byId = staged.objectById.find(object->id());
        if (byId != staged.objectById.end() &&
            byId->second.get() == object.get()) {
            staged.objectById.erase(byId);
        }
        for (auto it = staged.objectBySavedId.begin();
             it != staged.objectBySavedId.end();) {
            if (it->second.lock().get() == object.get()) {
                it = staged.objectBySavedId.erase(it);
            } else {
                ++it;
            }
        }
        staged.savedIdByObject.erase(object.get());
        staged.publishedRuntimeObjectIds.erase(object->id());
        staged.reservedSavedObjectIdsToRelease.erase(object->id());
        object->_runtimeState = Object::RuntimeState::Retired;
    }
    staged.candidateObjects.erase(
        std::remove_if(
            staged.candidateObjects.begin(),
            staged.candidateObjects.end(),
            [&](const auto &candidate) {
                return std::find(objects.begin(), objects.end(), candidate) !=
                       objects.end();
            }),
        staged.candidateObjects.end());
    // Equipment replaced before it was ever live is never announced.
    staged.equippedOnLoad.erase(
        std::remove_if(
            staged.equippedOnLoad.begin(),
            staged.equippedOnLoad.end(),
            [&](const auto &equipped) {
                return std::find(objects.begin(), objects.end(), equipped.item) !=
                       objects.end();
            }),
        staged.equippedOnLoad.end());
}

void Game::beginRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &obsoleteObjects) {
    if (_stagedRuntimeObjectGraph) {
        throw ValidationException("Nested runtime object graph replacement");
    }
    _stagedRuntimeObjectGraph.emplace(StagedRuntimeObjectGraph {});
    _stagedRuntimeObjectGraph->initialNextObjectId = _nextObjectId;

    _stagedRuntimeObjectGraph->obsoleteGraph =
        collectRuntimeObjectGraph(obsoleteObjects);
    for (const auto &object : _stagedRuntimeObjectGraph->obsoleteGraph) {
        _stagedRuntimeObjectGraph->replaceableObjectIds.insert(object->id());
    }
}

std::vector<Game::EquippedOnLoad> Game::commitRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &) {
    if (!_stagedRuntimeObjectGraph) {
        throw ValidationException("No runtime object graph replacement is active");
    }

    auto staged = std::move(*_stagedRuntimeObjectGraph);
    _stagedRuntimeObjectGraph.reset();

    // The complete old graph was discovered before candidate construction and
    // before the no-throw ownership publication. Nothing below walks ownership
    // or allocates graph bookkeeping after that irreversible boundary.
    for (auto it = staged.obsoleteGraph.rbegin();
         it != staged.obsoleteGraph.rend(); ++it) {
        unregisterRuntimeObject(*it);
    }
    _objectById.merge(staged.objectById);
    _publishedRuntimeObjectIds.merge(staged.publishedRuntimeObjectIds);
    _objectBySavedId.merge(staged.objectBySavedId);
    _savedIdByObject.merge(staged.savedIdByObject);
    for (const auto &object : staged.candidateObjects) {
        auto found = _objectById.find(object->id());
        if (found != _objectById.end() &&
            found->second.get() == object.get()) {
            object->_runtimeState = Object::RuntimeState::Live;
        }
    }
    for (uint32_t id : staged.reservedSavedObjectIdsToRelease) {
        _reservedSavedObjectIds.erase(id);
    }

    // All collisions are rejected while staging. A non-empty source here
    // would mean the supposedly atomic publication silently lost a node.
    if (!staged.objectById.empty() ||
        !staged.publishedRuntimeObjectIds.empty() ||
        !staged.objectBySavedId.empty() ||
        !staged.savedIdByObject.empty()) {
        std::terminate();
    }
    return std::move(staged.equippedOnLoad);
}

void Game::abortRuntimeObjectGraphReplacement() {
    if (!_stagedRuntimeObjectGraph) {
        return;
    }
    for (const auto &object : _stagedRuntimeObjectGraph->candidateObjects) {
        object->_runtimeState = Object::RuntimeState::Retired;
    }
    _nextObjectId = _stagedRuntimeObjectGraph->initialNextObjectId;
    _stagedRuntimeObjectGraph.reset();
}

std::shared_ptr<Object> Game::getObjectBySavedId(uint32_t id) const {
    auto found = _objectBySavedId.find(id);
    if (found == _objectBySavedId.end()) {
        return nullptr;
    }
    auto object = found->second.lock();
    return object && isRuntimeObjectLive(*object) ? object : nullptr;
}

uint32_t Game::savedObjectId(const resource::Gff &gff) const {
    uint32_t id = 0;
    if (!gff.readDword(id, "ObjectId")) {
        throw ValidationException("Saved runtime object is missing ObjectId");
    }
    return id;
}

void Game::registerObject(
    const std::shared_ptr<Object> &object,
    bool allowReserved) {
    uint32_t id = object->id();
    if (id == std::numeric_limits<uint32_t>::max()) {
        throw ValidationException("Invalid saved ObjectId");
    }
    if (!allowReserved && id < kFirstRuntimeObjectId) {
        throw ValidationException("Reserved saved ObjectId: " + std::to_string(id));
    }
    if (object->_runtimeState != Object::RuntimeState::Constructing ||
        object->_runtimeIncarnation != 0) {
        throw ValidationException("Runtime object storage cannot be republished");
    }
    if (_objectById.count(id) != 0) {
        throw ValidationException("Duplicate runtime ObjectId: " + std::to_string(id));
    }
    if (_publishedRuntimeObjectIds.count(id) != 0) {
        throw ValidationException(
            "Runtime ObjectId was already published in this session: " +
            std::to_string(id));
    }
    object->_runtimeIncarnation = _nextRuntimeIncarnation++;
    if (_stagedRuntimeObjectGraph) {
        if (!_stagedRuntimeObjectGraph->publishedRuntimeObjectIds.insert(id).second) {
            throw ValidationException(
                "Runtime ObjectId was already staged in this session: " +
                std::to_string(id));
        }
        if (!_stagedRuntimeObjectGraph->objectById.emplace(id, object).second) {
            throw ValidationException(
                "Duplicate staged runtime ObjectId: " + std::to_string(id));
        }
        _stagedRuntimeObjectGraph->candidateObjects.push_back(object);
        _stagedRuntimeObjectGraph->reservedSavedObjectIdsToRelease.insert(id);
    } else {
        _publishedRuntimeObjectIds.insert(id);
        _objectById.emplace(id, object);
        object->_runtimeState = Object::RuntimeState::Live;
        _reservedSavedObjectIds.erase(id);
    }
}

void Game::registerSavedObjectIdentity(
    uint32_t id,
    const std::shared_ptr<Object> &object,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        throw ValidationException(
            "Cannot register a non-authoritative saved object identity");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (id == std::numeric_limits<uint32_t>::max() || !object) {
        throw ValidationException("Invalid saved ObjectId mapping");
    }
    auto existing = getObjectBySavedId(id);
    if (existing && existing.get() != object.get() &&
        (!_stagedRuntimeObjectGraph ||
         !_stagedRuntimeObjectGraph->replaceableObjectIds.count(existing->id()))) {
        throw ValidationException(
            "Duplicate authoritative saved ObjectId mapping: " +
            std::to_string(id));
    }
    if (_stagedRuntimeObjectGraph) {
        auto staged = _stagedRuntimeObjectGraph->objectBySavedId.find(id);
        if (staged != _stagedRuntimeObjectGraph->objectBySavedId.end() &&
            staged->second.lock().get() != object.get()) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId mapping: " +
                std::to_string(id));
        }
    }
    const auto &reverseMap = _stagedRuntimeObjectGraph
                                 ? _stagedRuntimeObjectGraph->savedIdByObject
                                 : _savedIdByObject;
    auto reverse = reverseMap.find(object.get());
    if (reverse != reverseMap.end() && reverse->second != id) {
        throw ValidationException("Runtime object has multiple saved ObjectIds");
    }
    auto &savedMap = _stagedRuntimeObjectGraph
                         ? _stagedRuntimeObjectGraph->objectBySavedId
                         : _objectBySavedId;
    savedMap[id] = object;
    auto &canonicalId = _stagedRuntimeObjectGraph
                            ? _stagedRuntimeObjectGraph->savedIdByObject[object.get()]
                            : _savedIdByObject[object.get()];
    canonicalId = id;
    object->assignSerializedObjectIdentity({identityContext, id});
}

void Game::registerSavedModuleReferenceTarget(
    const std::shared_ptr<Module> &module,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds() || !module) {
        throw ValidationException("Invalid saved structural Module target");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (_reservedSavedObjectIds.count(kSavedRuntimeModuleObjectId) != 0) {
        throw ValidationException(
            "Saved ObjectId 0 collides with the structural Module target");
    }
    auto existing = getObjectBySavedId(kSavedRuntimeModuleObjectId);
    if (existing && existing.get() != module.get()) {
        throw ValidationException("Duplicate saved structural Module target");
    }
    auto &savedMap = _stagedRuntimeObjectGraph
                         ? _stagedRuntimeObjectGraph->objectBySavedId
                         : _objectBySavedId;
    auto [found, inserted] = savedMap.emplace(
        kSavedRuntimeModuleObjectId, module);
    if (!inserted) {
        auto existing = found->second.lock();
        if (!existing || existing.get() != module.get()) {
            throw ValidationException(
                "Duplicate saved structural Module target");
        }
    }
}

std::shared_ptr<Item> Game::newItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    return newObjectFromGff<Item>(gff, identityContext, *this, _services);
}

std::shared_ptr<Item> Game::newItemFromBlueprint(const std::string &resRef) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Item> item;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            item = newItem();
            item->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return item;
}

std::shared_ptr<Item> Game::newItemClone(const Item &source) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Item> item;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            item = newItem();
            item->clone(source);
        },
        []() noexcept {});
    return item;
}

std::shared_ptr<Item> Game::newOwnedItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    return newObjectFromGff<Item>(gff, identityContext, *this, _services);
}

std::shared_ptr<Area> Game::newSavedArea(
    uint32_t id,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Area> area;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            area = newArea(std::move(sceneName));
            registerSavedObjectIdentity(id, area, identityContext);
        },
        []() noexcept {});
    return area;
}

std::shared_ptr<Creature> Game::newCreature(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Creature>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Creature> Game::newCreatureFromBlueprint(
    const std::string &resRef,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Creature> creature;
    bool loaded = false;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            creature = newCreature(std::move(sceneName));
            loaded = creature->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    // A creature without a template is not created.
    if (!loaded) {
        destroyRuntimeObjectGraph(creature);
        return nullptr;
    }
    return creature;
}

std::shared_ptr<Placeable> Game::newPlaceable(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Placeable>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Placeable> Game::newPlaceableFromBlueprint(
    const std::string &resRef,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Placeable> placeable;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            placeable = newPlaceable(std::move(sceneName));
            placeable->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return placeable;
}

std::shared_ptr<Door> Game::newDoor(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Door>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Waypoint> Game::newWaypoint(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Waypoint>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Waypoint> Game::newWaypointFromBlueprint(const std::string &resRef) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Waypoint> waypoint;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            waypoint = newWaypoint();
            waypoint->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return waypoint;
}

std::shared_ptr<Trigger> Game::newTrigger(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Trigger>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Sound> Game::newSound(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Sound>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<AreaOfEffect> Game::newAreaOfEffect(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<AreaOfEffect>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Encounter> Game::newEncounter(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Encounter>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Store> Game::newStore(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Store>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Store> Game::newStoreFromBlueprint(const std::string &resRef) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Store> store;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            store = newStore();
            store->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return store;
}

void Game::prepareSavedRuntimeNamespace(
    const resource::Gff &ifo,
    const SerializedIdentityContext &identityContext) {
    reserveSavedObjectIds(ifo, identityContext, SerializedGraphRoot::ModuleIfo);

    uint32_t nextObjectId = kFirstRuntimeObjectId;
    if (ifo.readDword(nextObjectId, "Mod_NextObjId0") &&
        nextObjectId != 0 &&
        nextObjectId < kFirstRuntimeObjectId) {
        throw ValidationException("Invalid Mod_NextObjId0");
    }
    _nextObjectId = std::max(nextObjectId, kFirstRuntimeObjectId);

    uint64_t nextEffectId = 0;
    if (ifo.readDword64(nextEffectId, "Mod_Effect_NxtId")) {
        if (!setNextEffectId(nextEffectId)) {
            throw ValidationException("Invalid Mod_Effect_NxtId");
        }
    }

    const uint64_t pauseDay = ifo.getUint("Mod_PauseDay");
    const uint64_t pauseTime = ifo.getUint("Mod_PauseTime");
    restoreWorldTime(ifo, pauseDay, pauseTime);
}

void Game::restoreWorldTime(
    const resource::Gff &moduleIfo,
    uint64_t pauseDay,
    uint64_t pauseTime) {
    // Mod_MinPerHour first: it defines the day length that Mod_PauseTime is
    // measured against.
    uint32_t minutesPerHour = moduleIfo.getUint("Mod_MinPerHour");
    if (minutesPerHour > std::numeric_limits<uint8_t>::max()) {
        throw ValidationException("Invalid Mod_MinPerHour");
    }
    _minutesPerHour = minutesPerHour == 0
                          ? 5
                          : static_cast<uint8_t>(minutesPerHour);

    // Compose the absolute clock from the saved day/time pair. Oversized time
    // carries into later days. Both inputs are DWORDs, so the 64-bit composition cannot
    // overflow.
    _worldTimeMilliseconds =
        pauseDay * millisecondsPerWorldDay() + pauseTime;
    _worldTimeFraction = 0.0;
    _worldClockSample.reset();
}

void Game::reserveSavedObjectIds(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SerializedGraphRoot graphRoot) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        return;
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
        throw ValidationException("Cannot mix authoritative saved object namespaces");
    }
    for (auto &claim : collectSerializedObjectIdClaims(gff, identityContext, graphRoot)) {
        if (claim.id == std::numeric_limits<uint32_t>::max()) {
            throw ValidationException("Invalid saved ObjectId");
        }
        auto [found, inserted] = _reservedSavedObjectIdClaims.emplace(claim.id, claim.path);
        if (!inserted && found->second != claim.path) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId " +
                std::to_string(claim.id) + " at " + found->second +
                " and " + claim.path);
        }
        _reservedSavedObjectIds.insert(claim.id);
    }
}

void Game::resolveSavedObjectReferences() {
    for (const auto &[_, object] : _objectById) {
        const auto identityContext = object->_savedRuntimeIdentityContext;
        object->resolveSavedReferences(
            [this, identityContext](uint32_t id) {
                return resolveSerializedObjectReference(id, identityContext);
            });
    }
}

void Game::bindSavedRuntimeState() {
    if (!_module) {
        return;
    }
    resolveSavedObjectReferences();
    for (const auto &[_, object] : _objectById) {
        object->bindSavedRuntimeState();
    }
    _module->bindSavedEventQueue();
}

void Game::publishSavedRuntimeState() {
    if (!_module) {
        return;
    }
    for (const auto &[_, object] : _objectById) {
        object->publishSavedRuntimeState();
    }
    for (const auto &[_, object] : _objectById) {
        auto actor = std::dynamic_pointer_cast<Creature>(object);
        if (!actor) {
            continue;
        }
        for (const auto &action : actor->actions()) {
            const auto &saved = action->originalSavedAction();
            if (saved && saved->round) {
                _combat.restoreRound(action, actor, *saved->round);
            }
        }
        // Rounds no queued command carries: one still running after its
        // command was cleared, or a side ended while its partner's runs on.
        for (const auto &saved : actor->_savedRounds) {
            _combat.restoreRound(actor, saved);
        }
        actor->_savedRounds.clear();
    }
    for (const auto &[_, object] : _objectById) {
        auto actor = std::dynamic_pointer_cast<Creature>(object);
        if (actor)
            _combat.restoreScheduled(actor, actor->_savedScheduledActions,
                                     actor->_savedScheduledReferencesBound, actor->_savedScheduledTime);
        // A creature's round has one pause: a round restored with its own
        // keeps it, and only a creature owning no round is held by the saved one.
        if (actor && actor->_savedOwnerHold > 0.0f) {
            if (!_combat.ownsRound(*actor)) {
                _combat.restoreOwnerHold(actor, actor->_savedOwnerHold, actor->_savedOwnerHoldBy.boundObject());
            }
            actor->_savedOwnerHold = 0.0f;
        }
    }
    _module->restoreProjectilePresentations();
    _module->publishSavedEventQueue();
    syncClientCombatMode();
}

// One simulation second advances a clock by 1000 milliseconds.
static void advanceClock(uint64_t &milliseconds, double &fraction, double dt) {
    if (dt <= 0.0f) {
        return;
    }
    double gameMilliseconds = fraction + static_cast<double>(dt) * 1000.0;
    uint64_t wholeMilliseconds =
        static_cast<uint64_t>(std::floor(gameMilliseconds));
    fraction = gameMilliseconds - static_cast<double>(wholeMilliseconds);
    milliseconds += wholeMilliseconds;
}

void Game::advanceWorldTime(double dt) {
    // Mod_MinPerHour changes the day boundary, not the clock rate; scaling the
    // clock rate would shorten effect durations and delayed actions.
    advanceClock(_worldTimeMilliseconds, _worldTimeFraction, dt);
}

void Game::advanceTimeStopClock(double dt) {
    advanceClock(_timeStopMilliseconds, _timeStopFraction, dt);
}

void Game::toggleTimeStop() {
    _timeStopped = !_timeStopped;
    _timeStopMilliseconds = _worldTimeMilliseconds;
    _timeStopFraction = 0.0;
}

void Game::addTimeStopExclusion(Object &object) {
    if (isExcludedFromTimeStop(object)) {
        return;
    }
    _timeStopExclusions.emplace_back(getObjectById(object.id()));
}

void Game::removeTimeStopExclusion(const Object &object) {
    auto excluded = std::find_if(_timeStopExclusions.begin(), _timeStopExclusions.end(),
                                 [&object](const auto &ref) { return ref.resolve().get() == &object; });
    if (excluded != _timeStopExclusions.end()) {
        _timeStopExclusions.erase(excluded);
    }
}

bool Game::isExcludedFromTimeStop(const Object &object) const {
    return std::any_of(_timeStopExclusions.begin(), _timeStopExclusions.end(),
                       [&object](const auto &ref) { return ref.resolve().get() == &object; });
}

void Game::updateTimeStop() {
    if (!_timeStopped) {
        return;
    }
    const bool held = std::any_of(_timeStopExclusions.begin(), _timeStopExclusions.end(), [](const auto &ref) {
        auto creature = std::dynamic_pointer_cast<Creature>(ref.resolve());
        return creature && creature->hasEffect(EffectType::TimeStop);
    });
    if (!held) {
        toggleTimeStop();
    }
}

void Game::queueScriptEvent(Object &target, std::optional<std::reference_wrapper<const Object>> caller, const Event &event) {
    SavedEventRecord queued;
    queued.day = worldTimeDay();
    queued.time = worldTimeOfDay();
    queued.object = SavedObjectReference::fromRuntimeId(target.id());
    // An absent object is carried as no reference, so it never holds the event back.
    auto reference = [](uint32_t id) {
        return id == script::kObjectInvalid ? SavedObjectReference {} : SavedObjectReference::fromRuntimeId(id);
    };
    queued.caller = reference(caller ? caller->get().id() : script::kObjectInvalid);
    queued.eventId = static_cast<uint32_t>(SavedEventType::SignalEvent);
    SavedScriptEvent payload;
    payload.type = static_cast<uint16_t>(event.number());
    payload.integers = event.integers();
    payload.floats = event.floats();
    payload.strings = event.strings();
    for (uint32_t id : event.objects()) {
        payload.objects.push_back(reference(id));
    }
    queued.payload = std::move(payload);
    _module->enqueueSaveEvent(std::move(queued));
}

void Game::queueEffectApplication(Object &target, EffectInstance effect, uint32_t delayMilliseconds) {
    SavedEventRecord event;
    const uint64_t when = worldTimeMilliseconds() + delayMilliseconds;
    event.day = static_cast<uint32_t>(when / millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(when % millisecondsPerWorldDay());
    event.object = SavedObjectReference::fromRuntimeId(target.id());
    event.eventId = static_cast<uint32_t>(SavedEventType::ApplyEffect);
    event.payload = std::move(effect);
    const bool bound = bindSavedObjectReference(event.object);
    _module->enqueueBoundSaveEvent(std::move(event), bound);
}

std::optional<uint64_t> Game::delayedWorldTime(float seconds) const {
    // The delay is truncated to whole milliseconds and added to the time of
    // day as a day-0 delta. A negative delta, or one of a whole game day or
    // more, cannot be added, so nothing is posted. NaN and products outside
    // the int range truncate to a negative value.
    const float milliseconds = seconds * 1000.0f;
    if (!(milliseconds > -1.0f) ||
        !(static_cast<double>(milliseconds) < static_cast<double>(millisecondsPerWorldDay()))) {
        return std::nullopt;
    }
    return _worldTimeMilliseconds + static_cast<uint64_t>(static_cast<int64_t>(milliseconds));
}

void Game::postDelayedCommand(Object &owner, std::shared_ptr<Action> command, float seconds) {
    const auto when = delayedWorldTime(seconds);
    if (!when) {
        return;
    }
    SavedEventRecord event;
    event.day = static_cast<uint32_t>(*when / millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(*when % millisecondsPerWorldDay());
    event.object = SavedObjectReference::fromRuntimeId(owner.id());
    event.caller = SavedObjectReference::fromRuntimeId(owner.id());
    event.eventId = static_cast<uint32_t>(SavedEventType::Timed);
    _module->enqueueSaveEvent(std::move(event), std::move(command));
}

static constexpr float kMotionBlurRatio = 0.75f;

// While a minigame runs, its script owns the speed blur.
bool Game::isMiniGameActive() const {
    return _swoopRace.isActive() || _turret.isActive();
}

void Game::addMotionBlurProgram(bool onPlayer) {
    ++_motionBlurPrograms;
    if (!onPlayer || _motionBlurPrograms != 1 || isMiniGameActive()) {
        return;
    }
    setSpeedBlurRatio(kMotionBlurRatio);
    setSpeedBlur(true);
}

void Game::removeMotionBlurProgram() {
    _motionBlurPrograms = std::max(0, _motionBlurPrograms - 1);
    if (_motionBlurPrograms == 0 && !isMiniGameActive()) {
        setSpeedBlur(false);
    }
}

void Game::setSpeedBlur(bool enabled) {
    _services.scene.graphs.get(kSceneMain).setSpeedBlur(enabled);
}

void Game::setSpeedBlurRatio(float ratio) {
    _services.scene.graphs.get(kSceneMain).setSpeedBlurRatio(ratio);
}

// The fury overlays fx_fury01 to fx_fury03, by fury level.
static constexpr int kMaxFuryLevel = 3;

// A row outside the table lays nothing.
void Game::enableVideoEffect(int row) {
    disableVideoEffect();
    if (row < 0 || row >= static_cast<int>(_videoEffects.size())) {
        return;
    }
    const auto &video = _videoEffects[row];
    scene::VideoEffect effect;
    // The fury level picks its texture and 0 is no fury.
    const int furyLevel = std::clamp(video.fury, 0, kMaxFuryLevel);
    // A row enabling any of its parts is the current effect.
    if (video.scanNoise || video.saturation || video.clairvoyance || video.forceSight || video.fury != 0) {
        _videoEffectType = row;
    }
    if (video.scanNoise) {
        effect.scanNoise = _services.resource.textures.get("filmnoisetex", TextureUsage::MainTex);
    }
    effect.forceSight = video.forceSight;
    if (video.clairvoyance || furyLevel > 0) {
        effect.distortion = _services.resource.textures.get("distortiontex", TextureUsage::MainTex);
    }
    if (video.clairvoyance) {
        effect.clairvoyance = _services.resource.textures.get("fx_clair01", TextureUsage::MainTex);
    }
    if (furyLevel > 0) {
        effect.fury = _services.resource.textures.get("fx_fury0" + std::to_string(furyLevel), TextureUsage::MainTex);
    }
    if (video.saturation) {
        effect.saturation = true;
        effect.modulation = video.modulation;
        effect.saturationAmount = video.saturationAmount;
    }
    if (!effect.scanNoise && !effect.saturation && !effect.forceSight && !effect.clairvoyance && !effect.fury) {
        return;
    }
    _services.scene.graphs.get(kSceneMain).setVideoEffect(std::move(effect));
}

void Game::disableVideoEffect() {
    if (_videoEffectType == kNoVideoEffect) {
        return;
    }
    _videoEffectType = kNoVideoEffect;
    _services.scene.graphs.get(kSceneMain).setVideoEffect(std::nullopt);
}

// The videoeffects rows of the leader's Force Sight and of its three Fury levels.
static constexpr int kForceSightVideoEffect = 4;
static constexpr int kFirstFuryVideoEffect = 7;
static constexpr int kLastFuryVideoEffect = 9;

void Game::updateLeaderVideoEffect() {
    if (!isTSL()) {
        return;
    }
    const auto isLeaderEffectRow = [](int row) {
        return row == kForceSightVideoEffect || (row >= kFirstFuryVideoEffect && row <= kLastFuryVideoEffect);
    };
    if (isConversationActive()) {
        if (isLeaderEffectRow(_videoEffectType)) {
            disableVideoEffect();
        }
        return;
    }
    const auto leader = _party.getLeader();
    if (!leader) {
        return;
    }
    // Showing the row that is already the current effect changes nothing.
    const auto show = [this](int row) {
        if (_videoEffectType != row) {
            enableVideoEffect(row);
        }
    };
    bool fury = false;
    for (const auto &effect : leader->effects()) {
        if (effect.type() == EffectType::ForceSight) {
            show(kForceSightVideoEffect);
            return;
        }
        fury |= effect.type() == EffectType::Fury;
    }
    if (_services.scene.graphs.get(kSceneMain).isSpeedBlurEnabled()) {
        disableVideoEffect();
    } else if (fury) {
        show(leader->furySpellState());
    } else if (isLeaderEffectRow(_videoEffectType)) {
        disableVideoEffect();
    }
}

void Game::enableScriptVideoEffect(int row) {
    if (isTSL()) {
        _scriptVideoEffectHeld = true;
    }
    enableVideoEffect(row);
}

void Game::disableScriptVideoEffect() {
    if (isTSL()) {
        _scriptVideoEffectHeld = false;
    }
    disableVideoEffect();
}

void Game::postObjectDestruction(Object &target, std::optional<std::reference_wrapper<const Object>> caller, float seconds, bool keepsCallerFade) {
    const auto when = delayedWorldTime(seconds);
    if (!when) {
        return;
    }
    SavedEventRecord event;
    event.day = static_cast<uint32_t>(*when / millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(*when % millisecondsPerWorldDay());
    event.eventId = static_cast<uint32_t>(SavedEventType::DestroyObject);
    event.object = SavedObjectReference::fromRuntimeId(target.id());
    if (caller) {
        event.caller = SavedObjectReference::fromRuntimeId(caller->get().id());
    }
    _module->enqueueSaveEvent(std::move(event), nullptr, keepsCallerFade);
}

void Game::postObjectDestruction(Object &target, float delay) {
    const auto time = delayedWorldTime(delay);
    if (!time) {
        return;
    }
    SavedEventRecord event;
    event.day = static_cast<uint32_t>(*time / millisecondsPerWorldDay());
    event.time = static_cast<uint32_t>(*time % millisecondsPerWorldDay());
    event.eventId = static_cast<uint32_t>(SavedEventType::DestroyObject);
    event.object = SavedObjectReference::fromRuntimeId(target.id());
    const bool bound = bindSavedObjectReference(event.object);
    _module->enqueueBoundSaveEvent(std::move(event), bound);
}

void Game::queueEffectRemoval(Object &target, EffectId id) {
    SavedEventRecord event;
    event.day = worldTimeDay();
    event.time = worldTimeOfDay();
    event.object = SavedObjectReference::fromRuntimeId(target.id());
    event.eventId = static_cast<uint32_t>(SavedEventType::RemoveEffect);
    EffectInstance value;
    value.id = id;
    value.creatorId = kSavedEffectInvalidObjectId;
    event.payload = std::move(value);
    const bool bound = bindSavedObjectReference(event.object);
    _module->enqueueBoundSaveEvent(std::move(event), bound);
}

std::optional<float> Game::remainingEffectDuration(const EffectInstance &effect) const {
    if (effect.durationType() != DurationType::Temporary) {
        return std::nullopt;
    }
    if (effect.expiryOrigin == EffectExpiryOrigin::RuntimeCountdown ||
        effect.expiryOrigin == EffectExpiryOrigin::None)
        return effect.duration;
    const uint64_t expiry = static_cast<uint64_t>(effect.expiryDay) * millisecondsPerWorldDay() + effect.expiryTime;
    return expiry >= _worldTimeMilliseconds
               ? static_cast<float>(expiry - _worldTimeMilliseconds) / 1000.0f
               : -static_cast<float>(_worldTimeMilliseconds - expiry) / 1000.0f;
}

void Game::renderGUI() {
    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    bool gameplayHUD = _screen == Screen::InGame || _screen == Screen::SwoopRace || _screen == Screen::Turret;
    if (_screen == Screen::InGame) {
        if (_cameraType == CameraType::ThirdPerson) {
            renderHUD();
        }
    } else if (gameplayHUD) {
        if (auto gui = getScreenGUI()) {
            gui->render();
        }
    }
    renderGlobalFade();
    if (_screen == Screen::InGame && _cameraType == CameraType::ThirdPerson && _hud) {
        _hud->renderModal();
    }
    if (!gameplayHUD) {
        auto gui = getScreenGUI();
        if (gui) {
            gui->render();
        }
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->render();
    }
    // The credits cover everything, the fade included, and hide the pointer.
    if (_credits) {
        _credits->render();
    } else {
        _pointer->render(_options.graphics, relativeMouseMode());
    }
    renderDeveloperOverlay();
}

void Game::renderGlobalFade() {
    float alpha = _globalFade.opacity();
    if (alpha <= 0.0f) {
        return;
    }
    // Compatibility approximation: color, then blackfill.
    for (auto color : {_globalFade.color(), glm::vec3(0.0f)}) {
        renderScreenFill(color, alpha);
    }
}

void Game::renderScreenFill(const glm::vec3 &color, float alpha) {
    auto &graphics = _services.graphics;
    auto &context = graphics.context;
    GlobalUniforms previousGlobals;
    LocalUniforms previousLocals;
    graphics.uniforms.setGlobals([&](auto &globals) {
        previousGlobals = globals;
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(_options.graphics.width),
                                        static_cast<float>(_options.graphics.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    graphics.uniforms.setLocals([&](auto &locals) { previousLocals = locals; });
    context.withDepthTestMode(DepthTestMode::None, [&]() {
        context.withDepthMask(false, [&]() {
            context.withBlendMode(BlendMode::Normal, [&]() {
                context.withFaceCullMode(FaceCullMode::None, [&]() {
                    context.withPolygonMode(PolygonMode::Fill, [&]() {
                        // Inherit the GUI viewport, including window/drawable
                        // scaling. Logical GUI dimensions need not be pixels.
                        graphics.uniforms.setLocals([&](auto &locals) {
                            locals.reset();
                            locals.model = glm::scale(glm::mat4(1.0f),
                                                      glm::vec3(_options.graphics.width, _options.graphics.height, 1.0f));
                            locals.color = glm::vec4(color, alpha);
                        });
                        context.useProgram(graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
                        graphics.meshRegistry.get(MeshName::quad).draw(graphics.statistic);
                    });
                });
            });
        });
    });
    graphics.uniforms.setLocals([&](auto &locals) { locals = previousLocals; });
    graphics.uniforms.setGlobals([&](auto &globals) { globals = previousGlobals; });
}

void Game::settleFadeArrival() {
    if (!_fadeArrival) {
        return;
    }
    if (_fadeArrivalModule.lock() == _module && _module) {
        // The area's music and ambient loop start on arrival, after the enter
        // script and any movie it plays. Menu music keeps them suspended.
        if (_musicResRef.empty()) {
            _areaMusic.start();
        }
        stopLoadMusic();
        _globalFade.settleArrival(_fadeArrival);
    }
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
}

void Game::renderDeveloperOverlay() {
    if (!_options.game.developer || !_developerOverlay.visible || !_module || _screen != Screen::InGame) {
        return;
    }
    if (!_developerFont) {
        _developerFont = _services.resource.fonts.get("fnt_console");
    }
    if (!_developerFont) {
        return;
    }

    auto camera = getActiveCamera();
    bool hasCamera = camera != nullptr;
    glm::mat4 projection(1.0f);
    glm::mat4 view(1.0f);
    if (camera) {
        projection = camera->cameraSceneNode()->camera()->projection();
        view = camera->cameraSceneNode()->camera()->view();
    }

    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this]() {
        renderDeveloperBanner();
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this, hasCamera, &projection, &view]() {
        if (_developerOverlay.triggers && hasCamera) {
            renderDeveloperTriggerOverlay(projection, view);
        }
        if (_developerOverlay.actorLabels && hasCamera) {
            renderDeveloperActorLabels(projection, view);
        }
        if (_developerOverlay.watchedValues) {
            renderDeveloperWatchedValues();
        }
    });
}

void Game::renderDeveloperBanner() {
    std::vector<std::string> lines;
    lines.push_back("DEV OBSERVABILITY");
    lines.push_back(str(boost::format("%s overlay | %s triggers") %
                        kDeveloperOverlayToggleHelp %
                        kDeveloperTriggerToggleHelp));
    lines.push_back(str(boost::format("%s labels (%s) | %s verbose") %
                        kDeveloperActorToggleHelp %
                        (_developerOverlay.longActorLabels ? "long" : "short") %
                        kDeveloperActorLongToggleHelp));
    lines.push_back(str(boost::format("%s watch | ` console | F5 profiler") %
                        kDeveloperWatchToggleHelp));
    lines.push_back("V camera | +/- speed");

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    renderDeveloperPanel(
        lines,
        glm::vec2(0.5f * (static_cast<float>(_options.graphics.width) - maxWidth - 14.0f), 12.0f),
        glm::vec3(0.58f, 1.0f, 0.58f));
}

void Game::renderDeveloperTriggerOverlay(const glm::mat4 &projection, const glm::mat4 &view) {
    static glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
    auto area = _module ? _module->area() : nullptr;
    if (!area) {
        return;
    }

    const auto &opts = _options.graphics;
    for (const auto &object : area->getObjectsByType(ObjectType::Trigger)) {
        auto trigger = dyn_cast<Trigger>(object);
        if (!trigger) {
            continue;
        }
        const auto &geometry = trigger->geometry();
        if (geometry.empty()) {
            continue;
        }

        glm::vec3 centroid(0.0f);
        for (const auto &localPoint : geometry) {
            centroid += trigger->position() + localPoint;
        }

        // Trigger geometry now renders through the main scene pipeline; the overlay only adds labels.
        auto state = trigger->debugState();
        glm::vec4 color = trigger->debugColor();
        centroid /= static_cast<float>(geometry.size());
        glm::vec3 labelScreen = glm::project(centroid, view, projection, viewport);
        if (labelScreen.z >= 0.0f && labelScreen.z < 1.0f) {
            std::string label = str(boost::format("#%u %s") %
                                    trigger->id() %
                                    trigger->tag());
            if (!trigger->blueprintResRef().empty()) {
                label += " " + trigger->blueprintResRef();
            }
            label += str(boost::format(" [%s]") % triggerDebugStateName(state));
            glm::vec3 position(opts.width * labelScreen.x, opts.height * (1.0f - labelScreen.y), 0.0f);
            renderDeveloperText(label, position, glm::vec3(color), TextGravity::CenterBottom);
        }
    }
}

void Game::renderDeveloperActorLabels(const glm::mat4 &projection, const glm::mat4 &view) {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    if (!area || !leader) {
        return;
    }

    const auto &opts = _options.graphics;
    int rendered = 0;
    for (const auto &object : area->objects()) {
        bool supported = object->type() == ObjectType::Creature ||
                         object->type() == ObjectType::Door ||
                         object->type() == ObjectType::Placeable;
        bool inspected = object == area->hilightedObject() || object == area->selectedObject();
        if (!supported && !inspected) {
            continue;
        }

        float distance = object->getDistanceTo(*leader);
        if (!inspected && distance > kDeveloperActorLabelDistance) {
            continue;
        }

        glm::vec3 screen = area->getSelectableScreenCoords(object, projection, view);
        if (screen.z >= 1.0f) {
            continue;
        }

        int faction = getDebugFaction(object);
        bool hostile = false;
        auto creature = dyn_cast<Creature>(object);
        if (creature) {
            hostile = !creature->isDead() && _services.game.reputes.getIsEnemy(*creature, *leader);
        }

        glm::vec3 color = inspected ? glm::vec3(1.0f, 1.0f, 1.0f) : (hostile ? glm::vec3(1.0f, 0.42f, 0.36f) : glm::vec3(0.68f, 0.92f, 1.0f));
        std::string label;
        if (_developerOverlay.longActorLabels) {
            label = str(boost::format("#%u %s %s f=%d H=%d sel=%d cmd=%d vis=%d plot=%d") %
                        object->id() %
                        object->tag() %
                        object->blueprintResRef() %
                        faction %
                        static_cast<int>(hostile) %
                        static_cast<int>(object->isSelectable()) %
                        static_cast<int>(object->isCommandable()) %
                        static_cast<int>(object->visible()) %
                        static_cast<int>(object->plotFlag()));
        } else {
            label = str(boost::format("#%u %s") %
                        object->id() %
                        object->tag());
            if (!object->blueprintResRef().empty()) {
                label += " " + object->blueprintResRef();
            }
            if (hostile) {
                label += " [enemy]";
            }
            if (inspected) {
                label += str(boost::format(" [%s") % objectTypeName(object->type()));
                if (faction >= 0) {
                    label += str(boost::format(" f=%d") % faction);
                }
                label += "]";
            }
        }

        glm::vec3 position(opts.width * screen.x, opts.height * (1.0f - screen.y) - 18.0f - (rendered % 2) * 10.0f, 0.0f);
        renderDeveloperText(label, position, color, TextGravity::CenterBottom);
        if (++rendered >= 16) {
            break;
        }
    }
}

void Game::renderDeveloperWatchedValues() {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    auto selected = area ? area->selectedObject() : nullptr;
    auto hover = area ? area->hilightedObject() : nullptr;
    std::string room = leader && leader->room() ? leader->room()->name() : "-";
    glm::vec3 position = leader ? leader->position() : glm::vec3(0.0f);

    std::vector<std::string> lines;
    lines.push_back(str(boost::format("Watch (%s)") % kDeveloperWatchToggleHelp));
    lines.push_back(str(boost::format("screen=%s module=%s area=%s camera=%s") %
                        screenName(_screen) %
                        (_module ? _module->name() : "-") %
                        (area ? area->localizedName() : "-") %
                        cameraTypeName(_cameraType)));
    lines.push_back(str(boost::format("speed=%.1fx paused=%d relativeMouse=%d room=%s") %
                        _gameSpeed %
                        static_cast<int>(_paused) %
                        static_cast<int>(_relativeMouseMode) %
                        room));
    lines.push_back(str(boost::format("leader=#%u %s hp=%d/%d pos=%.2f,%.2f,%.2f") %
                        (leader ? leader->id() : 0) %
                        (leader ? leader->tag() : "-") %
                        (leader ? leader->currentHitPoints() : -1) %
                        (leader ? leader->maxHitPoints() : -1) %
                        position.x %
                        position.y %
                        position.z));
    lines.push_back(str(boost::format("selected=#%u %s/%s type=%s hp=%d/%d") %
                        (selected ? selected->id() : 0) %
                        (selected ? selected->tag() : "-") %
                        (selected ? selected->blueprintResRef() : "-") %
                        (selected ? objectTypeName(selected->type()) : "-") %
                        (selected ? selected->currentHitPoints() : -1) %
                        (selected ? selected->maxHitPoints() : -1)));
    lines.push_back(str(boost::format("hover=#%u %s/%s type=%s hp=%d/%d") %
                        (hover ? hover->id() : 0) %
                        (hover ? hover->tag() : "-") %
                        (hover ? hover->blueprintResRef() : "-") %
                        (hover ? objectTypeName(hover->type()) : "-") %
                        (hover ? hover->currentHitPoints() : -1) %
                        (hover ? hover->maxHitPoints() : -1)));

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float panelWidth = maxWidth + 14.0f;
    float panelHeight = (_developerFont->height() + 2.0f) * static_cast<float>(lines.size()) + 10.0f;
    renderDeveloperPanel(
        lines,
        glm::vec2(static_cast<float>(_options.graphics.width) - panelWidth - 4.0f, 16.0f + panelHeight),
        glm::vec3(0.92f));
}

void Game::renderDeveloperText(const std::string &text, const glm::vec3 &position, const glm::vec3 &color, TextGravity gravity) {
    if (!_developerFont) {
        return;
    }
    _developerFont->render(text, position + glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f), gravity);
    _developerFont->render(text, position, color, gravity);
}

void Game::renderDeveloperPanel(const std::vector<std::string> &lines, glm::vec2 position, glm::vec3 color) {
    if (!_developerFont || lines.empty()) {
        return;
    }

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float lineHeight = _developerFont->height() + 2.0f;
    glm::vec2 size(maxWidth + 14.0f, lineHeight * lines.size() + 10.0f);
    position.x = glm::clamp(position.x, 4.0f, static_cast<float>(_options.graphics.width) - size.x - 4.0f);
    position.y = glm::clamp(position.y, 4.0f, static_cast<float>(_options.graphics.height) - size.y - 4.0f);

    renderDeveloperRect(position, size, glm::vec4(0.0f, 0.0f, 0.0f, 0.58f));
    glm::vec3 textPosition(position.x + 7.0f, position.y + 5.0f, 0.0f);
    for (const auto &line : lines) {
        renderDeveloperText(line, textPosition, color, TextGravity::RightBottom);
        textPosition.y += lineHeight;
    }
}

void Game::renderDeveloperRect(glm::vec2 position, glm::vec2 size, glm::vec4 color) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(position.x, position.y, 0.0f));
    transform = glm::scale(transform, glm::vec3(size.x, size.y, 1.0f));

    _services.graphics.uniforms.setLocals([transform, color](auto &locals) {
        locals.reset();
        locals.model = transform;
        locals.color = color;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Game::updateMusic() {
    _areaMusic.update();
    if (_musicResRef.empty()) {
        return;
    }
    if (_music && _music->isPlaying()) {
        return;
    }
    auto clip = _services.resource.audioClips.get(_musicResRef);
    _music = _services.audio.mixer.play(std::move(clip), AudioType::Music, _areaMusic.musicGroupGain());
}

void Game::loadNextModule() {
    std::string target(_nextModule);

    // Capture the origin (current module + leader location) before the deferred
    // transition runs, so a swoop module entered from a script can return here.
    std::string originModule;
    glm::vec3 originPosition(0.0f);
    float originFacing = 0.0f;
    bool haveOrigin = false;
    if (_module) {
        originModule = _module->name();
        if (auto leader = _party.getLeader()) {
            originPosition = leader->position();
            originFacing = leader->getFacing();
            haveOrigin = true;
        }
    }
    bool wasLifecycleActive = _swoopLifecycle.active || _turretLifecycle.active;

    bool loaded = loadModule(_nextModule, _nextEntry);

    _nextModule.clear();
    _nextEntry.clear();
    if (!loaded) {
        return;
    }

    // A transition into an area declaring a minigame enters its minigame session
    // automatically. A session scheduled by startturretgame carries the origin
    // captured before the transition was queued.
    bool pendingTurret = _pendingTurret.active && boost::iequals(_pendingTurret.targetModule, target);
    if (pendingTurret) {
        originModule = _pendingTurret.originModule;
        originPosition = _pendingTurret.originPosition;
        originFacing = _pendingTurret.originFacing;
        haveOrigin = _pendingTurret.haveOrigin;
    }

    if (wasLifecycleActive || _swoopLifecycle.active || _turretLifecycle.active) {
        return;
    }
    if (originModule.empty() || boost::iequals(originModule, target)) {
        if (pendingTurret) {
            abandonPendingTurret("no origin module");
        }
        return;
    }
    auto mod = _module;
    auto area = mod ? mod->area() : nullptr;
    bool hasMinigame = area && area->hasMinigame();
    MinigameType minigameType = hasMinigame ? area->miniGame().type : MinigameType::None;

    auto resolution = resolveTurretRequest(pendingTurret, hasMinigame, minigameType);
    if (resolution == TurretRequestResolution::AbortNoMinigame ||
        resolution == TurretRequestResolution::AbortWrongType) {
        abandonPendingTurret(turretRequestResolutionMessage(resolution));
        return;
    }

    if (!hasMinigame) {
        return;
    }
    if (minigameType == MinigameType::Turret) {
        openTurret();
        if (_turret.isActive()) {
            _turretLifecycle = MinigameLifecycle();
            _turretLifecycle.active = true;
            _turretLifecycle.haveOrigin = haveOrigin;
            _turretLifecycle.originModule = originModule;
            _turretLifecycle.originPosition = originPosition;
            _turretLifecycle.originFacing = originFacing;
            _turretLifecycle.forcedSuccess = true;
            _pendingTurret = PendingTurretRequest();
            debug(str(boost::format("turret: lifecycle start origin=%s target=%s hook=%s")
                      % originModule % target
                      % (pendingTurret ? "startturretgame" : "StartNewModule")));
        } else if (pendingTurret) {
            abandonPendingTurret("turret failed to start");
        }
        return;
    }
    if (minigameType != MinigameType::SwoopRace) {
        return;
    }

    openSwoopRace();
    if (_swoopRace.isActive()) {
        _swoopLifecycle = MinigameLifecycle();
        _swoopLifecycle.active = true;
        _swoopLifecycle.haveOrigin = haveOrigin;
        _swoopLifecycle.originModule = originModule;
        _swoopLifecycle.originPosition = originPosition;
        _swoopLifecycle.originFacing = originFacing;
        _swoopLifecycle.forcedSuccess = true;
        debug(str(boost::format("swoop: script lifecycle start origin=%s target=%s forcedSuccess=yes hook=StartNewModule")
                  % originModule % target));
    }
}

void Game::stopMovement() {
    // Reached with no module while one is being swapped in: loadGame resets the
    // game before the destination module is up, and the menus that call this
    // outlive that reset. There is no player to halt and no in-game camera for
    // getActiveCamera to find, so there is nothing to stop.
    if (!_module) {
        return;
    }

    auto camera = getActiveCamera();
    if (camera) {
        camera->stopMovement();
    }
    _module->player().stopMovement();
}

void Game::scheduleModuleTransition(const std::string &moduleName, const std::string &entry) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
}

void Game::scheduleModuleTransitionWithMovies(const std::string &moduleName, const std::string &entry, std::vector<std::string> movies) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
    for (auto &movie : movies) {
        _moduleTransitionMovies.push(std::move(movie));
    }

    if (!_movie) {
        playNextModuleTransitionMovie();
    }
}

bool Game::startVideo(const std::string &name, bool streamingMusic) {
    _movie = _services.resource.movies.get(name);
    _timingDiscontinuity = true;
    if (!_movie) {
        return false;
    }
    // A movie ends the area's music and ambient loop for good; one that
    // comes before arrival, as an enter script's, leaves the music to start
    // on arrival but no ambient loop. Every other sound waits for it, including
    // sounds started before it is first shown; a movie that lets music play
    // leaves area music set to play through movies sounding while it fades.
    _areaMusic.stopSoundsForMovie();
    _services.audio.mixer.setSoundsHeldForMovie(true, streamingMusic ? _areaMusic.musicPlayingThroughMovies() : nullptr);
    return true;
}

bool Game::playNextModuleTransitionMovie() {
    while (!_moduleTransitionMovies.empty()) {
        auto name = std::move(_moduleTransitionMovies.front());
        _moduleTransitionMovies.pop();

        if (startVideo(name, false)) {
            // A transition list holds only movies that exist, and its load
            // plays no loading-screen music.
            if (!_nextModule.empty()) {
                _loadComesWithMovies = true;
            }
            return true;
        }
    }
    return false;
}

void Game::updateMovie(float dt) {
    _movie->update(dt);

    if (_movie->isFinished()) {
        _movie.reset();
        _timingDiscontinuity = true;
        playNextModuleTransitionMovie();
    }
}

static constexpr float kThirdPersonListenerShare = 0.75f;

void Game::updateCamera(float dt) {
    // The world cameras belong to a module's area: the main menu and character
    // generation have none.
    if (!_module) {
        return;
    }
    auto &area = *_module->area();
    updateFreeLookExits();
    // Client combat mode holds the third-person camera in its combat behavior.
    area.setThirdPersonCombat(_clientCombatMode);
    // The combat camera follows the leader's posed head every frame.
    if (_clientCombatMode) {
        area.update3rdPersonCameraTarget();
    }
    auto &thirdPerson = *area.getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
    switch (_screen) {
    case Screen::Conversation: {
        int cameraId;
        CameraType cameraType = getConversationCamera(cameraId);
        if (cameraType == CameraType::Static) {
            area.setStaticCamera(cameraId);
        }
        _cameraType = cameraType;
        break;
    }
    case Screen::InGame:
        if (_cameraType != CameraType::FirstPerson && _cameraType != CameraType::ThirdPerson) {
            _cameraType = CameraType::ThirdPerson;
            // Returning to play during combat aims the camera from the leader
            // toward the last target.
            auto leader = _party.getLeader();
            auto target = _lastTarget.resolve();
            // The combat camera starts turning from rest again.
            if (_clientCombatMode) {
                thirdPerson.resetCombatTurn();
            }
            if (_clientCombatMode && leader && target) {
                thirdPerson.faceAlong(leader->position(), target->position());
            }
        }
        break;
    default:
        break;
    }
    Camera *camera = getActiveCamera();
    if (camera) {
        camera->update(dt);

        // Behind the leader the listener sits three quarters of the way from
        // the camera to the point it follows, the leader's camera hook; any
        // other view, the death orbit included, listens from the camera.
        glm::vec3 listenerPosition {camera->sceneNode()->origin()};
        if (_cameraType == CameraType::ThirdPerson && !thirdPerson.isDeathOrbit() && _party.getLeader()) {
            listenerPosition += kThirdPersonListenerShare * (thirdPerson.targetPosition() - listenerPosition);
        }
        _services.audio.context.setListenerPosition(std::move(listenerPosition));
    }
}

void Game::updateSceneGraph(float dt) {
    auto camera = getActiveCamera();
    if (!camera) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    sceneGraph.setActiveCamera(camera->cameraSceneNode().get());
    sceneGraph.setUpdateRoots(!_paused && !_timeStopped);
    // While time is stopped only the excluded objects' models animate.
    if (!_paused && _timeStopped) {
        for (const auto &excluded : _timeStopExclusions) {
            auto object = excluded.resolve();
            auto model = object ? std::dynamic_pointer_cast<ModelSceneNode>(object->sceneNode()) : nullptr;
            if (model) {
                model->update(dt);
            }
        }
    }
    sceneGraph.setRenderAABB(isShowAABBEnabled());
    sceneGraph.setRenderWalkmeshes(isShowWalkmeshEnabled());
    bool renderDeveloperTriggers = _options.game.developer &&
                                   _screen == Screen::InGame &&
                                   _developerOverlay.visible &&
                                   _developerOverlay.triggers;
    sceneGraph.setRenderTriggers(isShowTriggersEnabled() || renderDeveloperTriggers);
    sceneGraph.update(dt);
}

bool Game::getGlobalBoolean(const std::string &name) const {
    auto it = _globalBooleans.find(name);
    return it != _globalBooleans.end() ? it->second : false;
}

int Game::getGlobalNumber(const std::string &name) const {
    auto it = _globalNumbers.find(name);
    return it != _globalNumbers.end() ? it->second : 0;
}

std::string Game::getGlobalString(const std::string &name) const {
    auto it = _globalStrings.find(name);
    return it != _globalStrings.end() ? it->second : "";
}

std::shared_ptr<Location> Game::getGlobalLocation(const std::string &name) const {
    auto it = _globalLocations.find(name);
    return it != _globalLocations.end() ? it->second : nullptr;
}

void Game::setCustomToken(int token, std::string value) {
    _customTokens[token] = std::move(value);
}

static constexpr char kUnrecognizedToken[] = "<UNRECOGNIZED TOKEN>";

// The calendar the date and time tokens read: years of 12 months of 28 days,
// days of 24 hours. A year beyond the last one the calendar counts reads 1340.
static constexpr uint32_t kCalendarDaysInMonth = 28;
static constexpr uint32_t kCalendarMonthsInYear = 12;
static constexpr uint32_t kCalendarHoursInDay = 24;
static constexpr uint32_t kCalendarLastYear = 32767;
static constexpr uint32_t kCalendarYearPastLast = 1340;

// The good-evil forms of a token: evil below 41, neutral below 60, good from 60.
static constexpr int kLowestNeutralGoodEvil = 41;
static constexpr int kLowestGoodGoodEvil = 60;

// What a token of stringtokens.2da stands for, by its actioncode.
enum class StringTokenAction {
    Deity = 0,
    GenderForm = 1,
    FullName = 2,
    FirstName = 3,
    LastName = 4,
    Race = 5,
    RaceLower = 6,
    Subrace = 7,
    Class = 8,
    ClassLower = 9,
    Level = 10,
    Alignment = 11,
    AlignmentLower = 12,
    LawChaos = 13,
    GoodEvil = 14,
    GameYear = 15,
    GameTime = 16,
    GameMonth = 17,
    PartOfDay = 18,
    StartAction = 19,
    StartCheck = 20,
    StartHighlight = 21,
    EndStart = 22,
    PlayerName = 23,
    Button = 24,
    KeyName = 25,
    MouseButton = 26,
    Bounty = 27
};

// The mouse-button texts a MouseButton token picks by its default value.
static constexpr std::array<int, 4> kMouseButtonStrRefsK1 {48483, 48482, 48485, 48484};
static constexpr std::array<int, 4> kMouseButtonStrRefsTSL {48726, 48725, 48728, 48727};
// The bounty token reads this global number and names that many times this
// amount of credits.
static constexpr char kBountyGlobal[] = "300NAR_Bounty";
static constexpr int kBountyCreditsPerPoint = 500;

// Keys are numbered from the mouse's, then the keyboard's from Return, in
// bindablekeys.2da order, then the controller's. Key 0 names nothing.
static constexpr int kFirstKeyboardKey = 6;
static constexpr int kFirstControllerKey = 110;
// What a key outside the keyboard reads, and a keyboard key without a name.
static constexpr int kNonKeyboardKeyStrRefK1 = 48144;
static constexpr int kNonKeyboardKeyStrRefTSL = 48724;
static constexpr char kUnnamedKey[] = "N/A";
// Single-key input events in this range take their keys from keymap.2da rows
// "action<event>", two-key ones from rows "action<event>a" and "b".
static constexpr int kFirstMappedInputEvent = 200;
static constexpr int kLastMappedInputEventK1 = 265;
static constexpr int kLastMappedInputEventTSL = 269;
static constexpr int kFirstTwoKeyInputEvent = 280;
static constexpr int kLastTwoKeyInputEvent = 286;

/** An input event the engine defines itself, and the key it takes. */
struct EngineInputEvent {
    int event {0};
    int key {0};
};

static constexpr std::array<EngineInputEvent, 23> kEngineInputEventsK1 {{
    {0x03, 110},
    {0x04, 111},
    {0x07, 111},
    {0x08, 110},
    {0x0b, 124},
    {0x0c, 113},
    {0x0d, 114},
    {0x17, 3},
    {0x18, 4},
    {0x19, 0},
    {0x41, 3},
    {0x42, 4},
    {0x43, 0},
    {0x44, 1},
    {0x45, 2},
    {0x46, 5},
    {0x50, 38},
    {0xb5, 6},
    {0xb6, 9},
    {0xb7, 10},
    {0xb8, 7},
    {0xb9, 8},
    {0xbb, 88},
}};
static constexpr std::array<EngineInputEvent, 29> kEngineInputEventsTSL {{
    {0x01, 125},
    {0x03, 110},
    {0x04, 111},
    {0x07, 111},
    {0x08, 110},
    {0x0b, 122},
    {0x0c, 113},
    {0x0d, 114},
    {0x17, 3},
    {0x18, 4},
    {0x19, 0},
    {0x1a, 1},
    {0x33, 111},
    {0x34, 110},
    {0x37, 117},
    {0x38, 116},
    {0x41, 3},
    {0x42, 4},
    {0x43, 0},
    {0x44, 1},
    {0x45, 2},
    {0x46, 5},
    {0x50, 38},
    {0xb5, 6},
    {0xb6, 9},
    {0xb7, 10},
    {0xb8, 7},
    {0xb9, 8},
    {0xbb, 88},
}};

void Game::initInputEvents() {
    _inputEvents.clear();
    const auto addEngineEvents = [this](const auto &events) {
        for (const EngineInputEvent &engineEvent : events) {
            _inputEvents[engineEvent.event] = InputEventKeys {engineEvent.key, std::nullopt};
        }
    };
    if (isTSL()) {
        addEngineEvents(kEngineInputEventsTSL);
    } else {
        addEngineEvents(kEngineInputEventsK1);
    }

    auto keymap = getRequiredTwoDA(_services.resource.twoDas, "keymap");
    const int lastMappedEvent = isTSL() ? kLastMappedInputEventTSL : kLastMappedInputEventK1;
    const auto rowOf = [&keymap](int event, const std::string &suffix) {
        return keymap->indexByLabel("action" + std::to_string(event) + suffix);
    };
    // A disabled row makes no event.
    const auto enabled = [&keymap](int row) {
        return row != -1 && keymap->getInt(row, "disabled", 0) != 1;
    };
    // Keys are read from the column of the talk table's language when an
    // enabled single-key row gives a key there, else from the first
    // language's.
    std::string column = "language" + std::to_string(_services.resource.strings.languageId());
    bool languageHasKeys = false;
    for (int event = kFirstMappedInputEvent; event <= lastMappedEvent && !languageHasKeys; ++event) {
        const int row = rowOf(event, "");
        languageHasKeys = enabled(row) && keymap->getInt(row, column, -1) >= 0;
    }
    if (!languageHasKeys) {
        column = "language0";
    }
    // A single-key row makes an event only when it gives a key that is not
    // negative.
    for (int event = kFirstMappedInputEvent; event <= lastMappedEvent; ++event) {
        const int row = rowOf(event, "");
        const int key = enabled(row) ? keymap->getInt(row, column, -1) : -1;
        if (key >= 0) {
            _inputEvents[event] = InputEventKeys {key, std::nullopt};
        }
    }
    // A two-key event is made from its A and B rows when both give a key that
    // is not negative and the two differ, or are both 0: the B key is its key
    // and the A key its alternate. Only the A row can disable it.
    for (int event = kFirstTwoKeyInputEvent; event <= kLastTwoKeyInputEvent; ++event) {
        const int rowA = rowOf(event, "a");
        const int rowB = rowOf(event, "b");
        if (!enabled(rowA) || rowB == -1) {
            continue;
        }
        const int keyA = keymap->getInt(rowA, column, -1);
        const int keyB = keymap->getInt(rowB, column, -1);
        if (keyA < 0 || keyB < 0 || (keyA == keyB && keyA != 0)) {
            continue;
        }
        _inputEvents[event] = InputEventKeys {keyB, keyA};
    }

    auto keys = getRequiredTwoDA(_services.resource.twoDas, "bindablekeys");
    _keyNameStrRefs.clear();
    for (int row = 0; row < keys->getRowCount(); ++row) {
        _keyNameStrRefs.push_back(keys->getInt(row, "keynamestrref", -1));
    }
}

// Text between "<" and ">" names a token; "<<" and "{{" stand for "<" and
// "{". Text between "{" and "}" is dropped, as is text that a StartAction or
// StartCheck token hides; token values are kept either way. A string with no
// token or note is returned as it is.
std::string Game::parseTokens(const std::string &text, const std::map<int, std::string> &customTokens,
                              std::optional<std::reference_wrapper<const Creature>> subject, bool hideActions,
                              bool &hidden) const {
    hidden = false;
    if (text.find_first_of("<{") == std::string::npos) {
        return text;
    }
    std::string result;
    size_t start = 0;
    size_t i = 0;
    const auto appendLiteral = [&](size_t end) {
        if (!hidden) {
            result.append(text, start, end - start);
        }
    };
    while (i < text.size()) {
        const char c = text[i];
        if (c != '<' && c != '{') {
            ++i;
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == c) {
            appendLiteral(i + 1);
            i += 2;
            start = i;
            continue;
        }
        appendLiteral(i);
        const size_t close = text.find(c == '<' ? '>' : '}', i + 1);
        if (c == '<') {
            if (close == std::string::npos) {
                result += kUnrecognizedToken;
            } else {
                result += getTokenValue(text.substr(i + 1, close - i - 1), customTokens, subject, hideActions,
                                        hidden);
            }
        }
        i = close == std::string::npos ? text.size() : close + 1;
        start = i;
    }
    appendLiteral(text.size());
    return result;
}

std::string Game::getTokenValue(const std::string &name, const std::map<int, std::string> &customTokens,
                                std::optional<std::reference_wrapper<const Creature>> subject, bool hideActions,
                                bool &hidden) const {
    static constexpr char kCustomTokenPrefix[] = "CUSTOM";
    static constexpr size_t kCustomTokenPrefixLength = 6;
    if (name.size() > kCustomTokenPrefixLength && name.compare(0, kCustomTokenPrefixLength, kCustomTokenPrefix) == 0) {
        uint32_t token = 0;
        for (size_t i = kCustomTokenPrefixLength; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                return kUnrecognizedToken;
            }
            token = token * 10 + static_cast<uint32_t>(name[i] - '0');
        }
        auto it = customTokens.find(static_cast<int>(token));
        return it != customTokens.end() ? it->second : kUnrecognizedToken;
    }

    const auto found = _stringTokens.find(name);
    if (found == _stringTokens.end()) {
        return kUnrecognizedToken;
    }
    const auto &token = found->second;
    auto &strings = _services.resource.strings;
    const auto strRef = [&](int index) { return token.strRefs[index]; };
    const int defaultValue = token.defaultStrRef;
    // Interface text is itself parsed, on the same hiding state.
    const auto guiText = [&](int textStrRef) {
        return parseTokens(strings.getText(textStrRef), customTokens, subject, hideActions, hidden);
    };
    const auto action = static_cast<StringTokenAction>(token.action);
    // TSL also asks for a subject before naming the player.
    const bool needsSubject = (action >= StringTokenAction::FullName && action <= StringTokenAction::AlignmentLower) ||
                              action == StringTokenAction::GoodEvil ||
                              (isTSL() && action == StringTokenAction::PlayerName);
    std::string value;
    if (needsSubject && !subject) {
        value = guiText(defaultValue);
    } else {
        const std::string pcName = _party.playerCharacterName();
        const size_t space = pcName.find(' ');
        switch (action) {
        case StringTokenAction::Deity:
            value = guiText(strRef(0));
            break;
        case StringTokenAction::GenderForm:
            // Only a female creature takes the second form.
            value = guiText(strRef(subject && subject->get().gender() == Gender::Female ? 1 : 0));
            break;
        case StringTokenAction::FullName:
            // TSL names the player character; K1 names the subject.
            value = isTSL() ? pcName : subject->get().name();
            break;
        case StringTokenAction::PlayerName:
            value = pcName;
            break;
        case StringTokenAction::FirstName:
            value = !isTSL() ? subject->get().firstName() : space == std::string::npos ? pcName
                                                                                       : pcName.substr(0, space);
            break;
        case StringTokenAction::LastName:
            if (!isTSL()) {
                value = subject->get().lastName();
            } else {
                // A name without a second word is its own last name.
                const std::string last = space == std::string::npos ? std::string() : pcName.substr(space + 1);
                value = last.empty() ? pcName : last;
            }
            break;
        case StringTokenAction::Race:
        case StringTokenAction::RaceLower: {
            const Race &race = subject->get().race();
            value = strings.getText(action == StringTokenAction::Race ? race.converName : race.converNameLower);
            break;
        }
        case StringTokenAction::Subrace:
            // A creature without subrace text reads its race's name.
            value = strings.getText(subject->get().race().name);
            break;
        case StringTokenAction::Class:
        case StringTokenAction::ClassLower:
        case StringTokenAction::Level: {
            // The first class is the one named; a creature without one has none to name.
            const auto &classLevels = subject->get().attributes().classLevels();
            if (classLevels.empty()) {
                break;
            }
            const auto &[firstClass, firstClassLevel] = classLevels.front();
            if (action == StringTokenAction::Level) {
                value = std::to_string(firstClassLevel);
            } else if (action == StringTokenAction::Class) {
                value = firstClass->name();
            } else {
                value = strings.getText(firstClass->lowerNameStrRef());
            }
            break;
        }
        case StringTokenAction::Alignment:
        case StringTokenAction::AlignmentLower:
            value = strings.getText(0);
            break;
        case StringTokenAction::GoodEvil:
            value = guiText(strRef(subject->get().goodEvil() < kLowestNeutralGoodEvil ? 2
                                   : subject->get().goodEvil() < kLowestGoodGoodEvil  ? 1
                                                                                      : 0));
            break;
        case StringTokenAction::GameYear: {
            const uint32_t year = worldTimeDay() / (kCalendarDaysInMonth * kCalendarMonthsInYear);
            value = std::to_string(year > kCalendarLastYear ? kCalendarYearPastLast : year);
            break;
        }
        case StringTokenAction::GameTime:
            // A 12-hour clock: midnight and noon read 12.
            value = std::to_string((worldTimeHour() + kCalendarHoursInDay / 2 - 1) % (kCalendarHoursInDay / 2) + 1);
            break;
        case StringTokenAction::GameMonth:
            value = std::to_string(worldTimeDay() / kCalendarDaysInMonth % kCalendarMonthsInYear + 1);
            break;
        case StringTokenAction::PartOfDay: {
            // The hours before dawn and after dusk take the first form; the
            // hours from dawn to dusk split evenly into the other three.
            const auto &info = _module->info();
            const int hour = static_cast<int>(worldTimeHour());
            int part = 0;
            if (hour >= info.dawnHour && hour <= info.duskHour) {
                part = 3 * (hour - info.dawnHour) / (info.duskHour - info.dawnHour + 1) + 1;
            }
            value = guiText(strRef(part));
            break;
        }
        case StringTokenAction::StartAction:
        case StringTokenAction::StartCheck:
            hidden = hideActions;
            break;
        case StringTokenAction::StartHighlight:
            break;
        case StringTokenAction::EndStart:
            hidden = false;
            break;
        case StringTokenAction::Button:
            value = std::string(1, static_cast<char>(defaultValue));
            break;
        case StringTokenAction::KeyName: {
            // The name of the key bound to the input event the default names;
            // a two-key event names its alternate key when strref1 is 2 or
            // more. Without an event or a key it reads string 0. A keyboard
            // key is named by its bindablekeys.2da text.
            value = strings.getText(0);
            const auto event = _inputEvents.find(defaultValue);
            if (event == _inputEvents.end()) {
                break;
            }
            const auto &eventKeys = event->second;
            const int key = eventKeys.alternateKey && strRef(0) >= 2 ? *eventKeys.alternateKey : eventKeys.key;
            if (key == 0) {
                break;
            }
            if (key < kFirstKeyboardKey || key >= kFirstControllerKey) {
                value = strings.getText(isTSL() ? kNonKeyboardKeyStrRefTSL : kNonKeyboardKeyStrRefK1);
                break;
            }
            const size_t keyRow = key - kFirstKeyboardKey;
            value = keyRow < _keyNameStrRefs.size() ? strings.getText(_keyNameStrRefs[keyRow]) : kUnnamedKey;
            break;
        }
        case StringTokenAction::MouseButton: {
            // A default outside the four texts reads string 0.
            const auto &mouseButtons = isTSL() ? kMouseButtonStrRefsTSL : kMouseButtonStrRefsK1;
            const bool known = defaultValue >= 0 && defaultValue < static_cast<int>(mouseButtons.size());
            value = strings.getText(known ? mouseButtons[defaultValue] : 0);
            break;
        }
        case StringTokenAction::Bounty:
            // Only TSL has the bounty token.
            value = isTSL() ? std::to_string(getGlobalNumber(kBountyGlobal) * kBountyCreditsPerPoint)
                            : kUnrecognizedToken;
            break;
        default:
            value = kUnrecognizedToken;
            break;
        }
    }
    // TSL trims the spaces from both ends of a token's value; KotOR keeps it
    // as it is.
    if (isTSL()) {
        value.erase(0, value.find_first_not_of(' '));
        value.erase(value.find_last_not_of(' ') + 1);
    }
    return value;
}

std::string Game::substituteCustomTokens(std::string str) const {
    bool hidden = false;
    const auto leader = _party.getLeader();
    return parseTokens(str, _customTokens, Creature::asCreature(leader), false, hidden);
}

std::string Game::getInterfaceText(int strRef) const {
    return substituteCustomTokens(_services.resource.strings.getText(strRef));
}

std::string Game::getInterfaceText(int strRef, const std::map<int, std::string> &tokens) {
    for (const auto &[token, value] : tokens) {
        setCustomToken(token, value);
    }
    return getInterfaceText(strRef);
}

std::string Game::getFeedbackText(int strRef) const {
    bool hidden = false;
    return parseTokens(_services.resource.strings.getText(strRef), _customTokens, std::nullopt, false, hidden);
}

std::string Game::getFeedbackText(int strRef, const std::map<int, std::string> &tokens) {
    for (const auto &[token, value] : tokens) {
        setCustomToken(token, value);
    }
    return getFeedbackText(strRef);
}

std::string Game::substituteLogTokens(std::string str) const {
    bool hidden = false;
    const auto leader = _party.getLeader();
    return parseTokens(str, _customTokens, Creature::asCreature(leader), true, hidden);
}

std::string Game::substituteLogTokens(std::string str, const Creature &subject) const {
    bool hidden = false;
    return parseTokens(str, _customTokens, subject, true, hidden);
}

void Game::setGlobalBoolean(const std::string &name, bool value) {
    _globalBooleans[name] = value;
}

void Game::setGlobalNumber(const std::string &name, int value) {
    // SetGlobalNumber stores the low byte in its signed-char table.
    // Express that conversion portably instead of relying on plain-char
    // signedness or an implementation-defined narrowing conversion.
    uint8_t raw = static_cast<uint8_t>(value);
    _globalNumbers[name] = raw <= 0x7f ? static_cast<int>(raw)
                                       : static_cast<int>(raw) - 0x100;
}

std::vector<SavedGame> Game::savedGames() const {
    return discoverSavedGames(_path);
}

void Game::setGlobalString(const std::string &name, const std::string &value) {
    _globalStrings[name] = value;
}

void Game::setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location) {
    _globalLocations[name] = location;
}

// Combat mode is the leader's, in the area it stands in. Outside a module (the
// main menu, character generation) nobody is in combat mode.
void Game::syncClientCombatMode() {
    auto leader = _party.getLeader();
    std::shared_ptr<Area> area;
    if (_module) {
        area = _module->area();
    }
    _clientCombatLeader = RuntimeObjectRef<Creature>(leader);
    _clientCombatArea = RuntimeObjectRef<Area>(area);
    _clientCombatMode = leader && area && area->isObjectResident(*leader) && leader->clientCombatMode();
    if (!_clientCombatMode) {
        _attackMashTime = 0.0f;
    }
}

void Game::setRelativeMouseMode(bool relative) {
    _relativeMouseMode = relative;
}

// In play the cursor is also held while the mouse turns the camera, unless a
// popup takes the input.
bool Game::relativeMouseMode() const {
    if (_relativeMouseMode) {
        return true;
    }
    if (_screen != Screen::InGame || _cameraType != CameraType::ThirdPerson || hasModalPanel()) {
        return false;
    }
    return getActiveCamera()->isMouseLookMode();
}

void Game::withLoadingScreen(const std::string &moduleName, const std::function<void()> &block) {
    if (!_loadScreen) {
        _loadScreen = tryLoadGUI<LoadingScreen>();
    }
    if (_loadScreen) {
        _loadScreen->setModuleImage(moduleName);
        _loadScreen->setProgress(0);
    }
    changeScreen(Screen::Loading);
    render();
    startLoadMusic();
    block();
    // KotOR's loading screen takes its music with it; TSL's plays on until
    // the area's sounds start. The transition list is cleared at the load's
    // end.
    if (!isTSL()) {
        stopLoadMusic();
    }
    _loadComesWithMovies = false;
    // Player input returns once a load is done.
    _playerInputBlocked = false;
}

/**
 * Terminal destination for a load that failed after the commit boundary.
 *
 * openMainMenu retires whatever was half-built and drops the candidate mounts
 * with it, so nothing of either session survives.
 */
void Game::retireToMainMenu() {
    openMainMenu();
}

// The selector of the TSL menu that shows the party leader.
static constexpr int kLeaderMenuSelector = 4;

void Game::openMainMenu() {
    // Only a committed playable session may replace the durable menu snapshot.
    // Cold startup and recovery from a partial load must not erase it.
    if (isTSL() && _runtimeSessionPlayable) {
        MenuPresentation state;
        state.selector = MenuPresentation::validateSelector(getGlobalNumber("GBL_MAIN_SITH_LORD"));
        if (state.selector == kLeaderMenuSelector) {
            if (auto leader = _party.getLeader()) {
                state.leader = leader->presentation();
            }
        }
        _options.game.menuPresentation = state;
        state.save(_options.game.configurationPath);
    }
    resetGame();
    // The main menu is the game's home screen: failing to load it is an error
    // of its own, not a screen left blank.
    if (!_mainMenu) {
        auto mainMenu = std::make_unique<MainMenu>(*this, _services);
        mainMenu->init();
        _mainMenu = std::move(mainMenu);
    } else {
        _mainMenu->refreshScene();
    }
    if (!_saveLoad) {
        _saveLoad = tryLoadGUI<SaveLoad>();
    }
    playMusic(_mainMenu->musicResRef());
    changeScreen(Screen::MainMenu);
}

static constexpr int kPauseTutorial = 6;

void Game::pressPauseKey() {
    // Pausing asks for the pause tutorial; the sound plays unless it opens.
    const bool paused = togglePlayerPause();
    if (paused && requestTutorialWindow(kPauseTutorial)) {
        return;
    }
    if (auto clip = _services.game.guiSounds.getActionAccepted()) {
        _services.audio.mixer.play(std::move(clip), AudioType::Sound);
    }
}

void Game::pressPauseButton() {
    // The HUD toggle asks for the pause tutorial whichever way it turns play.
    togglePlayerPause();
    requestTutorialWindow(kPauseTutorial);
}

static constexpr int kSoloModeOnStrRef = 37889;
static constexpr int kSoloModeForStealthStrRef = 37890;
static constexpr int kSoloModeOffStrRef = 37891;
static constexpr int kSoloModeOffStealthedStrRef = 37892;
static constexpr char kSoloModeScript[] = "k_sup_solo";

void Game::requestStealth() {
    auto leader = _party.getLeader();
    if (!leader || !leader->isStealthCapable()) {
        return;
    }
    if (_party.isSoloMode() || _party.getSize() == 1) {
        leader->useStealthSkill();
    } else {
        showSoloModeQuery(true);
    }
}

// The open query closes by itself, without restoring the pause, once a
// conversation or a transition starts or the leader goes down.
void Game::updateSoloModeQuery() {
    if (!_soloModeQueryOpen) {
        return;
    }
    if (!_confirmPopup || !_confirmPopup->isVisible()) {
        _soloModeQueryOpen = false;
        return;
    }
    // The query is up only in play.
    auto leader = _party.getLeader();
    if (isConversationActive() || _module->area()->transitionPending() ||
        (leader && (leader->isDead() || (_party.isMember(*leader) && leader->currentHitPoints() <= 0)))) {
        _soloModeQueryOpen = false;
        _confirmPopup->hide();
    }
}

// The query pauses play while it is up, unless play was already paused.
void Game::showSoloModeQuery(bool forStealth) {
    auto leader = _party.getLeader();
    if (isConversationActive() || _module->area()->transitionPending() || !leader || leader->isDead() ||
        leader->currentHitPoints() <= 0 || _party.getSize() <= 1) {
        if (auto clip = _services.game.guiSounds.getActionUnavailable()) {
            _services.audio.mixer.play(std::move(clip), AudioType::Sound);
        }
        return;
    }
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (!_confirmPopup) {
        return;
    }
    int strRef = kSoloModeOnStrRef;
    if (forStealth) {
        strRef = kSoloModeForStealthStrRef;
    } else if (_party.isSoloMode()) {
        strRef = leader->isStealthed() ? kSoloModeOffStealthedStrRef : kSoloModeOffStrRef;
    }
    const bool pausedBefore = _paused;
    if (!pausedBefore) {
        setPaused(true);
    }
    auto restorePause = [this, pausedBefore]() {
        _soloModeQueryOpen = false;
        if (!pausedBefore) {
            setPaused(false);
        }
    };
    _confirmPopup->showConfirm(
        _services.resource.strings.getText(strRef),
        [this, forStealth, restorePause]() {
            _soloModeQueryOpen = false;
            _party.setSoloMode(!_party.isSoloMode());
            _scriptRunner->run(kSoloModeScript, script::kObjectInvalid);
            if (forStealth) {
                if (auto leader = _party.getLeader()) {
                    leader->useStealthSkill();
                }
            }
            restorePause();
        },
        restorePause);
    _soloModeQueryOpen = true;
}

// The party-killed autopause comes at most once in this many seconds.
static constexpr float kPartyKilledAutoPauseInterval = 2.0f;
// After a door is opened, unlocked or bashed, an autopause requested within
// this many seconds is deferred by the second constant.
static constexpr float kAutoPauseDelayWindow = 5.0f;
static constexpr float kDeferredAutoPauseDelay = 1.0f;

void Game::requestAutoPause(AutoPauseReason reason) {
    const auto &options = _options.game.autoPause;
    bool enabled = false;
    switch (reason) {
    case AutoPauseReason::EnemySighted:
        enabled = options.enemySighted;
        break;
    case AutoPauseReason::EndOfCombatRound:
        enabled = options.endOfCombatRound;
        break;
    case AutoPauseReason::ActionMenu:
        enabled = options.actionMenu;
        break;
    case AutoPauseReason::PartyKilled:
        enabled = options.partyKilled;
        break;
    case AutoPauseReason::MineSighted:
        enabled = options.mineSighted;
        break;
    case AutoPauseReason::NewTargetSelected:
        enabled = options.newTargetSelected;
        break;
    }
    if (!enabled) {
        return;
    }
    autoPause(reason);
}

void Game::autoPause(AutoPauseReason reason) {
    // Autopause never stacks on an existing pause.
    if (_autoPaused || _paused) {
        return;
    }
    // Within the delay window the request is deferred, even one made during
    // a conversation.
    if (_autoPauseDelayWindow > 0.0f) {
        _deferredAutoPauseDelay = kDeferredAutoPauseDelay;
        _autoPauseDelayWindow = 0.0f;
        _deferredAutoPauseReason = reason;
        return;
    }
    // Autopause applies only over live play.
    if (_screen != Screen::InGame) {
        return;
    }
    if (reason == AutoPauseReason::PartyKilled) {
        if (_partyKilledAutoPauseDelay > 0.0f) {
            return;
        }
        _partyKilledAutoPauseDelay = kPartyKilledAutoPauseInterval;
    }
    static const std::map<AutoPauseReason, PauseReason> kPauseReasons {
        {AutoPauseReason::EnemySighted, PauseReason::EnemySighted},
        {AutoPauseReason::EndOfCombatRound, PauseReason::EndOfCombatRound},
        {AutoPauseReason::ActionMenu, PauseReason::ActionMenu},
        {AutoPauseReason::PartyKilled, PauseReason::PartyKilled},
        {AutoPauseReason::MineSighted, PauseReason::MineSighted},
        {AutoPauseReason::NewTargetSelected, PauseReason::NewTargetSelected}};
    setPaused(true, kPauseReasons.at(reason));
    _autoPaused = true;
}

void Game::delayNextEnemyAutoPause() {
    if (_deferredAutoPauseDelay <= 0.0f) {
        _autoPauseDelayWindow = kAutoPauseDelayWindow;
    }
}

// A deferred autopause is dropped when the leader is in combat mode once it
// is due. TSL pauses for an enemy sighting first; otherwise the stored reason
// pauses.
void Game::updateDeferredAutoPause(float frameTime) {
    if (_deferredAutoPauseDelay > 0.0f) {
        _deferredAutoPauseDelay -= frameTime;
        if (_deferredAutoPauseDelay <= 0.0f && !_clientCombatMode) {
            if (isTSL()) {
                autoPause(AutoPauseReason::EnemySighted);
            }
            autoPause(_deferredAutoPauseReason);
        }
    } else if (_autoPauseDelayWindow > 0.0f) {
        _autoPauseDelayWindow -= frameTime;
    }
}

void Game::setAutoPauseOptions(const AutoPauseOptions &options) {
    _options.game.autoPause = options;
}

void Game::saveAutoPauseOptions() const {
    _options.game.autoPause.save(_options.game.configurationPath);
}

void Game::saveFeedbackOptions() const {
    _options.game.feedback.save(_options.game.configurationPath);
}

void Game::saveDifficultyLevel() const {
    reone::game::saveDifficultyLevel(_options.game.configurationPath, _options.game.clientDifficulty);
}

void Game::saveMouseOptions() const {
    _options.game.mouse.save(_options.game.configurationPath);
}

// Tutorial windows

static constexpr int kTutorialNextStrRef = 38623;
static constexpr int kTutorialCloseStrRef = 1580;
static constexpr int kK1LastTutorial = 42;
// Tutorial windows are numbered from 0 to 255.
static constexpr int kLastTutorial = 255;
static constexpr int kCombatFeatTutorial = 0;
static constexpr int kGrenadeTutorial = 1;
static constexpr int kSetMineTutorial = 2;
static constexpr int kFriendlyPowerTutorial = 3;
static constexpr int kHostilePowerTutorial = 4;
static constexpr int kClearOneActionTutorial = 32;
static constexpr int kBashTutorial = 33;
static constexpr int kAttackTutorial = 34;
static constexpr int kAttackMashTutorial = 35;
static constexpr int kMenuCastRefusedStrRef = 1434;

// A page of a tutorial window: the string reference of its message, none
// when the cell is blank or the window has no such page.
static std::optional<int> tutorialMessage(const std::vector<std::optional<int>> &messages, int page) {
    return page >= 0 && page < static_cast<int>(messages.size()) ? messages[page] : std::nullopt;
}

// A tutorial window exists when its row has a first page.
bool Game::isTutorialWindowValid(int id) const {
    if (id < 0 || id > kLastTutorial || id >= static_cast<int>(_tutorials.size())) {
        return false;
    }
    return tutorialMessage(_tutorials[id].messages, 0).has_value();
}

bool Game::requestTutorialWindow(int id, uint32_t actor, uint32_t subject, uint32_t param) {
    if (!tutorialWindowsEnabled() || id < 0 || id > kLastTutorial) {
        return false;
    }
    if (!isTSL() && id > kK1LastTutorial) {
        return false;
    }
    if (_party.isTutorialShown(id) || isConversationActive() || !isTutorialWindowValid(id)) {
        return false;
    }
    // One slot: a later request before the next frame replaces this one.
    _tutorialPending = TutorialRequest {id, actor, subject, param};
    return true;
}

void Game::commitTutorialWindow() {
    if (!_tutorialPending) {
        return;
    }
    const auto request = *_tutorialPending;
    _tutorialPending.reset();
    if (!tutorialWindowsEnabled() || _party.isTutorialShown(request.id)) {
        return;
    }
    _party.setTutorialShown(request.id);
    // A window already open takes the new text; the game stays as it was paused.
    if (!_tutorial) {
        _tutorialPausedGame = !_paused;
        if (_tutorialPausedGame) {
            setPaused(true);
        }
    }
    _tutorial = request;
    _tutorialPage = 0;
    // A window that cannot be shown gives its action back at once.
    if (!showTutorialPage()) {
        finishTutorialWindow(true);
    }
}

bool Game::showTutorialPage() {
    const auto &tutorial = _tutorials[_tutorial->id];
    auto message = tutorialMessage(tutorial.messages, _tutorialPage);
    if (!message) {
        return false;
    }
    const bool hasNext = tutorialMessage(tutorial.messages, _tutorialPage + 1).has_value();

    std::shared_ptr<Texture> icon;
    const auto &iconResRef = tutorial.icon;
    if (!iconResRef.empty()) {
        // TSL leaves out controller button glyphs.
        static const std::array<const char *, 14> controllerGlyphs {
            "WHITEBUTTON", "STARTBUTTON", "XBUTTON", "YBUTTON", "Abutton", "Backbutton", "Bbutton",
            "Blbutton", "Whbutton", "Xbutton", "Ybutton", "uibut_Start", "uibut_X", "uibut_Y"};
        const bool glyph = isTSL() && std::any_of(controllerGlyphs.begin(), controllerGlyphs.end(), [&](const char *name) {
                               return boost::iequals(iconResRef, name);
                           });
        if (!glyph) {
            icon = _services.resource.textures.get(boost::to_lower_copy(iconResRef), TextureUsage::GUI);
        }
    }

    auto text = getInterfaceText(*message);
    if (!showMessagePopup(text, std::move(icon), [this, hasNext]() {
            if (hasNext) {
                ++_tutorialPage;
                if (showTutorialPage()) {
                    return;
                }
            }
            finishTutorialWindow(true);
        })) {
        return false;
    }
    _confirmPopup->setConfirmText(_services.resource.strings.getText(hasNext ? kTutorialNextStrRef : kTutorialCloseStrRef));
    return true;
}

void Game::finishTutorialWindow(bool takeAction) {
    const auto tutorial = *_tutorial;
    _tutorial.reset();
    if (_tutorialPausedGame) {
        setPaused(false);
    }
    _tutorialPausedGame = false;
    if (!takeAction) {
        return;
    }
    // The window finishes the action it interrupted.
    auto actor = getObjectById<Creature>(tutorial.actor);
    if (!actor) {
        return;
    }
    auto subject = getObjectById(tutorial.subject);
    std::shared_ptr<Action> action;
    switch (tutorial.id) {
    case kCombatFeatTutorial:
        if (subject) {
            sendAttack(*actor, subject, static_cast<FeatType>(tutorial.param));
        }
        break;
    case kGrenadeTutorial: {
        // The grenade may be the party's, carried by the player for whoever leads.
        auto item = getObjectById<Item>(tutorial.param);
        const auto spell = item ? item->activateSpell() : std::nullopt;
        if (subject && spell) {
            useMenuItem(*actor, item, item->spellProperty(*spell), subject, menuItemLocation(*actor, *subject));
        }
        break;
    }
    case kSetMineTutorial:
        // The kit is set where the setter stands, not on the door or placeable it was chosen for.
        if (auto item = std::dynamic_pointer_cast<Item>(subject)) {
            action = newAction<UseSkillAction>(SkillType::Demolitions, actor, 0, item);
        }
        break;
    case kFriendlyPowerTutorial:
    case kHostilePowerTutorial:
        if (auto spell = _services.game.spells.get(static_cast<SpellType>(tutorial.param)); spell && subject) {
            sendMenuCast(*actor, spell, subject);
        }
        break;
    case kClearOneActionTutorial:
        clearOneCombatAction(*actor);
        break;
    // The bash and the attack come back through their menu entries, whose
    // windows have now been shown.
    case kBashTutorial:
        if (prepareMenuBash(*actor, subject)) {
            sendAttack(*actor, subject);
        }
        break;
    case kAttackTutorial:
        if (prepareMenuAttack(*actor, subject)) {
            sendAttack(*actor, subject);
        }
        break;
    default:
        break;
    }
    // A creature that cannot be commanded sets no mine. Using the kit clears
    // the setter's actions out of combat, and so does setting the mine
    // whenever the area takes another, unless the setter is busy.
    if (action && actor->isCommandable()) {
        if (!actor->isInCombat() || _module->area()->playerCanSetMines()) {
            actor->clearOrdersUnlessBusy();
        }
        action->setUserAction(true);
        actor->addAction(std::move(action));
    }
}

void Game::beginMenuAttack(Creature &attacker) {
    attacker.setClientCombatMode(true);
    if (isTSL() && _party.isMember(attacker)) {
        for (const auto &member : _party.members()) {
            if (member.creature && member.creature.get() != &attacker) {
                member.creature->setClientCombatMode(true);
            }
        }
    }
    _attackMashTime = kAttackMashWindow;
}

// TSL takes up forms and casts the powers its feats grant straight away,
// whatever the caster's class.
static bool isImmediateMenuCast(bool tsl, SpellType spell) {
    static const std::array<SpellType, 5> kFeatPowers {
        SpellType::DroidTrick, SpellType::DroidConfusion, SpellType::WookieeRageI, SpellType::WookieeRageII,
        SpellType::WookieeRageIII};
    const bool form = spell >= SpellType::FormSaberIShiiCho && spell <= SpellType::FormForceIVMastery;
    return tsl && (form || std::find(kFeatPowers.begin(), kFeatPowers.end(), spell) != kFeatPowers.end());
}

bool Game::canMenuCast(const Creature &caster, const Spell &spell) const {
    if (isImmediateMenuCast(isTSL(), spell.type)) {
        return true;
    }
    // The Jedi classes, and in TSL the prestige classes.
    auto isJedi = [this](ClassType clazz) {
        return (clazz >= ClassType::JediGuardian && clazz <= ClassType::JediSentinel) ||
               (isTSL() && clazz >= ClassType::JediWeaponMaster && clazz <= ClassType::SithAssassin);
    };
    const auto &attributes = caster.attributes();
    return isJedi(attributes.getClassByPosition(1)) || isJedi(attributes.getClassByPosition(2));
}

void Game::refuseMenuCast(Creature &caster) {
    if (caster.racialType() == RacialType::Droid) {
        useMenuItem(caster, nullptr, std::nullopt, nullptr, glm::vec3(0.0f));
    }
}

void Game::useMenuItem(Creature &user, const std::shared_ptr<Item> &item, std::optional<size_t> property,
                       const std::shared_ptr<Object> &target, const glm::vec3 &location) {
    // Out of combat the order clears the user's actions unless the user is busy.
    if (!user.isInCombat()) {
        user.clearOrdersUnlessBusy();
    }
    const auto *entry = item && property && *property < item->properties().size() ? &item->properties()[*property] : nullptr;
    const auto spell = entry && entry->propertyName == static_cast<uint16_t>(ItemProperty::ActivateItem)
                           ? _services.game.spells.get(static_cast<SpellType>(entry->subtype))
                           : nullptr;
    if (!spell) {
        addFeedbackMessage(kMenuCastRefusedStrRef);
        return;
    }
    // A creature that cannot be commanded uses no item.
    if (!user.isCommandable()) {
        return;
    }
    auto action = newAction<CastSpellAtObjectAction>(spell, target, std::optional(item), false,
                                                     ProjectilePathType::Default, false, property);
    action->setCommandLocation(location);
    action->setUserAction(true);
    if (!useItem(user, *item, *property, action)) {
        addFeedbackMessage(kMenuCastRefusedStrRef);
    }
}

bool Game::useItem(Creature &user, const Item &item, size_t property, const std::shared_ptr<Action> &use) {
    if (!canUseItem(user, item, false) || property >= item.properties().size()) {
        return false;
    }
    const auto &entry = item.properties()[property];
    const bool usable = entry.propertyName == static_cast<uint16_t>(ItemProperty::ActivateItem) &&
                        item.isPropertyActive(entry) && entry.usable;
    if (usable) {
        _combat.scheduleCast(user, use);
    }
    // Using anything but forearm bands breaks the condition that allows only a shield.
    if (!breakForfeitCondition(user, Party::kForfeitNoItems) && item.itemType() != Item::kForearmBandsItemType) {
        breakForfeitCondition(user, Party::kForfeitNoItemButShield);
    }
    return usable;
}

void Game::sendMenuCast(Creature &caster, const std::shared_ptr<Spell> &spell, const std::shared_ptr<Object> &target) {
    if (!caster.isCommandable()) {
        return;
    }
    // Out of combat the order clears the caster's actions unless the caster is busy.
    if (!caster.isInCombat()) {
        caster.clearOrdersUnlessBusy();
    }
    // The player's cast at an object checks its line to the object.
    auto action = newAction<CastSpellAtObjectAction>(spell, target, std::nullopt);
    action->setUserAction(true);
    action->requireUseLineCheck();
    _combat.scheduleCast(caster, action);
    breakForfeitCondition(caster, Party::kForfeitNoForcePowers);
}

// The user-defined event a broken forfeit condition signals to the area.
static constexpr int kForfeitViolationEvent = 4001;

bool Game::breakForfeitCondition(const Creature &member, int condition) {
    if (!_party.isMember(member) || (_party.forfeitConditions() & condition) == 0) {
        return false;
    }
    _party.setLastForfeitViolation(condition);
    if (auto area = member.spatialArea()) {
        queueScriptEvent(area->get(), area->get(), *newEvent(static_cast<int>(SavedSignalEventType::UserDefined), std::vector<int32_t> {kForfeitViolationEvent}, std::vector<float> {}, std::vector<std::string> {}, std::vector<std::shared_ptr<Object>> {}));
    }
    return true;
}

// The only tag the Dxun battle circle allows in a hand.
static constexpr char kDxunSwordTag[] = "pl_sword";

void Game::breakEquipForfeit(const Creature &member, int slot, const Item &item) {
    if (!_party.isMember(member)) {
        return;
    }
    const int conditions = _party.forfeitConditions();
    std::optional<int> condition;
    if (slot == InventorySlots::body) {
        if (conditions & Forfeit::noArmor) {
            condition = Forfeit::noArmor;
        }
    } else if (slot == InventorySlots::rightWeapon || slot == InventorySlots::leftWeapon) {
        // The first condition the item breaks is the one recorded.
        if (conditions & Forfeit::noWeapons) {
            condition = Forfeit::noWeapons;
        } else if ((conditions & Forfeit::dxunSwordOnly) && !boost::iequals(item.tag(), kDxunSwordTag)) {
            condition = Forfeit::dxunSwordOnly;
        } else if ((conditions & Forfeit::noRanged) && item.isRanged()) {
            condition = Forfeit::noRanged;
        } else if ((conditions & Forfeit::noLightsaber) && item.isLightsaber()) {
            condition = Forfeit::noLightsaber;
        }
    }
    if (condition) {
        breakForfeitCondition(member, *condition);
    }
}

void Game::finishEquip(Creature &wearer, int slot, const std::shared_ptr<Item> &item) {
    signalItemEquipped(wearer, slot, item);
    breakEquipForfeit(wearer, slot, *item);
    if (slot == InventorySlots::body) {
        wearer.updateArmourAppearance(item);
    }
}

static constexpr int kItemEquippedEvent = 38;

void Game::signalItemEquipped(Creature &wearer, int slot, const std::shared_ptr<Item> &item) {
    // Character generation equips its creatures with no module to tell.
    if (!_module || slot == InventorySlots::rightWeapon2 || slot == InventorySlots::leftWeapon2) {
        return;
    }
    queueScriptEvent(*_module, wearer, *newEvent(kItemEquippedEvent, std::vector<int32_t> {}, std::vector<float> {}, std::vector<std::string> {}, std::vector<std::shared_ptr<Object>> {item}));
}

void Game::recordEquippedOnLoad(int slot, const std::shared_ptr<Item> &item) {
    assert(_stagedRuntimeObjectGraph && "equipment is read inside a runtime object graph");
    _stagedRuntimeObjectGraph->equippedOnLoad.push_back({slot, item});
}

std::shared_ptr<Creature> Game::equippedOnLoadWearer(const EquippedOnLoad &equipped) const {
    return std::dynamic_pointer_cast<Creature>(getObjectById(equipped.item->owner()));
}

void Game::releaseEquippedOnLoad(const std::vector<EquippedOnLoad> &equipped) {
    if (_transitionInProgress) {
        _equippedOnLoad.insert(_equippedOnLoad.end(), equipped.begin(), equipped.end());
        return;
    }
    for (const auto &[slot, item] : equipped) {
        auto wearer = equippedOnLoadWearer({slot, item});
        assert(wearer && "equipment read on load is worn by a live creature");
        signalItemEquipped(*wearer, slot, item);
    }
}

// An item destroyed during the load tells the module nothing.
void Game::signalEquippedOnLoad(const Creature &wearer) {
    for (auto it = _equippedOnLoad.begin(); it != _equippedOnLoad.end();) {
        auto creature = equippedOnLoadWearer(*it);
        if (creature.get() != &wearer) {
            ++it;
            continue;
        }
        if (isRuntimeObjectLive(*it->item)) {
            signalItemEquipped(*creature, it->slot, it->item);
        }
        it = _equippedOnLoad.erase(it);
    }
}

// The party arrives in every module as if read anew: the player character,
// then each other member, then each puppet, tells the module of every item it
// wears, in slot order. What any of them was read wearing during the load is
// not told twice.
void Game::signalPartyEquipment() {
    std::vector<std::shared_ptr<Creature>> arriving;
    auto player = _party.player();
    if (player) {
        arriving.push_back(player);
    }
    for (const auto &member : _party.members()) {
        if (member.creature && member.creature != player) {
            arriving.push_back(member.creature);
        }
    }
    for (int puppet : _party.persistedState().puppetIds) {
        if (auto creature = _party.getAvailablePuppet(puppet, true)) {
            arriving.push_back(creature);
        }
    }
    for (const auto &creature : arriving) {
        _equippedOnLoad.erase(
            std::remove_if(
                _equippedOnLoad.begin(),
                _equippedOnLoad.end(),
                [&](const EquippedOnLoad &kept) { return equippedOnLoadWearer(kept) == creature; }),
            _equippedOnLoad.end());
        for (const auto &[slot, item] : creature->equipment()) {
            signalItemEquipped(*creature, slot, item);
        }
    }
}

// Creatures made by scripts during the load tell the module last. A wearer or
// item destroyed during the load tells it nothing.
void Game::signalKeptEquippedOnLoad() {
    auto kept = std::move(_equippedOnLoad);
    _equippedOnLoad.clear();
    for (const auto &equipped : kept) {
        auto wearer = equippedOnLoadWearer(equipped);
        if (wearer && isRuntimeObjectLive(*equipped.item)) {
            signalItemEquipped(*wearer, equipped.slot, equipped.item);
        }
    }
}

std::optional<int> Game::forcePowerTutorial(const Spell &spell) const {
    if (isImmediateMenuCast(isTSL(), spell.type)) {
        return std::nullopt;
    }
    return spell.hostileSlot != -1 ? kHostilePowerTutorial : kFriendlyPowerTutorial;
}

void Game::clearOneAction() {
    auto leader = _party.getLeader();
    if (!leader || !_clientCombatMode) {
        return;
    }
    if (requestTutorialWindow(kClearOneActionTutorial, leader->id())) {
        return;
    }
    clearOneCombatAction(*leader);
}

// The last pending round entry goes; without one, a forced clear takes the
// leader's actions, the one its round has taken up included.
void Game::clearOneCombatAction(Creature &leader) {
    if (_combat.removeLastScheduled(leader)) {
        return;
    }
    leader.clearAllActions(true);
}

void Game::cancelCombat() {
    auto leader = _party.getLeader();
    if (!leader || !_clientCombatMode) {
        return;
    }
    leader->setClientCombatMode(false);
    _combat.clearAllOrders(*leader);
}

void Game::markNoClickEvent(uint32_t milliseconds) {
    _noClickDay = worldTimeDay();
    _noClickTime = worldTimeOfDay();
    _noClickMilliseconds = milliseconds;
}

// A whole day on, or the marked time past, a click works again; so does one
// made from a world time earlier than the mark.
bool Game::canClick() const {
    const uint32_t day = worldTimeDay();
    const uint32_t time = worldTimeOfDay();
    if (day < _noClickDay || (day == _noClickDay && time < _noClickTime)) {
        return true;
    }
    uint32_t days = 0;
    uint32_t elapsed = 0;
    subtractWorldTimes(day, time, _noClickDay, _noClickTime, days, elapsed);
    return days != 0 || elapsed >= _noClickMilliseconds;
}

void Game::clearPlayerHostileActions() {
    auto player = _party.getLeader();
    if (!player) {
        return;
    }
    const ObjectList creatures = _module->area()->getObjectsByType(ObjectType::Creature);
    for (const auto &object : creatures) {
        auto &creature = static_cast<Creature &>(*object);
        if (_party.isMember(creature) || creature.getReputationToward(*player) > kHighestHostileReputation) {
            continue;
        }
        creature.clearAllActions(true);
        _combat.clearAllOrders(creature);
    }
}

bool Game::prepareMenuBash(Creature &basher, const std::shared_ptr<Object> &target) {
    if (!target) {
        return false;
    }
    const bool door = target->type() == ObjectType::Door;
    if (requestTutorialWindow(kBashTutorial, basher.id(), target->id(), door ? 1 : 0)) {
        return false;
    }
    // A door is bashed after everything else the basher was doing is dropped,
    // as the player's controls clear it.
    if (door) {
        _combat.clearAllOrders(basher);
        delayNextEnemyAutoPause();
    }
    return true;
}

bool Game::prepareMenuAttack(Creature &attacker, const std::shared_ptr<Object> &target) {
    if (!target) {
        return false;
    }
    // A repeated attack soon after the last one asks not to mash, and is dropped.
    if (attackMashActive() && requestTutorialWindow(kAttackMashTutorial, attacker.id(), target->id())) {
        return false;
    }
    if (requestTutorialWindow(kAttackTutorial, attacker.id(), target->id())) {
        return false;
    }
    beginMenuAttack(attacker);
    return true;
}

static constexpr char kNihilusTag[] = "darthnihilus";
static constexpr int kNihilusUnassailableBoolean = 52;

void Game::sendAttack(Creature &attacker, const std::shared_ptr<Object> &target, FeatType feat) {
    if (!target) {
        return;
    }
    // TSL ignores attacks on Nihilus while his local boolean 52 is set.
    if (isTSL()) {
        auto nihilus = _module->area()->getObjectByTag(kNihilusTag);
        if (nihilus && nihilus == target && nihilus->getLocalBoolean(kNihilusUnassailableBoolean)) {
            return;
        }
    }
    if (auto action = _combat.scheduleAttack(attacker, target, feat)) {
        action->setUserAction(true);
    }
    // TSL party members with no attack target of their own join an attack on a
    // creature, at once and in front of anything else. Each of them remembers
    // it as the player's order, whether or not it can join.
    auto targetCreature = std::dynamic_pointer_cast<Creature>(target);
    if (isTSL() && targetCreature && _party.isMember(attacker) && !_party.isSoloMode() && _party.getSize() >= 2) {
        for (const auto &member : _party.members()) {
            auto creature = member.creature;
            if (!creature || creature.get() == &attacker || creature->getAttemptedAttackTarget() != script::kObjectInvalid) {
                continue;
            }
            creature->setOrderedAttackTarget(target->id());
            // A member who is down does not join.
            if (creature->isDead() || creature->isTemporarilyDead()) {
                continue;
            }
            // A confused member joins whether or not it can be commanded, and
            // whatever it thinks of the target.
            const auto state = static_cast<CreatureState>(creature->effectState());
            const bool stateExempt = state == CreatureState::Confusion || state == CreatureState::DroidConfused;
            if (!creature->isCommandable() && !stateExempt) {
                continue;
            }
            if (_services.game.reputes.getIsFriend(*targetCreature, *creature) && !stateExempt) {
                continue;
            }
            creature->clearAllActions(true);
            creature->addActionOnTop(newAction<AttackObjectAction>(target));
        }
    }
    // An order on a target other than the one the attacker is attempting is
    // remembered as the player's order.
    if (attacker.getAttemptedAttackTarget() != target->id()) {
        attacker.setOrderedAttackTarget(target->id());
    }
}

// END Tutorial windows

static constexpr float kSightingHold = 10.0f;
static constexpr int kTargetCyclingTutorial = 8;
static constexpr int kHostileCreatureTutorial = 21;
static constexpr float kNearestHostileRange = 30.0f;
static constexpr float kNearestOtherRangeTSL = 10.0f;
static constexpr float kNearestOtherRange = 30.0f;
static constexpr float kNearestConeApexOffset = 4.0f;
static constexpr float kNearestConeCosine = 0.866f;
static constexpr float kDegreesPerRadian = 57.2958f;
static constexpr float kTargetUnseenGrace = 1.0f;
static constexpr float kDeadTargetHold = 1.5f;
static constexpr float kTargetMoveSpeedTime = 0.5f;
static constexpr int kHostileStanding = 11;

// A creature that is dead, or (TSL) a party member at zero vitality, or
// (KotOR) temporarily dead, cannot hold the target.
bool Game::isDownForSelection(const Creature &creature) const {
    if (creature.isDead()) {
        return true;
    }
    if (isTSL()) {
        return _party.isMember(creature) && creature.currentHitPoints() <= 0;
    }
    return creature.isTemporarilyDead();
}

// Whether the leader sees an object is asked once per pass.
bool Game::isNearestObjectSeen(NearestObject &entry, const Creature &leader, const Area &area) {
    if (!entry.seen) {
        auto object = entry.object.resolve();
        entry.seen = object && area.isObjectSeen(leader, *object);
    }
    return *entry.seen;
}

// Rebuilds the objects that can hold the leader's target, sorted by bearing
// from the leader's facing, and returns the ones in the forward cone sorted by
// distance. Hostile objects count out to 30 m; others to 10 m in TSL, 30 m in KotOR.
std::vector<size_t> Game::collectNearestObjects(const Creature &leader, const Area &area) {
    struct Candidate {
        NearestObject entry;
        float bearing;
        float distance2;
    };
    const float otherRange = isTSL() ? kNearestOtherRangeTSL : kNearestOtherRange;
    const glm::vec3 facing {-std::sin(leader.getFacing()), std::cos(leader.getFacing()), 0.0f};
    const float facingYaw = std::atan2(-facing.x, facing.y) * kDegreesPerRadian;
    auto &reputes = _services.game.reputes;
    std::vector<Candidate> candidates;
    for (const auto &object : area.objects()) {
        if (object.get() == &leader) {
            continue;
        }
        bool rangeHostile = false;
        bool hostile = false;
        if (auto *creature = dyn_cast<Creature>(object.get())) {
            if (isDownForSelection(*creature)) {
                // A lootable corpse is what the player targets to search it.
                if (!creature->isSelectable()) {
                    continue;
                }
            } else {
                // Listed once seen, or when spotted now while heard.
                const auto &perception = leader.perception();
                if (!perception.sees(creature->id()) &&
                    !(perception.hears(creature->id()) &&
                      leader.detectsBySight(*creature, false)))
                    continue;
                rangeHostile = hostile = _module->isHostileToPartyLeader(*creature);
            }
        } else if (auto *trigger = dyn_cast<Trigger>(object.get())) {
            // Hostile mines are listed once the leader has found them, and at the shorter range.
            if (!trigger->isTrap() ||
                (!trigger->trapDetection().isDetectedBy(leader.id()) && trigger->isTrapHostileTo(leader)))
                continue;
            hostile = trigger->isTrapHostileTo(leader);
        } else if (auto *placeable = dyn_cast<Placeable>(object.get())) {
            if (!placeable->isUsable()) {
                continue;
            }
            hostile = placeable->isHostileAppearance();
            rangeHostile = hostile && reputes.getReputation(placeable->faction(), leader.faction()) < kHostileStanding;
        } else if (auto *door = dyn_cast<Door>(object.get())) {
            if (door->state() != DoorState::Closed || door->isStatic()) {
                continue;
            }
        } else {
            continue;
        }
        const glm::vec3 offset {object->position() - leader.position()};
        const float distance2 = glm::dot(offset, offset);
        const float range = rangeHostile ? kNearestHostileRange : otherRange;
        if (distance2 >= range * range) {
            continue;
        }
        float bearing = std::atan2(-offset.x, offset.y) * kDegreesPerRadian - facingYaw;
        while (bearing < 0.0f) {
            bearing += 360.0f;
        }
        while (bearing > 359.0f) {
            bearing -= 360.0f;
        }
        const glm::vec3 fromApex {object->position() - (leader.position() - kNearestConeApexOffset * facing)};
        const bool inCone = glm::dot(facing, offset) >= 0.0f &&
                            glm::dot(facing, glm::normalize(fromApex)) > kNearestConeCosine;
        Candidate candidate {NearestObject {RuntimeObjectRef<Object>(object), inCone, hostile, std::nullopt}, bearing, distance2};
        candidates.push_back(std::move(candidate));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.bearing < right.bearing;
    });
    _nearestObjects.clear();
    std::vector<size_t> cone;
    for (size_t i = 0; i < candidates.size(); ++i) {
        _nearestObjects.push_back(candidates[i].entry);
        if (candidates[i].entry.inCone) {
            cone.push_back(i);
        }
    }
    std::stable_sort(cone.begin(), cone.end(), [&candidates](size_t left, size_t right) {
        return candidates[left].distance2 < candidates[right].distance2;
    });
    return cone;
}

// Direct movement out of combat mode lets go of the target after half a second.
void Game::releaseTargetOnMove(float frameTime) {
    // A deferred autopause holds the target too.
    if (!_clientCombatMode && _deferredAutoPauseDelay <= 0.0f && _lastTarget.resolve() && !_paused &&
        _module->isPlayerMoving()) {
        _targetMoveTime += frameTime;
        if (_targetMoveTime >= kTargetMoveSpeedTime) {
            _targetMoveTime = 0.0f;
            _lastTarget.reset();
        }
    } else {
        _targetMoveTime = 0.0f;
    }
}

// In combat mode a dead target stays for a moment before another is picked.
bool Game::holdsDeadTarget(const std::shared_ptr<Object> &target, float frameTime) {
    auto targetCreature = std::dynamic_pointer_cast<Creature>(target);
    const bool down = targetCreature && isDownForSelection(*targetCreature);
    if (_deadTargetHold <= 0.0f) {
        if (down) {
            _deadTargetHold = kDeadTargetHold;
            return true;
        }
        return false;
    }
    _deadTargetHold -= frameTime;
    if (_deadTargetHold <= 0.0f) {
        return false;
    }
    if (!down) {
        _deadTargetHold = 0.0f;
        return false;
    }
    return true;
}

// The first enemy or mine to come into view takes the target, unless play is
// paused. A sighting holds while anything of its kind stays in view and for
// ten seconds after the last one leaves, so only a fresh one counts.
void Game::takeSightedTarget(TargetPass &pass, bool hostileCreature, bool hostileTrigger,
                             const Creature &leader, const Area &area, float frameTime) {
    bool enemyInView = false;
    bool mineInView = false;
    if (hostileCreature || hostileTrigger) {
        bool enemyDone = !hostileCreature;
        bool mineDone = !hostileTrigger;
        for (auto &entry : _nearestObjects) {
            auto object = entry.object.resolve();
            if (!object || !entry.hostile || !isNearestObjectSeen(entry, leader, area)) {
                continue;
            }
            auto *trigger = dyn_cast<Trigger>(object.get());
            const bool mine = trigger && trigger->isTrap();
            if (mine) {
                mineInView = true;
            } else {
                enemyInView = true;
            }
            if (!_paused) {
                bool &done = mine ? mineDone : enemyDone;
                done = true;
                if (!(mine ? _mineSighted : _enemySighted)) {
                    if (!mine) {
                        requestTutorialWindow(kHostileCreatureTutorial);
                    }
                    if (!_clientCombatMode) {
                        requestAutoPause(mine ? AutoPauseReason::MineSighted : AutoPauseReason::EnemySighted);
                    }
                    pass.target = object;
                    pass.keep = true;
                    pass.sighted = true;
                    _passTargetGone = false;
                    break;
                }
            }
            if (enemyDone && mineDone) {
                break;
            }
        }
    }
    auto updateSighting = [frameTime](bool inView, bool &sighted, float &hold) {
        if (sighted && !inView) {
            if (hold < 0.0f) {
                sighted = false;
            } else {
                hold -= frameTime;
            }
        } else {
            sighted = inView;
            hold = kSightingHold;
        }
    };
    updateSighting(enemyInView, _enemySighted, _enemySightingHold);
    updateSighting(mineInView, _mineSighted, _mineSightingHold);
}

// A kept target must stay in view, or return to it within a second.
bool Game::isTargetLost(TargetPass &pass, const Creature &leader, const Area &area, float frameTime) {
    auto listed = std::find_if(_nearestObjects.begin(), _nearestObjects.end(), [&pass](const auto &entry) {
        return entry.object.resolve() == pass.target;
    });
    const bool seen = listed != _nearestObjects.end()
                          ? isNearestObjectSeen(*listed, leader, area)
                          : area.isObjectSeen(leader, *pass.target);
    if (seen) {
        _targetUnseenTime = 0.0f;
        return false;
    }
    _targetUnseenTime += frameTime;
    if (_targetUnseenTime >= kTargetUnseenGrace) {
        _targetUnseenTime = 0.0f;
        return true;
    }
    pass.unseenGrace = true;
    return false;
}

// In combat mode with a hostile creature about: the nearest visible hostile
// ahead, then any visible hostile, then the nearest other object ahead.
// Otherwise the nearest visible object ahead.
std::shared_ptr<Object> Game::pickTarget(const std::vector<size_t> &cone, bool hostileCreature,
                                         const Creature &leader, const Area &area) {
    if (!_clientCombatMode || !hostileCreature) {
        for (size_t index : cone) {
            auto &entry = _nearestObjects[index];
            auto object = entry.object.resolve();
            if (object && isNearestObjectSeen(entry, leader, area)) {
                return object;
            }
        }
        return nullptr;
    }
    std::shared_ptr<Object> ahead;
    for (size_t index : cone) {
        auto &entry = _nearestObjects[index];
        auto object = entry.object.resolve();
        if (!object) {
            continue;
        }
        if (!entry.hostile) {
            if (!ahead) {
                ahead = object;
            }
        } else if (isNearestObjectSeen(entry, leader, area)) {
            return object;
        }
    }
    for (auto &entry : _nearestObjects) {
        auto object = entry.object.resolve();
        if (object && entry.hostile && isNearestObjectSeen(entry, leader, area)) {
            return object;
        }
    }
    return ahead;
}

// Holds the target among the objects around the leader, or picks another.
void Game::selectPassTarget(TargetPass &pass, const Creature &leader, const Area &area, float frameTime) {
    const auto cone = collectNearestObjects(leader, area);
    if (_nearestObjects.empty()) {
        return;
    }
    // Whether a hostile creature or mine is around, and whether the target is listed.
    bool hostileCreature = false;
    bool hostileTrigger = false;
    for (const auto &entry : _nearestObjects) {
        auto object = entry.object.resolve();
        if (!hostileCreature && object && entry.hostile) {
            if (isa<Creature>(object.get())) {
                hostileCreature = true;
            } else if (isa<Trigger>(object.get())) {
                hostileTrigger = true;
            }
        }
        if (object && object == pass.target && !_passTargetGone) {
            pass.keep = true;
        }
    }
    if (hostileCreature && _clientCombatMode && !pass.keep) {
        pass.keep = holdsDeadTarget(pass.target, frameTime);
    } else {
        _deadTargetHold = 0.0f;
    }
    takeSightedTarget(pass, hostileCreature, hostileTrigger, leader, area, frameTime);
    const bool repick = !pass.keep || !pass.target || isTargetLost(pass, leader, area, frameTime);
    if (!repick) {
        return;
    }
    pass.keep = false;
    if (auto picked = pickTarget(cone, hostileCreature, leader, area)) {
        pass.target = std::move(picked);
        pass.keep = true;
        pass.repicked = true;
        _passTargetGone = false;
    }
}

// The target held is the leader's and the HUD's, and a new one is looked at;
// a sighting also swings the camera to it.
void Game::presentPassTarget(const TargetPass &pass) {
    _lastTarget = pass.keep ? RuntimeObjectRef<Object>(pass.target) : RuntimeObjectRef<Object>();
    _party.setLeaderLastTarget(_lastTarget);
    _passTarget = _lastTarget;
    auto targetCreature = pass.keep ? std::dynamic_pointer_cast<Creature>(pass.target) : nullptr;
    _passTargetUp = targetCreature && !isDownForSelection(*targetCreature);
    // The HUD shows the target unless the console pinned a selection.
    auto &area = *_module->area();
    if (!area.isSelectionForced()) {
        area.selectObject(pass.keep ? pass.target : nullptr);
    }
    if (pass.keep && pass.target) {
        const bool look = pass.repicked || (!pass.unseenGrace && (pass.sighted || _lastTargetLook));
        showTarget(pass.target, look, !pass.repicked && !pass.unseenGrace && pass.sighted);
    } else {
        showTarget(nullptr, false, false);
    }
    _lastTargetLook = false;
}

// Keeps the leader's target on a valid nearby object, every in-game frame and
// also while paused. A first enemy or mine sighting takes the target and may
// pause play. A target that stays listed and in view, or out of view for less
// than a second, is kept; otherwise one is picked from what lies ahead.
void Game::updatePassiveSelection(float frameTime) {
    releaseTargetOnMove(frameTime);
    TargetPass pass;
    pass.target = _lastTarget.resolve();
    if (!pass.target || pass.target != _passTarget.resolve()) {
        _passTargetGone = false;
    } else if (_passTargetUp) {
        auto creature = std::dynamic_pointer_cast<Creature>(pass.target);
        if (creature && isDownForSelection(*creature)) {
            _passTargetGone = true;
        }
    }
    if (auto leader = _party.getLeader()) {
        selectPassTarget(pass, *leader, *_module->area(), frameTime);
    }
    presentPassTarget(pass);
}

std::shared_ptr<Object> Game::selectNearestObject(int direction) {
    requestTutorialWindow(kTargetCyclingTutorial);
    auto leader = _party.getLeader();
    if (!leader || _nearestObjects.empty()) {
        return nullptr;
    }
    // Targets are cycled only in play.
    const auto area = _module->area();
    auto hostileCreature = [](const NearestObject &entry, const Object &object) {
        return entry.hostile && isa<Creature>(&object);
    };
    // In combat mode only hostile creatures qualify, if any visible one remains.
    // Entries that are gone or out of view are dropped on the way.
    bool hostilesOnly = _clientCombatMode;
    if (hostilesOnly) {
        bool found = false;
        for (size_t i = _nearestObjects.size(); i-- > 0;) {
            auto object = _nearestObjects[i].object.resolve();
            if (!object || !isNearestObjectSeen(_nearestObjects[i], *leader, *area)) {
                _nearestObjects.erase(_nearestObjects.begin() + i);
                continue;
            }
            if (hostileCreature(_nearestObjects[i], *object)) {
                found = true;
                break;
            }
        }
        hostilesOnly = found;
    }
    if (_nearestObjects.empty()) {
        _lastTarget.reset();
        if (!area->isSelectionForced()) {
            area->selectObject(nullptr);
        }
        showTarget(nullptr, true, false);
        return nullptr;
    }

    const auto target = _lastTarget.resolve();
    int count = static_cast<int>(_nearestObjects.size());
    int current = -1;
    for (int i = 0; target && i < count; ++i) {
        if (_nearestObjects[i].object.resolve() == target) {
            current = i;
            break;
        }
    }
    int index;
    if (current >= 0) {
        index = direction == 0 ? (current < count - 1 ? current + 1 : 0) : (current == 0 ? count - 1 : current - 1);
    } else {
        index = direction == 0 ? 0 : count - 1;
    }
    std::shared_ptr<Object> found;
    for (int steps = count; steps > 0 && count > 0; --steps) {
        auto &entry = _nearestObjects[index];
        auto object = entry.object.resolve();
        bool removed = false;
        if (!object) {
            removed = true;
        } else if (!hostilesOnly || hostileCreature(entry, *object)) {
            if (isNearestObjectSeen(entry, *leader, *area)) {
                found = object;
                break;
            }
            removed = true;
        }
        if (removed) {
            _nearestObjects.erase(_nearestObjects.begin() + index);
            --count;
            if (count == 0) {
                break;
            }
        } else if (direction == 0) {
            ++index;
        }
        if (direction == 0) {
            if (index >= count) {
                index = 0;
            }
        } else {
            index = (index <= 0 || index > count) ? count - 1 : index - 1;
        }
    }
    if (found) {
        _lastTarget = RuntimeObjectRef<Object>(found);
        if (!area->isSelectionForced()) {
            area->selectObject(found);
        }
        showTarget(found, true, true);
    } else if (_nearestObjects.empty()) {
        _lastTarget.reset();
        if (!area->isSelectionForced()) {
            area->selectObject(nullptr);
        }
        showTarget(nullptr, true, false);
    }
    return found;
}

// Target presentation

static constexpr float kTargetLookDistance = 10.0f;
static constexpr float kHostileHiliteTime = 0.25f;
static const glm::vec3 kHostileHiliteColor(0.74f, 0.11f, 0.0f);

// The leader's head turns to a newly presented target; the camera swings to
// it when asked or in combat mode (the combat camera itself never swings);
// and in combat mode a hostile creature flashes red for a quarter second.
void Game::showTarget(const std::shared_ptr<Object> &object, bool look, bool camera) {
    auto leader = _party.getLeader();
    if (!object) {
        if (leader) {
            leader->lookAt(nullptr, kTargetLookDistance);
        }
        return;
    }
    if (!look) {
        return;
    }
    if (leader) {
        leader->lookAt(object, kTargetLookDistance);
    }
    if ((camera || _clientCombatMode) && _cameraType == CameraType::ThirdPerson) {
        _module->area()->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson)->setLookAtTarget(object);
    }
    auto creature = std::dynamic_pointer_cast<Creature>(object);
    if (_clientCombatMode && creature && _module->isHostileToPartyLeader(*creature)) {
        auto model = creature->sceneNode();
        if (model && model->type() == SceneNodeType::Model) {
            static_cast<ModelSceneNode &>(*model).setHiliteColor(kHostileHiliteColor);
        }
        auto hilite = std::find_if(_hostileHilites.begin(), _hostileHilites.end(), [&creature](const auto &entry) {
            return entry.creature.resolve() == creature;
        });
        if (hilite != _hostileHilites.end()) {
            hilite->timer = kHostileHiliteTime;
        } else {
            _hostileHilites.push_back(HostileHilite {RuntimeObjectRef<Creature>(creature), kHostileHiliteTime});
        }
    }
}

// The red flash runs on game time: it holds while play is paused.
void Game::updateHostileHilites(float dt) {
    for (size_t i = _hostileHilites.size(); i-- > 0;) {
        auto &entry = _hostileHilites[i];
        entry.timer -= dt;
        if (entry.timer > 0.0f) {
            continue;
        }
        if (auto creature = entry.creature.resolve()) {
            auto model = creature->sceneNode();
            if (model && model->type() == SceneNodeType::Model) {
                static_cast<ModelSceneNode &>(*model).setHiliteColor(std::nullopt);
            }
        }
        _hostileHilites.erase(_hostileHilites.begin() + i);
    }
}

// END Target presentation

void Game::openInGame() {
    _runtimeSessionPlayable = static_cast<bool>(_module);
    changeScreen(Screen::InGame);
}

namespace {

// Result of trying to anchor the race to the authored player track.
struct SwoopTrackFrame {
    glm::vec3 position {0.0f};
    float facing {0.0f};
    std::string mode {"fallback"}; // "lyt-track", "track-model", or "fallback"
    std::string reason;            // why fallback was used
    std::string info;              // concise track inspection details
};

// Max 2D distance (world units) the track's start hook may be from the party
// leader to be trusted WITHOUT an LYT placement. With an LYT track placement the
// hook is in module/world space, so this guard does not apply; without one a
// standalone-loaded track model may sit in a different frame and would otherwise
// teleport the bike into the void, so the proven leader anchor is kept.
constexpr float kTrackFrameMaxDistance = 64.0f;

// Non-blocking finish threshold (forward-progress units). The race finishes
// a margin past the furthest mapped obstacle, or at a conservative fallback
// distance when no obstacle placements exist.
constexpr float kSwoopFinishMargin = 500.0f;
constexpr float kSwoopFallbackFinishProgress = 4000.0f;

// Derive the bike start frame from the player track model's modelhook node.
// With LYT placement, transform the hook into world space. Without placement,
// use the hook only when it resolves near the party leader; otherwise use the leader frame.
SwoopTrackFrame deriveSwoopTrackFrame(const std::shared_ptr<graphics::Model> &trackModel,
                                      const std::string &trackResRef,
                                      const std::optional<glm::vec3> &lytTrackPos,
                                      const glm::vec3 &leaderPos,
                                      float leaderFacing) {
    SwoopTrackFrame frame;
    frame.position = leaderPos;
    frame.facing = leaderFacing;

    if (trackResRef.empty()) {
        frame.reason = "no-track-ref";
        return frame;
    }
    if (!trackModel) {
        frame.reason = "track-model-missing";
        return frame;
    }

    size_t animCount = trackModel->getAnimationNames().size();
    auto hook = trackModel->getNodeByNameRecursive("modelhook");
    if (!hook) {
        frame.reason = "no-modelhook";
        frame.info = str(boost::format("modelhook=no placement=%s anims=%zu")
                         % (lytTrackPos ? "yes" : "no") % animCount);
        return frame;
    }

    const glm::mat4 &abs = hook->absoluteTransform();
    glm::vec3 hookLocal {abs[3]};
    // Facing convention: forward = (-sin f, cos f). The LYT track placement
    // carries no rotation, so the hook's model-space orientation is also its
    // world orientation.
    glm::vec3 forward {glm::normalize(glm::vec3 {glm::mat3 {abs} * glm::vec3 {0.0f, 1.0f, 0.0f}})};
    float facing = glm::atan(-forward.x, forward.y);

    if (lytTrackPos) {
        // Combine the LYT placement (translation only) with the modelhook's
        // model-local transform to get the hook in module/world space.
        glm::vec3 start {*lytTrackPos + hookLocal};
        frame.position = start;
        frame.facing = facing;
        frame.mode = "lyt-track";
        frame.info = str(boost::format("modelhook=yes placement=yes anims=%zu lyt=[%.1f,%.1f,%.1f] hook=[%.1f,%.1f,%.1f] start=[%.1f,%.1f,%.1f]")
                         % animCount
                         % lytTrackPos->x % lytTrackPos->y % lytTrackPos->z
                         % hookLocal.x % hookLocal.y % hookLocal.z
                         % start.x % start.y % start.z);
        return frame;
    }

    float dist = glm::distance(glm::vec2 {hookLocal}, glm::vec2 {leaderPos});
    frame.info = str(boost::format("modelhook=yes placement=no anims=%zu hook=[%.1f,%.1f,%.1f] dist=%.1f")
                     % animCount % hookLocal.x % hookLocal.y % hookLocal.z % dist);

    if (dist > kTrackFrameMaxDistance) {
        // No LYT placement and the hook is not in the party's world frame.
        frame.reason = "no-lyt-track-placement";
        return frame;
    }

    frame.position = hookLocal;
    frame.facing = facing;
    frame.mode = "track-model";
    return frame;
}

// The bike's models: every Player.Models entry except the camera mount
// (player.cameraResRef), the first as the root and the rest attached to it. The
// visible bike may appear after the first entry.
struct SwoopBikeModels {
    std::shared_ptr<ModelSceneNode> root;
    std::vector<std::shared_ptr<ModelSceneNode>> children;
    /** A load line per entry, for the developer log. */
    std::vector<std::string> diagnostics;
    bool anyMissing {false};
};

SwoopBikeModels loadSwoopBikeModels(const MinigameSpec &mg, ServicesView &services, float drawDistance) {
    auto &sceneGraph = services.scene.graphs.get(kSceneMain);
    SwoopBikeModels bike;
    for (const auto &modelSpec : mg.player.models) {
        const auto &resRef = modelSpec.resRef;
        if (resRef.empty()) {
            continue;
        }
        if (!mg.player.cameraResRef.empty() && boost::iequals(resRef, mg.player.cameraResRef)) {
            bike.diagnostics.push_back(resRef + " camera-skip");
            continue;
        }
        auto model = services.resource.models.get(resRef);
        if (!model) {
            bike.diagnostics.push_back(resRef + " missing");
            bike.anyMissing = true;
            continue;
        }
        auto node = sceneGraph.newModel(*model, ModelUsage::Placeable);
        node->setDrawDistance(drawDistance);
        if (!bike.root) {
            bike.root = std::move(node);
        } else {
            bike.root->addChild(*node);
            bike.children.push_back(std::move(node));
        }
        bike.diagnostics.push_back(resRef + " loaded");
    }
    return bike;
}

// Choose a non-blocking finish threshold. Loop/finish handling is
// script-driven and not yet implemented, so use the furthest mapped LYT
// obstacle (the obstacle field spans the playable track) plus a margin, or
// a conservative fallback distance when no obstacle placements are present.
float swoopFinishProgress(const SwoopTrackFrame &trackFrame, const std::shared_ptr<resource::Layout> &layout) {
    glm::vec3 frameForward {-glm::sin(trackFrame.facing), glm::cos(trackFrame.facing), 0.0f};
    float maxObstacleProgress = 0.0f;
    if (layout) {
        for (const auto &obs : layout->obstacles) {
            float progress = glm::dot(obs.position - trackFrame.position, frameForward);
            maxObstacleProgress = glm::max(maxObstacleProgress, progress);
        }
    }
    return maxObstacleProgress > 0.0f ? maxObstacleProgress + kSwoopFinishMargin : kSwoopFallbackFinishProgress;
}

// Map authored LYT obstacle placements into the current track frame
// (progress = down-course distance, lateral = strafe offset). Diagnostic
// only: no damage/collision is applied in this slice. The "match" count is
// how many .are MiniGame obstacles have a same-name LYT placement.
void logSwoopObstacles(const MinigameSpec &mg, const SwoopTrackFrame &trackFrame, const resource::Layout &layout) {
    glm::vec3 fwd {-glm::sin(trackFrame.facing), glm::cos(trackFrame.facing), 0.0f};
    glm::vec3 right {glm::cos(trackFrame.facing), glm::sin(trackFrame.facing), 0.0f};
    size_t areMatched = 0;
    for (const auto &obs : mg.obstacles) {
        if (layout.findObstacleByName(obs.name)) {
            ++areMatched;
        }
    }
    debug(str(boost::format("swoop: lyt obstacles=%zu areObstacles=%zu matched=%zu") % layout.obstacles.size() % mg.obstacles.size() % areMatched));
    constexpr size_t kMaxObstacleDiag = 6;
    for (size_t i = 0; i < layout.obstacles.size() && i < kMaxObstacleDiag; ++i) {
        const auto &obs = layout.obstacles[i];
        glm::vec3 d {obs.position - trackFrame.position};
        float progress = glm::dot(d, fwd);
        float lateral = glm::dot(d, right);
        debug(str(boost::format("  swoopobj[%zu] name=%s pos=[%.1f,%.1f,%.1f] progress=%.1f lateral=%.1f type=obstacle") % i % obs.name % obs.position.x % obs.position.y % obs.position.z % progress % lateral));
    }
}

void logSwoopModelDiagnostics(const std::vector<std::string> &diagnostics) {
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        debug(str(boost::format("  model[%zu]=%s") % i % diagnostics[i]));
    }
}

} // namespace

void Game::openSwoopRace() {
    if (_swoopRace.isActive()) {
        _console.printLine("swoop: already running");
        return;
    }
    // The console command may be given outside a module.
    if (!_module) {
        _console.printLine("swoop: no module loaded");
        return;
    }
    auto area = _module->area();
    if (!area->hasMinigame() || area->miniGame().type != MinigameType::SwoopRace) {
        _console.printLine("swoop: current area has no swoop minigame");
        return;
    }
    auto leader = _party.getLeader();
    if (!leader) {
        _console.printLine("swoop: no party leader to anchor the race");
        return;
    }

    const auto &mg = area->miniGame();
    auto camera = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->stopMovement();

    auto bike = loadSwoopBikeModels(mg, _services, _options.graphics.drawDistance);
    if (!bike.root) {
        _console.printLine("swoop: no visible bike models loaded");
        logSwoopModelDiagnostics(bike.diagnostics);
        return;
    }
    const size_t loadedCount = 1 + bike.children.size();
    _services.scene.graphs.get(kSceneMain).addRoot(bike.root);

    // Anchor the race to the authored player track. Prefer the LYT track
    // placement (module/world space); otherwise fall back as before.
    std::shared_ptr<graphics::Model> trackModel;
    if (!mg.player.trackResRef.empty()) {
        trackModel = _services.resource.models.get(mg.player.trackResRef);
    }
    auto layout = _services.resource.layouts.get(area->name());
    std::optional<glm::vec3> lytTrackPos;
    if (layout && !mg.player.trackResRef.empty()) {
        if (auto placement = layout->findTrackByName(mg.player.trackResRef)) {
            lytTrackPos = placement->get().position;
        }
    }
    SwoopTrackFrame trackFrame = deriveSwoopTrackFrame(
        trackModel, mg.player.trackResRef, lytTrackPos, leader->position(), leader->getFacing());
    const float finishProgress = swoopFinishProgress(trackFrame, layout);

    _savedCameraType = _cameraType;
    _swoopRace.start(mg, camera, std::move(bike.root), std::move(bike.children), trackFrame.position, trackFrame.facing, finishProgress);

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(false);
    changeScreen(Screen::SwoopRace);

    // The minigame is taking ownership of the party now, so discard any actions
    // the party queued before the race. In particular the swoop entry dialogue
    // queues a pre-race walk-off (e.g. a MoveToObject to a "flee" waypoint) on
    // the player; left in place it survives the module transitions and, on
    // return, sits in front of the post-race actions the result scripts queue,
    // blocking them. This is scoped to swoop/minigame entry: ordinary module
    // loads never reach openSwoopRace, so other scripted transitions (e.g. the
    // Endar Spire Trask/Bandon cutscene) keep their queued party actions.
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->teardownActions();
        }
    }

    // Hide normal party models while the minigame actor represents the player. Restore them
    // on exit or when the return module spawns them.
    setPartyVisible(false);

    debug(str(boost::format("swoop: started type=%s track=%s models=%zu loaded=%zu camera=chase movePerSec=%.0f lataccel=%.0f camfov=%.0f")
              % minigameTypeName(mg.type)
              % mg.player.trackResRef
              % mg.player.models.size()
              % loadedCount
              % mg.movementPerSec
              % mg.lateralAccel
              % mg.cameraViewAngle));

    // Track frame: lyt-track/track-model/fallback mode and how the start frame
    // was chosen (see deriveSwoopTrackFrame).
    std::string trackLabel {mg.player.trackResRef.empty() ? std::string("<none>") : mg.player.trackResRef};
    if (trackFrame.mode == "fallback") {
        debug(str(boost::format("swoop: track=%s mode=fallback reason=%s%s")
                  % trackLabel
                  % trackFrame.reason
                  % (trackFrame.info.empty() ? std::string() : (" " + trackFrame.info))));
    } else {
        debug(str(boost::format("swoop: track=%s mode=%s %s startFacing=%.2f")
                  % trackLabel
                  % trackFrame.mode
                  % trackFrame.info
                  % trackFrame.facing));
    }

    // Movement model: track-relative progress + lateral strafe (no turning).
    debug(str(boost::format("swoop: movement=track-progress strafeOnly=yes progressAxis=trackForward lateralAxis=trackRight anim=deferred start=[%.1f,%.1f,%.1f] facing=%.2f finish=%.1f")
              % trackFrame.position.x % trackFrame.position.y % trackFrame.position.z
              % trackFrame.facing
              % finishProgress));

    // Lateral bounds chosen for the strafe (see SwoopRace::computeLateralBounds).
    debug(str(boost::format("swoop: bounds lateral=[-%.1f,+%.1f] source=%s tunnelX=[%.1f,%.1f]")
              % _swoopRace.lateralLeftBound()
              % _swoopRace.lateralRightBound()
              % _swoopRace.lateralBoundSource()
              % mg.player.tunnelXNeg
              % mg.player.tunnelXPos));

    if (layout) {
        logSwoopObstacles(mg, trackFrame, *layout);
    }

    // Print the per-model breakdown when a load failed; it is a one-shot dev
    // diagnostic, so avoid spam on the common success path.
    if (bike.anyMissing) {
        logSwoopModelDiagnostics(bike.diagnostics);
    }
}

void Game::closeSwoopRace() {
    if (!_swoopRace.isActive()) {
        debug("swoop: closeSwoopRace called but race not active");
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    auto bikeRoot = _swoopRace.bikeRoot();
    if (bikeRoot) {
        sceneGraph.removeRoot(*bikeRoot);
    }
    _swoopRace.stop();
    setPartyVisible(true);
    _cameraType = _savedCameraType;
    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);
    openInGame();
    debug("swoop: stopped (race ended, party restored, camera reset)");
}

void Game::setPartyVisible(bool visible) {
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->setVisible(visible);
        }
    }
}

void Game::exitSwoopRace() {
    // Escape / stopswoop entry point. If a lifecycle race is in progress, return
    // to the origin module; otherwise just stop the dev race in place.
    if (_swoopLifecycle.active) {
        finishSwoopLifecycle(/*success=*/true);
    } else {
        closeSwoopRace();
    }
}

void Game::finishSwoopLifecycle(bool success) {
    if (!_swoopLifecycle.active) {
        return;
    }
    // Capture and clear the session first so the upcoming module load does not
    // re-enter this path. The current module (before returning) is the race
    // module, which selects the planet-specific result contract.
    MinigameLifecycle session = _swoopLifecycle;
    _swoopLifecycle = MinigameLifecycle();
    std::string raceModule = _module ? _module->name() : "";

    // Stop the race (removes bike models, restores camera/FOV/input, screen).
    closeSwoopRace();

    // Return to the originating module using its race-end waypoint when known.
    // Otherwise restore the saved pre-race position.
    const auto returnWaypoint = swoopReturnWaypoint(raceModule);
    if (!returnWaypoint.empty()) {
        debug(str(boost::format("swoop: return waypoint=%s") % returnWaypoint));
        loadModule(session.originModule, returnWaypoint);
    } else {
        loadModule(session.originModule);
        if (session.haveOrigin) {
            if (auto mod = _module) {
                if (auto area = mod->area()) {
                    if (auto leader = _party.getLeader()) {
                        leader->setPosition(session.originPosition);
                        leader->setFacing(session.originFacing);
                        area->determineObjectRoom(*leader);
                        area->onPartyLeaderMoved(/*roomChanged=*/true);
                    }
                }
            }
        }
    }

    if (success) {
        applySwoopForcedSuccessResult(raceModule);
    }

    debug(str(boost::format("swoop: finished forcedSuccess=%s returning=%s")
              % (success ? "yes" : "no")
              % session.originModule));
}

std::string Game::swoopReturnWaypoint(const std::string &raceModule) const {
    // Return Taris races to the mechanic waypoint inside the post-race trigger.
    // Other planets use the saved pre-race position until their return routes are wired.
    if (boost::iequals(raceModule, "tar_m03mg")) {
        return "tar03_wpmechanic";
    }
    return "";
}

void Game::applyTarisForcedWinningTime() {
    // Read the current heat's time-to-beat in MIN*10000 + SEC*100 + MSEC units.
    // The winning time must remain above 25 because the next target is the player
    // time minus 25. A zero player time would make the next target negative.
    int beatMin  = getGlobalNumber("TAR_SWOOP_MIN_BEAT");
    int beatSec  = getGlobalNumber("TAR_SWOOP_SEC_BEAT");
    int beatMsec = getGlobalNumber("TAR_SWOOP_MSEC_BEAT");
    int beatTotal = beatMin * 10000 + beatSec * 100 + beatMsec;

    // Choose a winning time while keeping the next target positive. Use a 50-unit
    // margin when there is room, a one-unit margin for low targets, and the first
    // heat's default winning time for degenerate targets.
    static constexpr int kMargin = 50;
    static constexpr int kMinSafe = 26;                     // next beat = playerTotal - 25 > 0
    static constexpr int kMinSafePlayerTime = 100;          // floor for the comfortable-margin branch
    static constexpr int kNormalThreshold = kMinSafePlayerTime + kMargin; // 150
    static constexpr int kFallback = 3793;                  // k_ptar_racefirst heat-1 beat (3843) - 50

    int playerTotal;
    if (beatTotal > kNormalThreshold) {
        playerTotal = beatTotal - kMargin;
    } else if (beatTotal > 25) {
        playerTotal = std::max(beatTotal - 1, kMinSafe);
    } else {
        playerTotal = kFallback;
    }

    int playerMin  = playerTotal / 10000;
    int playerSec  = (playerTotal % 10000) / 100;
    int playerMsec = playerTotal % 100;
    setGlobalNumber("TAR_SWOOP_MIN",  playerMin);
    setGlobalNumber("TAR_SWOOP_SEC",  playerSec);
    setGlobalNumber("TAR_SWOOP_MSEC", playerMsec);

    _console.printLine(str(boost::format(
        "swoop: result forcedSuccess=yes planet=taris TAR_SWOOP_RUN=1"
        " beat=%d:%d.%d time=%d:%d.%d margin=%d") %
        beatMin % beatSec % beatMsec %
        playerMin % playerSec % playerMsec %
        (beatTotal - playerTotal)));
}

void Game::applySwoopForcedSuccessResult(const std::string &raceModule) {
    // Set the race-state globals consumed by the post-race trigger. The trigger
    // checks TAR_SWOOP_RUN, compares the recorded time against TAR_SWOOP_*_BEAT,
    // updates the result and race counter, and starts the post-race conversation.
    // Leave those result and scene updates to the trigger; only record a winning
    // time and mark the run complete here. Other planets are not yet wired.
    if (!boost::iequals(raceModule, "tar_m03mg")) {
        return;
    }
    setGlobalBoolean("TAR_SWOOP_RUN", true);
    applyTarisForcedWinningTime();
}

void Game::openTurret() {
    if (_turret.isActive()) {
        _console.printLine("turret: already running");
        return;
    }
    if (!_module || !_module->area()) {
        _console.printLine("turret: no module loaded");
        return;
    }
    auto area = _module->area();
    if (!area->hasMinigame() || area->miniGame().type != MinigameType::Turret) {
        _console.printLine("turret: current area has no turret minigame");
        return;
    }

    const auto &mg = area->miniGame();
    auto camera = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    if (camera) {
        camera->stopMovement();
    }

    _savedCameraType = _cameraType;
    if (!_turret.start(mg, camera, area->name())) {
        _console.printLine("turret: failed to start (see log)");
        return;
    }

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(true);
    changeScreen(Screen::Turret);

    // Every started session is a lifecycle session, so a win or a loss is
    // consumed the same way however the turret was entered. A session started
    // in place - the startturret developer command, or any entry that did not
    // come from a module transition - captures no origin; finishing one falls
    // through to the authored return module when the turret area names one and
    // otherwise leaves the player where they are. A transition-driven entry
    // overwrites this in onModuleLoaded with the origin it captured.
    _turretLifecycle = MinigameLifecycle();
    _turretLifecycle.active = true;

    // The minigame owns the party now; drop any actions queued before entry so
    // they do not survive the module transitions (mirrors the swoop entry).
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->teardownActions();
        }
    }

    // The turret actor represents the player while the minigame runs. Restore normal party
    // visibility on exit.
    setPartyVisible(false);

    debug(str(boost::format("turret: started track=%s anchor=%s models=%zu banks=%zu enemies=%zu hp=%d camfov=%.0f clip=[%.2f,%.0f]")
              % mg.player.trackResRef
              % _turret.anchorSource()
              % mg.player.models.size()
              % _turret.gunBankCount()
              % _turret.enemyCount()
              % _turret.hitPoints()
              % mg.cameraViewAngle
              % mg.nearClip
              % mg.farClip));
    debug(str(boost::format("turret: hud gauge=%s radar=%s healthState=%d(%s) heading=%d contacts=%zu radarChannels=%zu alarm=%d")
              % (_turret.haveHealthHud() ? "mgf_hud02" : "<missing>")
              % (_turret.haveRadarHud() ? "mgf_hud01" : "<missing>")
              % _turret.healthState()
              % turretHealthAnimation(_turret.healthState())
              % _turret.headingState()
              % _turret.contactsLive()
              % _turret.radarChannelCount()
              % static_cast<int>(_turret.alarmActive())));
    debug(str(boost::format("turret: camera mount=%s hook=%s eyeOffset=[%.3f,%.3f,%.3f] targetOffset=[%.1f,%.1f,%.1f] rotate=%d")
              % (mg.player.cameraResRef.empty() ? "<none>" : mg.player.cameraResRef)
              % (_turret.haveCameraHook() ? "camerahook" : "<missing>")
              % _turret.cameraHookOffset().x
              % _turret.cameraHookOffset().y
              % _turret.cameraHookOffset().z
              % mg.player.targetOffset.x
              % mg.player.targetOffset.y
              % mg.player.targetOffset.z
              % static_cast<int>(mg.player.cameraRotate)));
    debug(str(boost::format("turret: aim pitch=[%.1f,%.1f]%s yaw=[%.1f,%.1f]%s authoredStart=[%.1f,%.1f,%.1f] startPitch=%.1f startYaw=%.1f")
              % glm::degrees(_turret.aim().minPitch())
              % glm::degrees(_turret.aim().maxPitch())
              % (_turret.aim().pitchBounded() ? "" : " (infinite)")
              % glm::degrees(_turret.aim().minYaw())
              % glm::degrees(_turret.aim().maxYaw())
              % (_turret.aim().yawBounded() ? "" : " (infinite)")
              % mg.player.startOffset.x
              % mg.player.startOffset.y
              % mg.player.startOffset.z
              % glm::degrees(_turret.aim().startPitch())
              % glm::degrees(_turret.aim().startYaw())));
}

void Game::closeTurret() {
    if (!_turret.isActive()) {
        debug("turret: closeTurret called but turret not active");
        return;
    }
    _turret.stop();
    setPartyVisible(true);
    _cameraType = _savedCameraType;
    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);
    openInGame();
    debug("turret: stopped (party restored, camera reset)");
}

void Game::exitTurret() {
    // Escape / stopturret entry point. If a lifecycle session is in progress,
    // return to the origin module; otherwise just stop the dev session in place.
    if (_turretLifecycle.active) {
        // A session abandoned mid-run is still InProgress; it returns to the
        // origin but is neither a win nor a loss.
        finishTurretLifecycle(_turret.outcome());
    } else if (_pendingTurret.active) {
        // Scheduled but never started: drop the request rather than leaving it
        // to fire on a later transition into the same module.
        abandonPendingTurret("cancelled");
    } else {
        closeTurret();
    }
}

void Game::returnToLifecycleOrigin(const std::string &module,
                                   bool haveOrigin,
                                   const glm::vec3 &position,
                                   float facing) {
    if (module.empty()) {
        return;
    }
    loadModule(module);
    if (!haveOrigin) {
        return;
    }
    auto mod = _module;
    if (!mod || !mod->area()) {
        return;
    }
    auto leader = _party.getLeader();
    if (!leader) {
        return;
    }
    leader->setPosition(position);
    leader->setFacing(facing);
    mod->area()->determineObjectRoom(*leader);
    mod->area()->onPartyLeaderMoved(/*roomChanged=*/true);
}

void Game::abandonPendingTurret(const std::string &reason) {
    if (!_pendingTurret.active) {
        return;
    }
    PendingTurretRequest request = _pendingTurret;
    _pendingTurret = PendingTurretRequest();
    _console.printLine(str(boost::format("turret: lifecycle aborted (%s), returning to origin=%s")
                           % reason % request.originModule));
    returnToLifecycleOrigin(request.originModule,
                            request.haveOrigin,
                            request.originPosition,
                            request.originFacing);
}

void Game::finishTurretLifecycle(Turret::Outcome outcome) {
    // Repeated calls are no-ops: the session is cleared below before anything
    // else runs, so neither the return nor the completion state can be emitted
    // twice for one session.
    if (!_turretLifecycle.active) {
        return;
    }
    // Capture and clear the session first so the upcoming module load does not
    // re-enter this path. The current module (before returning) is the turret
    // module, which selects the return contract.
    MinigameLifecycle session = _turretLifecycle;
    _turretLifecycle = MinigameLifecycle();
    std::string turretModule = _module ? _module->name() : "";

    closeTurret();

    // Use the turret end-script destination when known; otherwise return
    // to the module where the session started.
    std::string returnModule = game::turretReturnModule(turretModule, session.originModule);
    if (returnModule.empty()) {
        // Nothing authored and nothing captured: stay put rather than schedule
        // a transition to an empty module name.
        debug("turret: no return module, staying put");
        applyTurretResult(turretModule, outcome);
        return;
    }
    bool returningToOrigin = boost::iequals(returnModule, session.originModule);
    returnToLifecycleOrigin(returnModule,
                            returningToOrigin && session.haveOrigin,
                            session.originPosition,
                            session.originFacing);

    applyTurretResult(turretModule, outcome);

    debug(str(boost::format("turret: finished outcome=%s returning=%s")
              % turretOutcomeName(outcome)
              % returnModule));
}

void Game::applyTurretResult(const std::string &turretModule, Turret::Outcome outcome) {
    // A victory in M12ab clears the remaining-fighters count and marks the turret
    // sequence complete. Defeat or abandonment leaves the sequence outstanding.
    if (!boost::iequals(turretModule, "m12ab")) {
        return;
    }
    if (!turretSessionSucceeded(outcome)) {
        _console.printLine(str(boost::format(
            "turret: result module=m12ab outcome=%s (no completion state written)")
            % turretOutcomeName(outcome)));
        return;
    }
    setGlobalNumber("ebo_num_fighters", 0);
    setGlobalBoolean("ebo_turret_done", true);
    _console.printLine(
        "turret: result module=m12ab outcome=won ebo_turret_done=1 ebo_num_fighters=0");
}

void Game::openInGameMenu(InGameMenuTab tab) {
    setCursorType(CursorType::Default);
    // The menus end the choice of a party member to use an item on.
    cancelPartyItemTarget();
    switch (tab) {
    case InGameMenuTab::Equipment:
        _inGame->openEquipment();
        break;
    case InGameMenuTab::Inventory:
        _inGame->openInventory();
        break;
    case InGameMenuTab::Character:
        _inGame->openCharacter();
        break;
    case InGameMenuTab::Abilities:
        _inGame->openAbilities();
        break;
    case InGameMenuTab::Party:
        _inGame->openPartySelection();
        break;
    case InGameMenuTab::Messages:
        _inGame->openMessages();
        break;
    case InGameMenuTab::Journal:
        _inGame->openJournal();
        break;
    case InGameMenuTab::Map:
        _inGame->openMap();
        break;
    case InGameMenuTab::Options:
        _inGame->openOptions();
        break;
    default:
        break;
    }
    changeScreen(Screen::InGameMenu);
}

void Game::openContainer(const std::shared_ptr<Object> &container) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _container->open(container);
    changeScreen(Screen::Container);
}

// A container closed from outside its screen leaves everything in it to the
// controlled creature.
void Game::closeContainer(const Object &container) {
    if (_screen == Screen::Container && _container->displays(container)) {
        _container->closeTakingAll();
    }
}

void Game::openPartySelection(const PartySelectionContext &ctx) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _partySelect->prepare(ctx);
    changeScreen(Screen::PartySelection);
}

bool Game::isPartySelectionRealized(
    const std::vector<int> &selectedNpcs) const {
    if (!_module || !_module->area()) {
        return false;
    }
    const auto &area = *_module->area();
    std::array<bool, Party::kMaxNpcCount> requested {};
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        if (!_party.isRosterIdentityValid(identity) || requested[npc]) {
            return false;
        }
        requested[npc] = true;
    }

    std::array<bool, Party::kMaxNpcCount> realized {};
    for (const auto &member : _party.members()) {
        if (member.npc == kNpcPlayer) {
            continue;
        }
        const RosterIdentity identity {RosterKind::Npc, member.npc};
        if (!_party.isRosterIdentityValid(identity) ||
            !requested[member.npc] || realized[member.npc] ||
            !member.creature) {
            return false;
        }
        auto bound = _party.rosterCreature(identity);
        if (bound != member.creature ||
            !isRuntimeObjectLive(*bound) ||
            area.isObjectPendingDestruction(*bound) ||
            !area.isObjectResident(*bound)) {
            return false;
        }
        realized[member.npc] = true;
    }

    return realized == requested;
}

bool Game::reconcilePartySelection(
    const std::vector<int> &selectedNpcs) {
    if (!_module || !_module->area()) {
        warn("Party selection: no active Area");
        return false;
    }

    std::array<bool, Party::kMaxNpcCount> requested {};
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        if (!_party.isRosterIdentityValid(identity) || requested[npc] ||
            !_party.isRosterAvailable(identity)) {
            warn("Party selection: NPC is not available: " +
                 std::to_string(npc));
            return false;
        }
        requested[npc] = true;
    }
    if (isPartySelectionRealized(selectedNpcs)) {
        return true;
    }

    auto area = _module->area();

    // A queued representation remains live and Area-resident until the next
    // update, but it cannot satisfy a selection which must survive that
    // update. Drop only its exact roster binding so detached Party state can
    // materialize a new incarnation. The queued destruction remains aimed at
    // the obsolete object and cannot clear the replacement binding.
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        auto bound = _party.rosterCreature(identity);
        if (bound &&
            (!isRuntimeObjectLive(*bound) ||
             area->isObjectPendingDestruction(*bound))) {
            _party.clearRosterCreature(identity, bound.get());
        }
    }

    const auto currentMembers = _party.members();
    bool leadHandedBack = false;
    for (const auto &member : currentMembers) {
        // The actor standing in for the player character is not a follower,
        // and a follower whose creature was destroyed is not on the screen to
        // be deselected: it keeps its place.
        if (member.creature && member.npc != kNpcPlayer && member.npc != _party.controlledNpc() &&
            (member.npc < 0 ||
             member.npc >= static_cast<int>(requested.size()) ||
             !requested[member.npc])) {
            // In TSL the player character takes the lead back before the
            // first deselected companion leaves.
            if (isTSL() && !leadHandedBack) {
                _party.leadWithPlayerCharacter();
                leadHandedBack = true;
            }
            // A deselected companion leaves the party as any removed member
            // does: its effects are cleared, its record saved and its puppet
            // dismissed. Its creature stays where it stands.
            _party.removeMember(member.npc);
        }
    }

    // The party keeps its controlled character, or the place of one no longer
    // there, and its remaining companions where they stand; a selected
    // companion not travelling with it joins at the end.
    for (int npc : selectedNpcs) {
        // A follower whose creature was destroyed cannot join again.
        if (_party.isMember(npc) || _party.isFollower(npc)) {
            continue;
        }
        auto member = _party.getAvailableMember(npc, true);
        if (member) {
            _party.spawnIntoPlayerFaction(*member);
            // A companion newly brought in drops its actions, commandable or not.
            const bool kept = std::any_of(currentMembers.begin(), currentMembers.end(),
                                          [npc](const Party::Member &current) { return current.npc == npc; });
            if (!kept) {
                member->clearAllActions(true, true);
            }
        }
        if (!member || !_party.addMember(npc, member)) {
            warn("Party selection: could not realize NPC: " +
                 std::to_string(npc));
            return false;
        }
        // A companion joining takes up the alignment its influence gives.
        member->recomputeInfluenceAlignment();
    }

    area->repositionParty();
    return isPartySelectionRealized(selectedNpcs);
}

void Game::openSaveLoad(SaveLoadMode mode) {
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _saveLoad->setMode(mode);
    _saveLoad->refresh();
    changeScreen(Screen::SaveLoad);
}

bool Game::canOpenGalaxyMapFrom(Screen screen) {
    switch (screen) {
    case Screen::None:
    case Screen::InGame:
    case Screen::InGameMenu:
    case Screen::Conversation:
        return true;
    default:
        // Every other screen owns the whole display and has somewhere of its
        // own to return to. Taking it over would strand it.
        return false;
    }
}

void Game::openGalaxyMap(int initialPlanet) {
    if (!canOpenGalaxyMapFrom(_screen)) {
        return;
    }
    if (_galaxyMap && _galaxyMap->isRunningTravelScript()) {
        // The travel script this panel dispatched must not reopen it.
        return;
    }
    if (!_galaxyMap) {
        _galaxyMap = tryLoadGUI<GalaxyMap>();
    }
    if (!_galaxyMap) {
        // A panel that will not load must not take the screen away from
        // whatever is on it.
        return;
    }
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    // The panel decides what the routine's planet means: K2 has to record
    // where the party already is before anything can move the selection.
    _galaxyMap->prepare(initialPlanet);
    changeScreen(Screen::GalaxyMap);
}

void Game::serializePazaakPartyTable(resource::Gff &ptGff) const {
    auto replaceField = [&ptGff](resource::Gff::Field replacement) {
        auto &fields = ptGff.fields();
        auto found = std::find_if(fields.begin(), fields.end(), [&replacement](const auto &field) {
            return field.label == replacement.label;
        });
        if (found == fields.end()) {
            fields.push_back(std::move(replacement));
        } else {
            *found = std::move(replacement);
        }
    };

    replaceField(resource::Gff::Field::newDword(
        "PT_GOLD",
        static_cast<uint32_t>(std::max(0, _party.gold()))));
    if (!_party.hasValidPazaakData()) {
        return;
    }

    // Only the entries the running title actually stores are written back.
    std::vector<std::shared_ptr<resource::Gff>> cardEntries;
    const auto &savedCounts = _party.pazaakCardCounts();
    for (size_t i = 0; i < _party.pazaakCardCount(); ++i) {
        int count = savedCounts[i];
        cardEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newByte(
                    "PT_PAZAAKCOUNT",
                    static_cast<uint32_t>(count)))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZAAKCARDS",
        std::move(cardEntries)));

    std::vector<std::shared_ptr<resource::Gff>> sideEntries;
    for (int cardId : _party.pazaakSideDeck()) {
        sideEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newInt(
                    "PT_PAZSIDECARD",
                    cardId))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZSIDELIST",
        std::move(sideEntries)));
}

bool Game::playPazaak(
    int opponentDeck,
    std::string continuationScript,
    int maximumWager,
    bool tutorialRequested,
    const std::shared_ptr<Object> &opponent) {

    if (!opponent) {
        return false;
    }

    PazaakSessionParams params;
    params.opponentDeck = opponentDeck;
    params.continuationScript = std::move(continuationScript);
    params.maximumWager = maximumWager;
    params.tutorialRequested = tutorialRequested;
    params.opponentId = opponent->id();
    params.opponentName = opponent->name().empty() ? opponent->tag() : opponent->name();
    // TSL shows a creature opponent's name with its actions hidden, resolved
    // for the opponent.
    if (auto creature = std::dynamic_pointer_cast<Creature>(opponent); creature && isTSL()) {
        params.opponentName = substituteLogTokens(std::move(params.opponentName), *creature);
    }

    // A match always plays with the cards the player actually owns, read
    // from PARTYTABLE.res. Only the developer command uses temporary cards.
    if (!_party.hasValidPazaakData()) {
        error("Unable to start Pazaak: PARTYTABLE.res has no valid Pazaak data");
        return false;
    }
    int cardTypes = static_cast<int>(
        (isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount) - 1);
    std::array<std::optional<size_t>, Party::kMaxPazaakCardCount> collectionIndex;
    const auto &counts = _party.pazaakCardCounts();
    size_t ownedCards = 0;
    for (int cardId = 0; cardId < cardTypes; ++cardId) {
        if (counts[cardId] == 0) {
            continue;
        }
        auto definition = getPazaakCardDefinition(cardId, isTSL());
        if (!definition) {
            error("Unable to start Pazaak: invalid player collection card ID");
            return false;
        }
        collectionIndex[cardId] = params.collection.size();
        params.collection.push_back(
            {*definition, static_cast<size_t>(counts[cardId]), cardId});
        ownedCards += static_cast<size_t>(counts[cardId]);
    }
    if (ownedCards < pazaak::kSideDeckSize) {
        error("Unable to start Pazaak: player owns fewer than ten side-deck cards");
        return false;
    }

    const auto &savedSideDeck = _party.pazaakSideDeck();
    if (std::all_of(savedSideDeck.begin(), savedSideDeck.end(), [](int id) {
            return id >= 0;
        })) {
        for (int cardId : savedSideDeck) {
            if (cardId >= cardTypes || !collectionIndex[cardId]) {
                error("Unable to start Pazaak: saved side deck is not owned");
                return false;
            }
            params.initialChosenCards.push_back(*collectionIndex[cardId]);
        }
    }

    if (_pazaakOpponentDeckOverride) {
        params.opponentSideDeck = _pazaakOpponentDeckOverride;
    } else {
        try {
            auto decks = _services.resource.twoDas.get("pazaakdecks");
            if (!decks) {
                error("Unable to start Pazaak: pazaakdecks.2da is missing");
                return false;
            }
            params.opponentSideDeck = loadPazaakOpponentDeck(*decks, opponentDeck, isTSL());
        } catch (const std::exception &e) {
            error("Unable to read pazaakdecks.2da: " + std::string(e.what()));
            return false;
        }
        if (!params.opponentSideDeck) {
            error("Unable to start Pazaak: invalid opponent deck row in pazaakdecks.2da");
            return false;
        }
    }
    return startPazaakFlow(std::move(params), false);
}

bool Game::startDevelopmentPazaak(std::string opponentName, int maximumWager) {
    PazaakSessionParams params;
    params.opponentDeck = 0;
    params.maximumWager = maximumWager;
    params.opponentName = opponentName.empty() ? "Pazaak Opponent" : std::move(opponentName);
    // The developer route never touches save-owned cards or credits: it uses a
    // temporary, title-appropriate collection and opponent deck only.
    if (isTSL()) {
        params.collection = PazaakSession::makeDebugCollection(true);
        params.opponentSideDeck = PazaakSession::makeDebugOpponentSideDeck(true);
        // Deterministic showcase deck covering every KotOR II family. The first
        // four entries become the opening hand: a Value Change card, a
        // sign-selectable card, a fixed card and a non-switchable special.
        params.initialChosenCards = PazaakSession::specialCardShowcaseSelection();
        _pazaakShowcaseHands = true;
    } else {
        params.collection = PazaakSession::makeDebugCollection(false);
        params.opponentSideDeck = PazaakSession::makeDebugOpponentSideDeck(false);
    }
    return startPazaakFlow(std::move(params), true);
}

bool Game::startPazaakFlow(
    PazaakSessionParams params,
    bool developmentLaunch) {

    if (_pazaakSession) {
        return false;
    }

    _pazaakOriginScreen = _screen;
    _pazaakDevelopmentLaunch = developmentLaunch;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakOpponentEventElapsed = 0.0f;
    params.availableCredits = _party.gold();
    params.paceAutomaticDraws = _pazaakPaceAutomaticDraws;
    if (auto player = _party.player()) {
        params.playerName = player->name();
    }

    auto playerSelector = _pazaakPlayerHandSelector
                              ? _pazaakPlayerHandSelector
                              : (_pazaakShowcaseHands
                                     ? showcasePazaakHandSelector()
                                     : PazaakSession::HandSelector(randomPazaakHandSelection));
    auto opponentSelector = _pazaakOpponentHandSelector
                                ? _pazaakOpponentHandSelector
                                : PazaakSession::HandSelector(randomPazaakHandSelection);
    auto mainDeckFactory = _pazaakMainDeckFactory
                               ? _pazaakMainDeckFactory
                               : PazaakSession::MainDeckFactory(randomPazaakMainDeck);
    pazaak::Participant firstParticipant =
        randomInt(0, 1) == 0 ? pazaak::Participant::One : pazaak::Participant::Two;
    auto firstParticipantSelector = _pazaakFirstParticipantSelector
                                        ? _pazaakFirstParticipantSelector
                                        : PazaakSession::FirstParticipantSelector(
                                              [firstParticipant](size_t setIndex) {
                                                  bool useInitial = setIndex % 2 == 0;
                                                  if (useInitial) {
                                                      return firstParticipant;
                                                  }
                                                  return firstParticipant == pazaak::Participant::One
                                                             ? pazaak::Participant::Two
                                                             : pazaak::Participant::One;
                                              });

    try {
        _pazaakSession = std::make_unique<PazaakSession>(
            std::move(params),
            std::move(playerSelector),
            std::move(opponentSelector),
            std::move(mainDeckFactory),
            std::move(firstParticipantSelector));
    } catch (const std::exception &e) {
        error("Unable to create Pazaak session: " + std::string(e.what()));
        releasePazaakFlow(true);
        return false;
    }

    if (!loadPazaakGUIs()) {
        releasePazaakFlow(true);
        return false;
    }

    if (_module && _module->area()) {
        stopMovement();
    }
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    if (_pazaakSession->screen() == PazaakFlowScreen::Wager) {
        if (_pazaakWager) {
            _pazaakWager->refresh();
        }
        changeScreen(Screen::PazaakWager);
    } else {
        showPazaakSetup();
    }
    return true;
}

bool Game::loadPazaakGUIs() {
    if (_pazaakGuiLoadOverride) {
        _pazaakGUIsReady = _pazaakGuiLoadOverride();
        return _pazaakGUIsReady;
    }

    _pazaakWager = tryLoadGUI<PazaakWagerGUI>();
    _pazaakSetup = tryLoadGUI<PazaakSetupGUI>();
    _pazaakBoard = tryLoadGUI<PazaakBoardGUI>();
    _pazaakGUIsReady = _pazaakWager && _pazaakSetup && _pazaakBoard;
    if (!_pazaakGUIsReady) {
        _pazaakWager.reset();
        _pazaakSetup.reset();
        _pazaakBoard.reset();
    }
    return _pazaakGUIsReady;
}

void Game::showPazaakSetup() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Setup) {
        return;
    }
    if (_pazaakSetup) {
        _pazaakSetup->refresh();
    }
    changeScreen(Screen::PazaakSetup);
}

void Game::showPazaakBoard() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Board ||
        !_pazaakSession->match()) {
        return;
    }
    if (!_pazaakDevelopmentLaunch && !_pazaakSelectionPersisted) {
        Party::PazaakSideDeck selected;
        const auto &collection = _pazaakSession->collection();
        const auto &chosen = _pazaakSession->chosenCards();
        if (chosen.size() != selected.size()) {
            error("Unable to persist Pazaak side deck: selection is incomplete");
            return;
        }
        for (size_t i = 0; i < chosen.size(); ++i) {
            if (chosen[i] >= collection.size() ||
                collection[chosen[i]].persistentId < 0) {
                error("Unable to persist Pazaak side deck: invalid collection mapping");
                return;
            }
            selected[i] = collection[chosen[i]].persistentId;
        }
        _party.setPazaakSideDeck(std::move(selected));
        _pazaakSelectionPersisted = true;
    }
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    changeScreen(Screen::PazaakBoard);
    completePazaakIfReady();
}

void Game::cancelPazaak() {
    if (!_pazaakSession || _pazaakSession->screen() == PazaakFlowScreen::Board) {
        return;
    }
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    releasePazaakFlow(true);
    if (developmentLaunch) {
        _console.printLine("pazaak: development match cancelled");
    }
}

void Game::abortPazaak() {
    if (!_pazaakSession) {
        return;
    }
    releasePazaakFlow(true);
}

void Game::completePazaakIfReady() {
    if (!_pazaakSession ||
        !_pazaakSession->completedResult() ||
        _pazaakSession->presentationPending()) {
        return;
    }
    finishPazaak(*_pazaakSession->completedResult());
}

void Game::finishPazaak(PazaakCompletedResult result) {
    if (!_pazaakSession) {
        return;
    }

    std::string continuation(_pazaakSession->continuationScript());
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    int wager = _pazaakSession->wager();
    _lastPazaakResult = result;

    if (!developmentLaunch && !_pazaakSettlementApplied) {
        // A wager is won onto the party's credits or lost from them, down to
        // none; the change the wager makes, if any, is reported in the status
        // summary.
        if (wager > 0) {
            const int before = _party.gold();
            const int after = result == PazaakCompletedResult::PlayerWon ? before + wager : std::max(before - wager, 0);
            if (after != before) {
                if (after > before) {
                    _party.giveGold(after - before);
                } else {
                    _party.takeGold(before - after);
                }
                submitStatusSummary(StatusSummaryCategory::Credits, after - before);
            }
        }
        _pazaakSettlementApplied = true;
    }

    // Release ownership before external script execution so re-entrant or
    // repeated completion cannot invoke the continuation twice.
    releasePazaakFlow(true);

    if (developmentLaunch) {
        switch (result) {
        case PazaakCompletedResult::PlayerWon:
            _console.printLine("pazaak: development match completed - player won");
            break;
        case PazaakCompletedResult::OpponentWon:
            _console.printLine("pazaak: development match completed - opponent won");
            break;
        case PazaakCompletedResult::PlayerForfeited:
            _console.printLine("pazaak: development match completed - player forfeited");
            break;
        }
    }

    // The end script runs without a caller, whatever became of the opponent.
    if (continuation.empty()) {
        return;
    }
    if (_pazaakContinuationOverride) {
        _pazaakContinuationOverride(continuation, script::kObjectInvalid);
    } else if (_scriptRunner) {
        _scriptRunner->run(continuation, script::kObjectInvalid);
    }
}

Game::Screen Game::safePazaakOriginScreen() const {
    switch (_pazaakOriginScreen) {
    case Screen::PazaakWager:
    case Screen::PazaakSetup:
    case Screen::PazaakBoard:
        return _module ? Screen::InGame : Screen::None;
    case Screen::Conversation:
        // The dialogue may finish after its action script opens Pazaak.
        // Returning to the world cannot strand an ended conversation GUI.
        return _module ? Screen::InGame : Screen::None;
    default:
        return _pazaakOriginScreen;
    }
}

void Game::releasePazaakFlow(bool restoreOrigin) {
    Screen restore = safePazaakOriginScreen();
    _pazaakSession.reset();
    _pazaakWager.reset();
    _pazaakSetup.reset();
    _pazaakBoard.reset();
    _pazaakGUIsReady = false;
    _pazaakDevelopmentLaunch = false;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakShowcaseHands = false;
    _pazaakOpponentEventElapsed = 0.0f;
    if (restoreOrigin) {
        changeScreen(restore);
    }
    _pazaakOriginScreen = Screen::None;
}

void Game::openLevelUp() {
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }

    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _charGen->startLevelUp();
    changeScreen(Screen::CharacterGeneration);
}

static constexpr int kLevelUpAvailableStrRef = 1438;
static constexpr char kLevelUpDialogFiredGlobal[] = "000_Level_Dlg_Fired";
static constexpr char kLevelUpDialogScript[] = "k_level_dlg";

void Game::notifyLevelUpAvailable(const Creature &creature, bool newlyAvailable) {
    if (!_party.isMember(creature)) {
        return;
    }
    // Only the controlled creature is told.
    if (&creature != _party.getLeader().get()) {
        return;
    }
    if (newlyAvailable) {
        addFeedbackMessage(kLevelUpAvailableStrRef);
    }
    // TSL introduces levelling up the first time the player character can,
    // outside a conversation.
    if (isTSL() && creature.isPlayerCreated() && !isConversationActive()) {
        runLevelUpDialogOnce();
    }
}

void Game::runLevelUpDialogOnce() {
    if (getGlobalNumber(kLevelUpDialogFiredGlobal) != 0) {
        return;
    }
    setGlobalNumber(kLevelUpDialogFiredGlobal, 1);
    _scriptRunner->run(kLevelUpDialogScript);
}

/** The name character generation loads under. */
static constexpr char kClassSelectionLoadScreen[] = "classsel";

void Game::startCharacterGeneration() {
    resetGame();
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }
    // Character generation loads as the class selection does, and every new
    // character starts from a new class selection.
    // TSL's character generation plays its loading screen's music throughout;
    // KotOR's plays its menu music once the panel is up.
    if (isTSL()) {
        setLoadMusicForModule(kClassSelectionLoadScreen);
    }
    withLoadingScreen(kClassSelectionLoadScreen, [this]() {
        _charGen->startCreation();
        _loadScreen->setProgress(100);
        render();
        if (!isTSL()) {
            playMusic(_charGen->musicResRef());
        }
        changeScreen(Screen::CharacterGeneration);
    });
}

void Game::startDialog(const std::shared_ptr<Object> &owner, const std::string &resRef,
                       GlobalFade::DialogTicket admission, const std::shared_ptr<Object> &listener) {
    if (_captureHUDPresentation) {
        return;
    }
    if (admission && !_globalFade.isCurrentDialog(admission)) {
        return;
    }
    std::shared_ptr<resource::Dialog> dialog;
    try {
        if (_services.resource.gffs.get(resRef, ResType::Dlg)) {
            dialog = _services.resource.dialogs.get(resRef);
        }
    } catch (const std::exception &e) {
        warn("Game: conversation load failed: " + resRef + ": " + e.what());
        _globalFade.finishDialog(admission);
        _dialogUnderWay = false;
        return;
    }
    if (!dialog) {
        warn("Game: conversation not found or invalid: " + resRef);
        _globalFade.finishDialog(admission);
        _dialogUnderWay = false;
        return;
    }

    bool computerConversation = dialog->conversationType == ConversationType::Computer;
    auto conversation = computerConversation ? _computer.get() : static_cast<Conversation *>(_dialog.get());
    if (!conversation) {
        _globalFade.finishDialog(admission);
        _dialogUnderWay = false;
        return;
    }

    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    changeScreen(Screen::Conversation);
    _dialogUnderWay = true;

    // A computer conversation leaves everyone's stealth alone.
    if (!computerConversation && (!isTSL() || !_keepStealthInDialog)) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(owner)) {
            creature->setStealthMode(false);
        }
        if (auto leader = _party.getLeader()) {
            leader->setStealthMode(false);
        }
        for (int i = 0; i < _party.getSize(); ++i)
            if (auto member = _party.getMember(i)) {
                member->setStealthMode(false);
            }
    }
    _conversation = conversation;
    _conversation->setAutoSkip(&_conversationAutoSkip);
    _conversation->start(dialog, owner, std::move(admission), listener);
}

void Game::resumeConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->resume();
}

// Conversations are paused and resumed by callers that are still standing.
static bool canHoldConversation(Party &party, Object &caller) {
    if (caller.isDead()) {
        return false;
    }
    auto *creature = dyn_cast<Creature>(&caller);
    return !creature || !party.isMember(*creature) || creature->currentHitPoints() > 0;
}

void Game::pauseConversationBy(Object &caller) {
    if (!canHoldConversation(_party, caller)) {
        return;
    }
    pauseConversation();
    if (auto *creature = dyn_cast<Creature>(&caller)) {
        creature->setConversationPaused(true);
    }
}

void Game::resumeConversationBy(Object &caller) {
    if (!canHoldConversation(_party, caller)) {
        return;
    }
    resumeConversation();
    if (auto *creature = dyn_cast<Creature>(&caller)) {
        creature->setConversationPaused(false);
        creature->setStealthMode(false);
    }
}

void Game::stopConversationParticipation(const Object &object) {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->stopParticipant(object);
}

void Game::pauseConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->pause();
}

void Game::loadInGameMenus() {
    if (!_hud) {
        _hud = tryLoadGUI<HUD>();
    }
    if (!_inGame) {
        _inGame = tryLoadGUI<InGameMenu>();
    }
    if (!_dialog) {
        _dialog = tryLoadGUI<DialogGUI>();
    }
    if (!_computer) {
        _computer = tryLoadGUI<ComputerGUI>();
    }
    if (!_container) {
        _container = tryLoadGUI<ContainerGUI>();
    }
    if (!_partySelect) {
        _partySelect = tryLoadGUI<PartySelection>();
    }
}

int Game::inventoryMenuCharacter() const {
    return _screen == Screen::InGameMenu && _inGame ? _inGame->inventoryCharacter() : -1;
}

void Game::finishPostDialogCharacterSwitch() {
    if (_postDialogCharacterSwitch.empty()) {
        return;
    }
    auto companion = _postDialogCharacterSwitch.resolve();
    _postDialogCharacterSwitch.reset();
    // The lead passes to the next member, one step at a time, until the
    // companion leads; each step is a leader change of its own. A companion
    // that is gone never leads, so the lead goes once round the party.
    for (int step = 0; step < _party.getSize() && _party.getLeader() != companion; ++step) {
        _party.switchLeader(false);
    }
}

float Game::baseItemCostMultiplier(int baseItem) const {
    auto found = _baseItemCostMultipliers.find(baseItem);
    return found != _baseItemCostMultipliers.end() ? found->second : 1.0f;
}

void Game::changeScreen(Screen screen) {
    if (_screen == Screen::InGameMenu && screen != Screen::InGameMenu && _inGame) {
        _inGame->closeEquipment();
    }
    auto gui = getScreenGUI();
    if (gui) {
        gui->clearSelection();
    }
    if (_confirmPopup) {
        _confirmPopup->hide();
    }
    if (_tutorial) {
        finishTutorialWindow(false);
    }
    _screen = screen;
}

GameGUI *Game::getScreenGUI() const {
    switch (_screen) {
    case Screen::Death:
        return isTSL() ? static_cast<GameGUI *>(_deathDisplay.get()) : static_cast<GameGUI *>(_deathMessage.get());
    case Screen::MainMenu:
        return _mainMenu.get();
    case Screen::Loading:
        return _loadScreen.get();
    case Screen::CharacterGeneration:
        return _charGen.get();
    case Screen::InGame:
        return _cameraType == game::CameraType::ThirdPerson ? _hud.get() : nullptr;
    case Screen::InGameMenu:
        return _inGame.get();
    case Screen::Conversation:
        return _conversation;
    case Screen::Container:
        return _container.get();
    case Screen::PartySelection:
        return _partySelect.get();
    case Screen::SaveLoad:
        return _saveLoad.get();
    case Screen::GalaxyMap:
        return _galaxyMap.get();
    case Screen::SwoopRace:
        return nullptr; // race skeleton has no HUD yet
    case Screen::PazaakWager:
        return _pazaakWager.get();
    case Screen::PazaakSetup:
        return _pazaakSetup.get();
    case Screen::PazaakBoard:
        return _pazaakBoard.get();
    case Screen::Turret:
        return nullptr; // the turret HUD is part of the player model set
    default:
        return nullptr;
    }
}

void Game::setBarkBubbleText(std::string text, float duration) {
    _hud->barkBubble().setBarkText(text, duration);
}

void Game::addFeedbackMessage(int strRef, const std::map<int, std::string> &tokens) {
    auto text = getFeedbackText(strRef, tokens);
    // An empty line is not written.
    if (text.empty()) {
        return;
    }
    _messageLog.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, std::move(text));
}

void Game::presentCombatMessage(int strref) {
    if (_hud) {
        _hud->setCombatMessage(strref);
    }
}

void Game::cancelPartyItemTarget() {
    if (_hud) {
        _hud->cancelPartyItemTarget();
    }
}

void Game::submitStatusSummary(
    StatusSummaryCategory category,
    int amount,
    std::vector<std::string> items) {

    // A suppressed report leaves no trace; the summary option decides at
    // presentation.
    _statusSummary.submit(category, amount, std::move(items));
}

void Game::finishStatusSummaryCycle() {
    static constexpr char kAlignmentMovieScript[] = "k_align_movie";
    if (!isTSL()) {
        return;
    }
    _scriptRunner->run(kAlignmentMovieScript);
    const auto &player = *_party.player();
    if (player.isPlayerCreated() && player.isLevelUpPending()) {
        runLevelUpDialogOnce();
    }
}

int Game::getPlotXP(const std::string &plotName) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable) {
        return 0;
    }
    for (int row = 0; row < plotTable->getRowCount(); ++row) {
        if (boost::iequals(plotTable->getString(row, "label"), plotName)) {
            return plotTable->getInt(row, "xp");
        }
    }
    return 0;
}

int Game::getPlotXPByIndex(int plotIndex) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable || plotIndex < 0 || plotIndex >= plotTable->getRowCount()) {
        return 0;
    }
    return plotTable->getInt(plotIndex, "xp");
}

void Game::awardPlotXP(const std::string &plotName, int percentage) {
    if (plotName.empty() || percentage == 0) {
        return;
    }
    int baseXP = getPlotXP(plotName);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>((static_cast<int64_t>(baseXP) * percentage) / 100);
    _party.awardXP(amount, XPSource::Plot);
}

void Game::awardPlotXPByIndex(int plotIndex, float fraction) {
    if (plotIndex < 0 || fraction == 0.0f) {
        return;
    }
    int baseXP = getPlotXPByIndex(plotIndex);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>(baseXP * fraction);
    _party.awardXP(amount, XPSource::Plot);
}

void Game::onModuleSelected(const std::string &module) {
    _mainMenu->onModuleSelected(module);
}

void Game::renderHUD() {
    _hud->render();
}

CameraType Game::getConversationCamera(int &cameraId) const {
    return _conversation->getCamera(cameraId);
}

void Game::updateImGui(float dt) {
    ImGui::ShowDemoWindow(&_showImGui);
}

std::shared_ptr<Object> Game::getConsoleTargetObject() {
    auto object = getConsoleArea()->selectedObject();
    if (!object) {
        object = party().getLeader();
    }
    if (object) {
        return object;
    }
    throw std::runtime_error("No object is selected");
}

std::shared_ptr<Creature> Game::getConsoleTargetCreature() {
    if (auto object = getConsoleArea()->selectedObject()) {
        if (auto creature = dyn_cast<Creature>(object)) {
            return creature;
        }
        throw std::runtime_error("Selected object must be a creature");
    }

    return getConsoleLeader();
}

std::shared_ptr<Creature> Game::getConsoleLeader() {
    if (std::shared_ptr<Creature> leader = _party.getLeader()) {
        return leader;
    }
    throw std::runtime_error("No party leader");
}

std::shared_ptr<Area> Game::getConsoleArea() {
    std::shared_ptr<Module> mod = module();
    if (!mod) {
        throw std::runtime_error("Module is not loaded");
    }

    if (std::shared_ptr<Area> area = mod->area()) {
        return area;
    }
    throw std::runtime_error("Area is not loaded");
}

static void consoleCheckUsage(const ConsoleArgs &args,
                              size_t minArgs, size_t maxArgs,
                              std::string_view usage) {
    size_t numArgs = args.size() - 1;
    if (numArgs < minArgs || numArgs > maxArgs) {
        throw std::runtime_error(str(boost::format("Usage: %s %s") % args[0].value() % usage));
    }
}

void Game::consoleInfo(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    glm::vec3 position(object->position());

    std::stringstream ss;
    ss << std::setprecision(2) << std::fixed
       << "id=" << object->id()
       << " "
       << "tag=\"" << object->tag() << "\""
       << " "
       << "tpl=\"" << object->blueprintResRef() << "\""
       << " "
       << "pos=[" << position.x << ", " << position.y << ", " << position.z << "]";

    if (auto creature = dyn_cast<Creature>(object)) {
        ss << " "
           << "app=" << creature->appearance()
           << " "
           << "fac=" << static_cast<int>(creature->faction());
    } else if (auto placeable = dyn_cast<Placeable>(object)) {
        ss << " "
           << "app=" << placeable->appearance();
    }

    _console.printLine(ss.str());
}

void Game::consoleListGlobals(const ConsoleArgs &args) {
    auto &strings = globalStrings();
    for (auto &var : strings) {
        _console.printLine(var.first + " = " + var.second);
    }

    auto &booleans = globalBooleans();
    for (auto &var : booleans) {
        _console.printLine(var.first + " = " + (var.second ? "true" : "false"));
    }

    auto &numbers = globalNumbers();
    for (auto &var : numbers) {
        _console.printLine(var.first + " = " + std::to_string(var.second));
    }

    auto &locations = globalLocations();
    for (auto &var : locations) {
        _console.printLine(str(boost::format("%s = (%.04f, %.04f, %.04f, %.04f") %
                               var.first %
                               var.second->position().x %
                               var.second->position().y %
                               var.second->position().z %
                               var.second->facing()));
    }
}

void Game::consoleListLocals(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();

    auto &booleans = object->localBooleans();
    for (auto &var : booleans) {
        _console.printLine(std::to_string(var.first) + " -> " + (var.second ? "true" : "false"));
    }

    auto &numbers = object->localNumbers();
    for (auto &var : numbers) {
        _console.printLine(std::to_string(var.first) + " -> " + std::to_string(var.second));
    }
}

void Game::consoleListAnim(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    auto substr = args[1];

    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    std::vector<std::string> anims(model->model().getAnimationNames());
    sort(anims.begin(), anims.end());

    for (auto &anim : anims) {
        if (!substr || boost::contains(anim, substr.value())) {
            _console.printLine(anim);
        }
    }
}

void Game::consolePlayAnim(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "anim_name");
    std::string anim(args[1].value());

    auto object = getConsoleTargetObject();
    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    model->playAnimation(anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

void Game::consoleKill(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    auto effect = newEffect<DamageEffect>(
        100000,
        DamageType::Universal,
        DamagePower::Normal);
    object->applyEffect(std::move(effect), DurationType::Instant);
}

void Game::consoleAddItem(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "item_tpl [size]");
    auto object = getConsoleTargetObject();
    int stackSize = args.get<int>(2).value_or(1);
    auto receiver = _party.sharedInventoryReceiver(object);
    receiver->addItem(std::string(args[1].value()), stackSize);
}

void Game::consoleGiveXP(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    auto creature = getConsoleTargetCreature();
    int amount = args.get<int>(1).value();
    if (_party.isMember(*creature)) {
        _party.awardXP(amount, XPSource::Console);
    } else {
        creature->giveXP(amount);
    }
}

void Game::consoleGiveGold(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    _party.giveGold(args.get<int>(1).value());
    _console.printLine(str(boost::format("party gold: %d") % _party.gold()));
}

// The free camera is the first-person camera flown off the player: WASD/QZ
// move it, the mouse aims it. These commands exist so a viewpoint found by
// hand can be replayed exactly from a commands file - camstatus prints the
// line to paste - and the same commands drive other builds, which keeps
// captures comparable between them.
void Game::consoleCamera(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "free");
    if (args[1].value() != "free") {
        throw std::runtime_error("Unknown camera: " + std::string(args[1].value()));
    }
    if (_screen != Screen::InGame) {
        throw std::runtime_error("The free camera needs the in-game screen");
    }
    if (_cameraType != CameraType::FirstPerson) {
        toggleInGameCameraType();
    }
}

void Game::consoleCamPos(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setPosition({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamLook(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setLookAt({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamStatus(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    glm::vec3 pos = camera->position();
    glm::vec3 forward(-glm::sin(camera->facing()) * glm::cos(camera->pitch()),
                      glm::cos(camera->facing()) * glm::cos(camera->pitch()),
                      glm::sin(camera->pitch()));
    glm::vec3 target = pos + forward;
    std::string result = str(boost::format("camera free; campos %.6f %.6f %.6f; camlook %.6f %.6f %.6f") %
                             pos.x % pos.y % pos.z % target.x % target.y % target.z);
    _console.printLine(result);
    info(result);
}

void Game::consoleWarp(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");
    // Gallery states share an engine process for speed. A warp is their scene
    // boundary, so fixture-only HUD state must not bleed into the next image.
    _captureHUDPresentation = false;
    if (_hud) {
        _hud->clearCapturePresentation();
    }
    loadModule(std::string(args[1].value()));
}

void Game::consoleOpenMenu(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "main|equipment|equipment-items|inventory|character|abilities|party|messages|journal|map|options");

    std::string_view name(args[1].value());
    if (boost::iequals(name, "main")) {
        openMainMenu();
        return;
    }
    if (boost::iequals(name, "equipment-items")) {
        setCursorType(CursorType::Default);
        _inGame->openEquipmentItems();
        changeScreen(Screen::InGameMenu);
        return;
    }

    static const std::array<std::pair<std::string_view, InGameMenuTab>, 9> kTabs {{
        {"equipment", InGameMenuTab::Equipment},
        {"inventory", InGameMenuTab::Inventory},
        {"character", InGameMenuTab::Character},
        {"abilities", InGameMenuTab::Abilities},
        {"party", InGameMenuTab::Party},
        {"messages", InGameMenuTab::Messages},
        {"journal", InGameMenuTab::Journal},
        {"map", InGameMenuTab::Map},
        {"options", InGameMenuTab::Options},
    }};
    for (const auto &[tabName, tab] : kTabs) {
        if (boost::iequals(name, tabName)) {
            openInGameMenu(tab);
            return;
        }
    }
    throw std::runtime_error("Unknown in-game menu tab: " + std::string(name));
}

static std::string joinConsoleArgs(const ConsoleArgs &args, size_t first) {
    std::string result;
    for (size_t i = first; i < args.size(); ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += args[i].value();
    }
    return result;
}

void Game::consoleOpenCharacterGeneration(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "class|quick-or-custom|quick|portrait|name|custom|abilities|skills|feats|powers|level-up [select]");

    std::string_view screen(args[1].value());
    bool selectFirst = args.size() > 2 && boost::iequals(std::string(args[2].value()), "select");
    if (boost::iequals(screen, "class")) {
        startCharacterGeneration();
        if (_charGen) {
            _charGen->openClassSelection();
        }
        return;
    }

    if (!_module || !_party.getLeader()) {
        throw std::runtime_error("Character-generation capture screens require a loaded party");
    }
    openLevelUp();
    if (!_charGen) {
        throw std::runtime_error("Character generation GUI is unavailable");
    }
    if (boost::iequals(screen, "level-up")) {
        _charGen->openLevelUp();
        return;
    }

    Character character(_charGen->character());
    ClassType captureClass = boost::iequals(screen, "powers")
                                 ? ClassType::JediConsular
                                 : (isTSL() ? ClassType::JediGuardian : ClassType::Soldier);
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(captureClass));
    if (!clazz) {
        throw std::runtime_error("Starting class is unavailable");
    }
    character.attributes = clazz->defaultAttributes();
    _charGen->setCharacter(std::move(character));

    if (boost::iequals(screen, "quick")) {
        _charGen->startQuick();
        return;
    }
    _charGen->startCustom();
    if (boost::iequals(screen, "quick-or-custom")) {
        _charGen->openQuickOrCustom();
        return;
    }
    if (boost::iequals(screen, "portrait")) {
        _charGen->openPortraitSelection();
        return;
    }
    if (boost::iequals(screen, "name")) {
        _charGen->openNameEntry();
        return;
    }
    if (boost::iequals(screen, "custom")) {
        _charGen->openCustom();
        return;
    }
    if (boost::iequals(screen, "abilities")) {
        _charGen->openAbilities();
        return;
    }
    if (boost::iequals(screen, "skills")) {
        _charGen->openSkills();
        if (selectFirst) {
            _charGen->skills().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "feats")) {
        _charGen->openFeats();
        if (selectFirst) {
            _charGen->feats().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "powers")) {
        _charGen->openPowers();
        if (selectFirst) {
            _charGen->powers().selectFirstEntryForCapture();
        }
        return;
    }
    throw std::runtime_error("Unknown character-generation screen: " + std::string(screen));
}

void Game::consoleShowBark(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "seconds message ...");
    auto duration = args.get<float>(1);
    if (!duration || *duration <= 0.0f) {
        throw std::invalid_argument("showbark duration must be positive");
    }
    if (!_hud) {
        throw std::runtime_error("HUD is unavailable; load a module first");
    }
    setBarkBubbleText(joinConsoleArgs(args, 2), *duration);
}

void Game::consoleSkipMovie(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    _movie.reset();
}

void Game::consoleShowPopup(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "icon|none message ...");
    std::shared_ptr<Texture> icon;
    std::string_view iconResRef(args[1].value());
    if (!boost::iequals(iconResRef, "none")) {
        icon = _services.resource.textures.get(std::string(iconResRef), TextureUsage::GUI);
    }
    if (!showMessagePopup(joinConsoleArgs(args, 2), std::move(icon))) {
        throw std::runtime_error("Confirmation popup GUI is unavailable");
    }
}

void Game::closeMessagePopup() {
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->close();
    }
}

bool Game::showMessagePopup(
    const std::string &message,
    std::shared_ptr<Texture> icon,
    std::function<void()> onConfirm) {
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (!_confirmPopup) {
        return false;
    }
    // The popup takes all input, key and button releases included, so the
    // steering and mouse-look held when it opens stop now.
    stopMovement();
    _confirmPopup->show(message, std::move(icon), std::move(onConfirm));
    return true;
}

void Game::consoleSeed(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    setRandomSeed(static_cast<uint32_t>(args.get<int>(1).value()));
}

void Game::consoleGraphics(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "on|off");
    auto mode = std::string(args[1].value());
    if (boost::iequals(mode, "on")) {
        _options.graphics.sceneRender = true;
    } else if (boost::iequals(mode, "off")) {
        _options.graphics.sceneRender = false;
    } else {
        throw std::runtime_error("Expected on or off");
    }
}

void Game::consoleOpenContainer(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto leader = getConsoleLeader();
    if (!leader || !_container) {
        throw std::runtime_error("Container fixture requires a loaded module");
    }
    openContainer(leader);
}

void Game::consoleShowHUD(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[combat]");
    if (!_module || !_hud) {
        throw std::runtime_error("HUD capture fixture requires a loaded module");
    }
    bool combat = args.size() > 1 && boost::iequals(std::string(args[1].value()), "combat");
    if (args.size() > 1 && !combat) {
        throw std::runtime_error("Unknown HUD capture presentation: " + std::string(args[1].value()));
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(combat);
}

void Game::consoleShowTransition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "destination ...");
    if (!_module || !_hud) {
        throw std::runtime_error("Area-transition capture fixture requires a loaded module");
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(false);
    _hud->showTransitionCapturePresentation(joinConsoleArgs(args, 1));
}

void Game::consoleSelectDialogOption(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "index");
    if (_screen != Screen::Conversation || _conversation != _dialog.get()) {
        throw std::runtime_error("Dialog selection fixture requires an active character conversation");
    }
    _dialog->selectReplyForCapture(args.get<int>(1).value());
}

void Game::consoleShowGalleryMode(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "swoop|pazaak wager|setup|board");

    std::string_view mode(args[1].value());
    if (boost::iequals(mode, "swoop")) {
        if (!_swoopRace.isActive()) {
            openSwoopRace();
        }
        if (!_swoopRace.isActive()) {
            throw std::runtime_error("Swoop gallery fixture requires a loaded swoop minigame module");
        }
        return;
    }

    if (!boost::iequals(mode, "pazaak") || args.size() != 3) {
        throw std::runtime_error("Unknown gallery mode; expected swoop or pazaak wager|setup|board");
    }
    if (!_module) {
        throw std::runtime_error("Pazaak gallery fixture requires a loaded module");
    }

    std::string_view screen(args[2].value());
    bool showWager = boost::iequals(screen, "wager");
    bool showSetup = boost::iequals(screen, "setup");
    bool showBoard = boost::iequals(screen, "board");
    if (!showWager && !showSetup && !showBoard) {
        throw std::runtime_error("Unknown Pazaak gallery screen: " + std::string(screen));
    }

    abortPazaak();
    if (!startDevelopmentPazaak("Gallery Opponent", showWager ? 100 : 0)) {
        throw std::runtime_error("Unable to start Pazaak gallery fixture");
    }
    if (showWager || showSetup) {
        return;
    }

    for (size_t collectionIndex = 0;
         collectionIndex < _pazaakSession->collection().size() &&
         _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize;
         ++collectionIndex) {
        while (_pazaakSession->remainingCopies(collectionIndex) > 0 &&
               _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize) {
            _pazaakSession->selectCard(collectionIndex);
        }
    }
    if (!_pazaakSession->confirmSetup()) {
        throw std::runtime_error("Unable to prepare Pazaak board gallery fixture");
    }
    showPazaakBoard();
}

void Game::consoleRunScript(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "resref [kind:value ...]");

    std::string resRef(args[1].value());
    std::vector<script::Argument> vars;
    for (size_t i = 2; i < args.size(); ++i) {
        vars.push_back(script::Argument::fromString(std::string(args[i].value())));
    }

    int result = scriptRunner().run(resRef, vars);
    _console.printLine(str(boost::format("%s -> %d") % resRef % result));
}

void Game::consoleShowAABB(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowAABB(show);
}

void Game::consoleShowWalkmesh(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowWalkmesh(show);
}

void Game::consoleShowTriggers(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowTriggers(show);
}

void Game::consoleSpawnCreature(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "res [id]");

    std::string res(args[1].value());
    std::optional<uint32_t> id = args.get<uint32_t>(2);

    auto area = getConsoleArea();
    auto leader = getConsoleLeader();

    std::shared_ptr<Creature> creature;
    if (auto explicitId = args.get<uint32_t>(2)) {
        if (getObjectById(*explicitId)) {
            throw std::runtime_error("Object already exists");
        }
        std::vector<std::shared_ptr<Object>> noObsolete;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                creature = newObjectAtId<Creature>(
                    *explicitId, false, kSceneMain, *this, _services);
                if (!creature->loadFromBlueprint(res)) {
                    throw std::runtime_error("Creature template not found");
                }
            },
            []() noexcept {});
    } else {
        creature = newCreatureFromBlueprint(res);
        if (!creature) {
            throw std::runtime_error("Creature template not found");
        }
    }
    creature->setPosition(leader->position());
    creature->setFacing(leader->getFacing());
    creature->setFaction(Faction::Neutral);

    area->landObject(*creature);
    area->add(creature);
}

void Game::consoleSpawnCompanion(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 3, "res npcindex [id]");

    std::string res(args[1].value());
    int npc = args.get<int>(2).value();
    std::optional<uint32_t> id = args.get<uint32_t>(3);

    auto leader = getConsoleLeader();
    auto area = getConsoleArea();

    std::shared_ptr<Creature> companion;
    if (id) {
        if (getObjectById(id.value())) {
            throw std::runtime_error("Object already exists");
        }
        std::vector<std::shared_ptr<Object>> noObsolete;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                companion = newObjectAtId<Creature>(
                    id.value(), false, kSceneMain, *this, _services);
                if (!companion->loadFromBlueprint(res)) {
                    throw std::runtime_error("Creature template not found");
                }
            },
            []() noexcept {});
    } else {
        companion = newCreatureFromBlueprint(res);
        if (!companion) {
            throw std::runtime_error("Creature template not found");
        }
    }
    companion->setPosition(leader->position());
    companion->setFacing(leader->getFacing());
    companion->setFaction(leader->faction());

    area->landObject(*companion);
    area->add(companion);
    _party.addAvailableMember(npc, companion);
    _party.addMember(npc, companion);
}

void Game::consoleAddAvailableNpc(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "npcindex blueprint");

    int npc = args.get<int>(1).value();
    std::string blueprint(args[2].value());

    if (!_party.addAvailableMember(npc, blueprint)) {
        throw std::runtime_error("NPC is already available: " + std::to_string(npc));
    }
}

void Game::consoleSelectObjectById(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "id");
    int id = args.get<int>(1).value();

    std::shared_ptr<Object> object = getObjectById(id);
    if (!object) {
        throw std::runtime_error("Object not found");
    }

    getConsoleArea()->selectObject(object, /*force=*/true);
}

void Game::consoleSelectObjectByTag(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "tag");
    std::string_view tag = args[1].value();

    auto area = getConsoleArea();
    for (auto &object : area->objects()) {
        if (object->tag() == tag) {
            area->selectObject(object, /*force=*/true);
            return;
        }
    }

    throw std::runtime_error("Object not found");
}

void Game::consoleSelectLeader(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    getConsoleArea()->selectObject(getConsoleLeader());
}

void Game::consoleSetFaction(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    Faction faction = args.getEnum<Faction>(1).value();
    getConsoleTargetCreature()->setFaction(faction);
}

void Game::consoleSetPosition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");

    glm::vec3 pos(args.get<float>(1).value(),
                  args.get<float>(2).value(),
                  args.get<float>(3).value());

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    std::shared_ptr<Area> area = getConsoleArea();

    creature->setPosition(pos);
    area->determineObjectRoom(*creature);

    auto leader = party().getLeader();
    if (creature == leader) {
        area->onPartyLeaderMoved(/*roomChanged=*/true);
    }
}

void Game::consoleProfessionalTools(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::vector<std::pair<std::string, int>> items = {
        // Ranged weapons
        {"g_w_blstrcrbn001", 1},
        {"g_w_blstrpstl001", 2},
        {"g_w_blstrrfl001", 1},
        {"g_w_bowcstr001", 1},
        {"g_w_dsrptpstl001", 2},
        {"g_w_dsrptrfl001", 1},
        {"g_w_ionblstr02", 2},
        {"g_w_ionrfl01", 1},
        {"g_w_rptnblstr01", 1},
        {"g_w_sonicpstl01", 2},
        {"g_w_sonicrfl01", 1},

        // Melee weapons
        {"g_w_dblsbr001", 1},
        {"g_w_dblswrd001", 1},
        {"g_w_gaffi001", 1},
        {"g_w_lghtsbr01", 2},
        {"g_w_lngswrd01", 2},
        {"g_w_stunbaton01", 1},
        {"g_w_waraxe001", 1},

        // Grenades
        {"g_w_adhsvgren001", 10},
        {"g_w_cryobgren001", 10},
        {"g_w_firegren001", 10},
        {"g_w_flashgren001", 10},
        {"g_w_fraggren01", 10},
        {"g_w_iongren01", 10},
        {"g_w_poisngren01", 10},
        {"g_w_sonicgren01", 10},
        {"g_w_stungren01", 10},
        {"g_w_thermldet01", 10},

        // Mines
        {"g_i_trapkit001", 10},
        {"g_i_trapkit004", 10},
        {"g_i_trapkit007", 10},
        {"g_i_trapkit010", 10},

        // Consumables
        {"g_i_frarmbnds01", 10},
        {"g_i_medeqpmnt01", 10},
        {"g_i_medeqpmnt04", 10},
        {"g_i_adrnaline001", 10},
        {"g_i_adrnaline002", 10},
        {"g_i_adrnaline003", 10},
    };

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    if (!creature) {
        return;
    }
    for (auto &kv : items) {
        creature->addItem(kv.first, kv.second);
    }
}

void Game::consoleKillRoom(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::shared_ptr<Creature> target = getConsoleTargetCreature();
    Room *room = target->room();
    if (!room) {
        throw std::runtime_error("No room found for the selected creature");
    }

    auto leader = party().getLeader();
    bool killEnemies = target == leader;

    SmallSet<Creature *, 16> targets;
    for (Object *object : room->tenants()) {
        Creature *creature = dyn_cast<Creature>(object);
        if (!creature || creature->isDead()) {
            continue;
        }

        if (killEnemies) {
            // Kill the creatures hostile to the leader
            if (_services.game.reputes.getIsEnemy(*creature, *target)) {
                targets.insert(creature);
            }
        } else {
            // Kill all creatures with the same faction as the selected target
            if (target->faction() == creature->faction()) {
                targets.insert(creature);
            }
        }
    }

    for (Creature *creature : targets) {
        creature->damage(std::numeric_limits<int>::max(), nullptr);
    }
}

void Game::consoleAutoSkipEnable(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    _conversationAutoSkip.enabled = args.get<int>(1).value();
}

void Game::consoleAutoSkipEntries(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "1|0 ...");

    auto &entries = _conversationAutoSkip.entries;
    entries = std::queue<bool>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        entries.push(args.get<int>(i).value());
    }
}

void Game::consoleAutoSkipReplies(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "number|? ...");

    auto &replies = _conversationAutoSkip.replies;
    replies = std::queue<std::optional<int>>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        int val = args.get<int>(i).value();
        if (!val) {
            replies.push(std::optional<int>());
            continue;
        }
        replies.push(val - 1);
    }
}

void Game::consoleStartConversation(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[dlg_resref]");

    auto leader = getConsoleLeader();
    _captureHUDPresentation = false;
    auto resRef = args[1];
    if (resRef) {
        startDialog(leader, std::string(resRef.value()), {}, _party.player());
        return;
    }

    auto target = getConsoleTargetObject();

    auto action = newAction<StartConversationAction>(target, "");
    leader->addAction(std::move(action));
}

void Game::consoleCutsceneAttack(const ConsoleArgs &args) {
    consoleCheckUsage(args, 4, 4, "target_id animation_id result damage");

    std::shared_ptr<Creature> actor = getConsoleTargetCreature();

    std::shared_ptr<Object> target = getObjectById(args.get<uint32_t>(1).value());
    if (!target) {
        throw std::runtime_error("Target not found");
    }

    const CutsceneAttack cutscene {args.get<int>(2).value(), args.get<int>(3).value(), args.get<int>(4).value()};
    _combat.scheduleAttack(*actor, target, FeatType::Invalid, cutscene);
}

void Game::consoleSetAbility(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "ability value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<Ability> ability = args.getEnum<Ability>(1);
    if (!ability) {
        throw std::runtime_error("Invalid ability: must be a number");
    }
    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setAbilityScore(ability.value(), value.value());
    if (ability.value() == Ability::Constitution) {
        actor->recalculatePermanentVitality();
    }
}

void Game::consoleSetSkill(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "skill value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SkillType> skill = args.getEnum<SkillType>(1);
    if (!skill) {
        throw std::runtime_error("Invalid skill: must be a number");
    }

    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setSkillRank(skill.value(), value.value());
}

void Game::consoleAddOrRemoveFeat(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "feat");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<FeatType> feat = args.getEnum<FeatType>(1);
    if (!feat) {
        throw std::runtime_error("Invalid feat: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addfeat") {
        attrs.addFeat(feat.value());
    } else {
        attrs.removeFeat(feat.value());
    }
    actor->recalculatePermanentVitality();
}

void Game::consoleAddOrRemoveSpell(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "spell");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SpellType> spell = args.getEnum<SpellType>(1);
    if (!spell) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addspell") {
        attrs.addSpell(spell.value());
    } else {
        attrs.removeSpell(spell.value());
    }
}

void Game::consoleCastSpellAtObject(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 3, "spell ischeat item");

    auto leader = getConsoleLeader();
    auto target = getConsoleTargetObject();

    std::optional<SpellType> spellType = args.getEnum<SpellType>(1);
    if (!spellType) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    std::shared_ptr<Spell> spell = _services.game.spells.get(spellType.value());
    if (!spell) {
        throw std::runtime_error("Unknown spell");
    }

    bool cheat = args.get<int>(2).value_or(false);
    std::optional<std::string_view> spellItem = args[3];
    std::optional<std::shared_ptr<Item>> item;
    if (spellItem) {
        for (const std::shared_ptr<Item> &inventoryItem : leader->items()) {
            if (inventoryItem->tag() == spellItem.value()) {
                item = inventoryItem;
                break;
            }
        }
        if (!cheat && !item) {
            throw std::runtime_error("Item is not in the inventory");
        }
    }

    auto action = newAction<CastSpellAtObjectAction>(
        spell, std::move(target), std::move(item), cheat);

    _combat.scheduleCast(*leader, action);
}

void Game::consoleOpenCloseDoor(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[triggerer_id]");

    auto target = dyn_cast<Door>(getConsoleTargetObject());
    if (!target) {
        throw std::runtime_error("Selected object must be a door");
    }

    auto triggerer_id = args.get<uint32_t>(1);
    std::shared_ptr<Object> triggerer;
    if (triggerer_id) {
        if (uint32_t id = triggerer_id.value()) {
            triggerer = getObjectById(id);
        }
    } else {
        triggerer = getConsoleLeader();
    }

    if (args[0].value() == "opendoor") {
        if (triggerer) {
            target->open(*triggerer);
            target->onOpen(triggerer->id());
        } else {
            target->open();
        }
    } else {
        target->close();
        if (triggerer) {
            target->onClosed(triggerer->id());
        }
    }
}

void Game::consoleListGames(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    // Indices must address the same list consoleLoadGame indexes.
    std::stringstream ss;
    unsigned index = 0;
    const char *newline = "";
    for (const auto &save : savedGames()) {
        ss << newline << "[" << index++ << "] "
           << save.descriptor.directory.filename().string();
        newline = "\n";
    }
    _console.printLine(ss.str());
}

void Game::consoleLoadGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "save_id");
    size_t id = *args.get<size_t>(1);
    auto saves = savedGames();
    if (id >= saves.size()) {
        throw std::runtime_error("Invalid savegame id");
    }
    loadGame(saves[id].descriptor);
}

void Game::consoleSaveGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, std::numeric_limits<size_t>::max(), "slot [name]");
    auto slot = args.get<uint32_t>(1);
    if (!slot) {
        throw std::runtime_error("Invalid save slot");
    }
    std::string name;
    for (size_t i = 2; i < args.size(); ++i) {
        if (!name.empty()) {
            name += " ";
        }
        name += std::string(*args[i]);
    }
    auto result = requestSave(
        {SaveKind::Developer, *slot, std::move(name), true});
    _console.printLine(result.message);
}

void Game::consoleStartPazaak(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    if (!_options.game.developer) {
        _console.printLine("pazaak: developer mode required");
        return;
    }
    if (_pazaakSession) {
        _console.printLine("pazaak: already active");
        return;
    }
    if (!_module || _screen != Screen::InGame) {
        _console.printLine("pazaak: no active in-game module");
        return;
    }

    std::shared_ptr<Object> selected(
        _pazaakDevelopmentSelectedObjectOverride.resolve());
    if (!selected) {
        if (auto area = _module->area()) {
            selected = area->selectedObject();
        }
    }

    std::string opponentName("Pazaak Opponent");
    if (selected && selected->type() == ObjectType::Creature) {
        if (!selected->name().empty()) {
            opponentName = selected->name();
        } else if (!selected->tag().empty()) {
            opponentName = selected->tag();
        }
    }

    if (!startDevelopmentPazaak(opponentName)) {
        _console.printLine("pazaak: development launch failed");
        return;
    }
    _console.printLine("pazaak: development match started against " + opponentName);
}

void Game::consoleMiniGameInfo(const ConsoleArgs &args) {
    auto area = getConsoleArea();
    if (!area->hasMinigame()) {
        _console.printLine("minigame: none");
        return;
    }
    const auto &mg = area->miniGame();
    _console.printLine(str(boost::format("minigame: type=%s camfov=%.1f lataccel=%.3f movePerSec=%.3f inertia=%d bumpPlane=%u doBumping=%d")
                           % minigameTypeName(mg.type)
                           % mg.cameraViewAngle
                           % mg.lateralAccel
                           % mg.movementPerSec
                           % static_cast<int>(mg.useInertia)
                           % mg.bumpPlane
                           % static_cast<int>(mg.doBumping)));
    _console.printLine(str(boost::format("  player: cam=%s track=%s spd=[%.1f,%.1f] accel=%.3f hp=%u models=%zu")
                           % mg.player.cameraResRef
                           % mg.player.trackResRef
                           % mg.player.minimumSpeed
                           % mg.player.maximumSpeed
                           % mg.player.accelSecs
                           % mg.player.hitPoints
                           % mg.player.models.size()));
    _console.printLine(str(boost::format("  tunnel (deg): X=[%.1f,%.1f] Y=[%.1f,%.1f] Z=[%.1f,%.1f]")
                           % mg.player.tunnelXNeg % mg.player.tunnelXPos
                           % mg.player.tunnelYNeg % mg.player.tunnelYPos
                           % mg.player.tunnelZNeg % mg.player.tunnelZPos));
    _console.printLine(str(boost::format("  tracks=%zu enemies=%zu obstacles=%zu")
                           % mg.trackResRefs.size()
                           % mg.enemies.size()
                           % mg.obstacles.size()));
    for (size_t i = 0; i < mg.trackResRefs.size(); ++i) {
        _console.printLine(str(boost::format("    track[%zu] %s") % i % mg.trackResRefs[i]));
    }
    for (size_t i = 0; i < mg.enemies.size(); ++i) {
        const auto &e = mg.enemies[i];
        _console.printLine(str(boost::format("    enemy[%zu] track=%s hp=%u models=%zu")
                               % i % e.trackResRef % e.hitPoints % e.models.size()));
    }
    for (size_t i = 0; i < mg.obstacles.size(); ++i) {
        _console.printLine(str(boost::format("    obstacle[%zu] name=%s") % i % mg.obstacles[i].name));
    }
    if (auto layout = _services.resource.layouts.get(area->name())) {
        auto placement = layout->findTrackByName(mg.player.trackResRef);
        if (placement) {
            const auto &p = placement->get().position;
            _console.printLine(str(boost::format("  lyt tracks=%zu playerTrack=%s pos=[%.1f,%.1f,%.1f]")
                                   % layout->tracks.size() % mg.player.trackResRef % p.x % p.y % p.z));
        } else {
            _console.printLine(str(boost::format("  lyt tracks=%zu playerTrack=%s pos=<not found>")
                                   % layout->tracks.size() % mg.player.trackResRef));
        }
        size_t obstaclesMatched = 0;
        for (const auto &obs : mg.obstacles) {
            if (layout->findObstacleByName(obs.name)) {
                ++obstaclesMatched;
            }
        }
        _console.printLine(str(boost::format("  lyt obstacles=%zu (matched %zu of %zu .are obstacles)")
                               % layout->obstacles.size() % obstaclesMatched % mg.obstacles.size()));
    }
    const auto &sc = mg.player.scripts;
    if (!sc.onCreate.empty() || !sc.onDeath.empty() || !sc.onTrackLoop.empty()) {
        _console.printLine(str(boost::format("  scripts: create=%s death=%s loop=%s damage=%s")
                               % sc.onCreate % sc.onDeath % sc.onTrackLoop % sc.onDamage));
    }
}

void Game::consoleStartSwoop(const ConsoleArgs &args) {
    openSwoopRace();
}

void Game::consoleStopSwoop(const ConsoleArgs &args) {
    // If a lifecycle race is active, return to origin safely; otherwise stop in place.
    exitSwoopRace();
}

void Game::consoleStartSwoopRace(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");

    if (_swoopLifecycle.active) {
        _console.printLine("swoop: lifecycle already active");
        return;
    }
    if (!_module) {
        _console.printLine("swoop: no origin module loaded");
        return;
    }
    std::string target(boost::to_lower_copy(std::string(args[1].value())));
    if (_moduleNames.count(target) == 0) {
        _console.printLine("swoop: unknown module '" + target + "'");
        return;
    }

    // Capture origin module/state before transitioning.
    MinigameLifecycle session;
    session.originModule = _module->name();
    session.forcedSuccess = true;
    if (auto leader = _party.getLeader()) {
        session.originPosition = leader->position();
        session.originFacing = leader->getFacing();
        session.haveOrigin = true;
    }

    // Transition to the target swoop module and auto-start the race.
    loadModule(target);
    openSwoopRace();

    if (!_swoopRace.isActive()) {
        // Target loaded but is not a swoop minigame (openSwoopRace printed why).
        // Return to origin so the failed attempt does not strand the player.
        _console.printLine("swoop: lifecycle aborted, returning to origin=" + session.originModule);
        loadModule(session.originModule);
        if (session.haveOrigin) {
            if (auto mod = _module) {
                if (auto area = mod->area()) {
                    if (auto leader = _party.getLeader()) {
                        leader->setPosition(session.originPosition);
                        leader->setFacing(session.originFacing);
                        area->determineObjectRoom(*leader);
                        area->onPartyLeaderMoved(/*roomChanged=*/true);
                    }
                }
            }
        }
        return;
    }

    _swoopLifecycle = session;
    _swoopLifecycle.active = true;
    _console.printLine(str(boost::format("swoop: lifecycle start origin=%s target=%s forcedSuccess=yes")
                           % session.originModule % target));
}

void Game::consoleFinishSwoop(const ConsoleArgs &args) {
    if (!_swoopLifecycle.active) {
        _console.printLine("swoop: no lifecycle race active");
        return;
    }
    finishSwoopLifecycle(/*success=*/true);
}

void Game::consoleSwoopState(const ConsoleArgs &args) {
    if (!_swoopRace.isActive()) {
        _console.printLine("swoop: not active");
        return;
    }
    glm::vec3 pos = _swoopRace.position();
    _console.printLine(str(boost::format("swoop: progress=%.1f finish=%.1f lateral=%.2f speed=%.1f elapsed=%.1f pos=[%.1f,%.1f,%.1f] bounds=[-%.1f,+%.1f] mode=track-progress")
                           % _swoopRace.progress()
                           % _swoopRace.finishProgress()
                           % _swoopRace.lateralOffset()
                           % _swoopRace.speed()
                           % _swoopRace.elapsed()
                           % pos.x % pos.y % pos.z
                           % _swoopRace.lateralLeftBound()
                           % _swoopRace.lateralRightBound()));
}

void Game::consoleStartTurret(const ConsoleArgs &args) {
    openTurret();
}

void Game::consoleStartTurretGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");

    std::string target(boost::to_lower_copy(std::string(args[1].value())));
    std::string originModule(_module ? _module->name() : "");
    bool alreadyActive = _turretLifecycle.active || _pendingTurret.active || _turret.isActive();

    auto error = validateTurretRequest(target,
                                       originModule,
                                       _moduleNames.count(target) > 0,
                                       alreadyActive);
    if (error != TurretRequestError::None) {
        _console.printLine(str(boost::format("turret: %s") % turretRequestErrorMessage(error)));
        return;
    }

    // Capture the origin now: the transition is deferred, and by the time it
    // runs the current module is already gone.
    _pendingTurret = PendingTurretRequest();
    _pendingTurret.active = true;
    _pendingTurret.targetModule = target;
    _pendingTurret.originModule = originModule;
    if (auto leader = _party.getLeader()) {
        _pendingTurret.originPosition = leader->position();
        _pendingTurret.originFacing = leader->getFacing();
        _pendingTurret.haveOrigin = true;
    }

    // Go through the normal deferred transition so the Type 2 detection in
    // loadNextModule starts the minigame, exactly as a script entry would.
    scheduleModuleTransition(target, "");

    _console.printLine(str(boost::format("turret: lifecycle scheduled origin=%s target=%s")
                           % originModule % target));
}

void Game::consoleStopTurret(const ConsoleArgs &args) {
    // If a lifecycle session is active, return to origin safely; otherwise stop
    // in place.
    exitTurret();
}

void Game::consoleTurretState(const ConsoleArgs &args) {
    if (!_turret.isActive()) {
        _console.printLine("turret: not active");
        return;
    }
    glm::vec3 pos = _turret.position();
    const char *outcome = "in-progress";
    switch (_turret.outcome()) {
    case Turret::Outcome::Won:
        outcome = "won";
        break;
    case Turret::Outcome::Lost:
        outcome = "lost";
        break;
    default:
        break;
    }
    _console.printLine(str(boost::format("turret: pitch=%.1f yaw=%.1f hp=%d/%d enemies=%zu/%zu bullets=%zu elapsed=%.1f pos=[%.1f,%.1f,%.1f] outcome=%s")
                           % glm::degrees(_turret.aim().pitch())
                           % glm::degrees(_turret.aim().yaw())
                           % _turret.hitPoints()
                           % _turret.maxHitPoints()
                           % _turret.enemiesAlive()
                           % _turret.enemyCount()
                           % _turret.bulletCount()
                           % _turret.elapsed()
                           % pos.x % pos.y % pos.z
                           % outcome));
    _console.printLine(str(boost::format("  hud: health=%d(%s) heading=%d alarm=%d contacts=%zu/%zu gauge=%d radar=%d radarChannels=%zu")
                           % _turret.healthState()
                           % turretHealthAnimation(_turret.healthState())
                           % _turret.headingState()
                           % static_cast<int>(_turret.alarmActive())
                           % _turret.contactsLive()
                           % kTurretContactCount
                           % static_cast<int>(_turret.haveHealthHud())
                           % static_cast<int>(_turret.haveRadarHud())
                           % _turret.radarChannelCount()));
}

void Game::consoleShowImGui(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    _showImGui = show;
}

void Game::consoleShowPath(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowPath(show);
}

} // namespace game

} // namespace reone
