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

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "reone/audio/source.h"
#include "reone/graphics/cursor.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/movie/movie.h"
#include "reone/script/routines.h"
#include "reone/system/logutil.h"

#include "action.h"
#include "areamusicplayer.h"
#include "combat.h"
#include "console.h"
#include "di/services.h"
#include "effect.h"
#include "event.h"
#include "floatingtext.h"
#include "globalfade.h"
#include "gui/chargen.h"
#include "gui/computer.h"
#include "gui/confirmpopup.h"
#include "gui/container.h"
#include "gui/conversation.h"
#include "gui/credits.h"
#include "gui/deathdisplay.h"
#include "gui/dialog.h"
#include "gui/galaxymap.h"
#include "gui/hud.h"
#include "gui/ingame.h"
#include "gui/loadscreen.h"
#include "gui/mainmenu.h"
#include "gui/map.h"
#include "gui/partyselect.h"
#include "gui/pazaak.h"
#include "gui/saveload.h"
#include "journal.h"
#include "location.h"
#include "messagelog.h"
#include "object/area.h"
#include "object/areaofeffect.h"
#include "object/camera/animated.h"
#include "object/camera/dialog.h"
#include "object/camera/firstperson.h"
#include "object/camera/static.h"
#include "object/camera/thirdperson.h"
#include "object/creature.h"
#include "object/door.h"
#include "object/encounter.h"
#include "object/module.h"
#include "object/placeable.h"
#include "object/sound.h"
#include "object/store.h"
#include "object/trigger.h"
#include "object/waypoint.h"
#include "options.h"
#include "party.h"
#include "pazaaksession.h"
#include "presentationpointer.h"
#include "savegame.h"
#include "saveprovenance.h"
#include "script/runner.h"
#include "statussummary.h"
#include "swooprace.h"
#include "talent.h"
#include "turret.h"

namespace reone {

namespace gui {

class GUI;

}

namespace graphics {

class Font;
class Texture;

}

namespace resource {

class PreparedModuleLoad;
class SaveWorkingState;

}

namespace game {

/**
 * Temporary death recovery's millisecond counters, independent of world simulation.
 * Game owns readiness/game-over/modal admission. Blocked calls must not sample
 * the clock: the next admitted call includes that real-time interval.
 */
class TemporaryDeathRecovery {
public:
    /** While a member is down, the area is scanned for hostiles this often. */
    static constexpr std::uint32_t kHostileScanMilliseconds = 1000;
    /** Members recover once no hostile has been seen for this long. */
    static constexpr std::uint32_t kSafeMilliseconds = 5000;

    /** A new session starts both counters at zero, sampled from now. */
    void reset(std::uint32_t now) {
        _lastSample = now;
        _hostileScanElapsed = 0;
        _safeElapsed = 0;
    }

    /**
     * Advance to the real time now. Returns whether the downed members recover.
     *
     * @param hasTemporaryDeath a party member is down
     * @param scan whether a hostile creature sees a party member
     */
    template <class HostileScan>
    bool sample(std::uint32_t now, bool hasTemporaryDeath, HostileScan scan) {
        const std::uint32_t elapsed = _lastSample ? now - *_lastSample : 0;
        _lastSample = now;
        return update(elapsed, hasTemporaryDeath, scan);
    }

    /** As sample, by the milliseconds elapsed since the last call. */
    template <class HostileScan>
    bool update(std::uint32_t elapsedMilliseconds, bool hasTemporaryDeath, HostileScan scan) {
        if (!hasTemporaryDeath) {
            // The scan accumulator survives a no-down-member gap.
            _safeElapsed = 0;
            return false;
        }
        _hostileScanElapsed += elapsedMilliseconds;
        if (_hostileScanElapsed > kHostileScanMilliseconds) {
            _hostileScanElapsed = 0;
            if (scan()) {
                _safeElapsed = 0;
                return false;
            }
        }
        _safeElapsed += elapsedMilliseconds;
        // Keep retrying until the next party scan finds no temporary deaths,
        // including cases where resurrection was refused.
        return _safeElapsed > kSafeMilliseconds;
    }

private:
    std::optional<std::uint32_t> _lastSample;
    std::uint32_t _hostileScanElapsed {0};
    std::uint32_t _safeElapsed {0};
};

enum class ModuleLoadContext {
    FreshModule,
    InitialTemplateRestore,
    InitialSaveRestore,
    SavedModuleTransition,
};

ModuleLoadContext resolveModuleLoadContext(
    bool initialSaveRestore,
    bool savedModuleSnapshot);

bool restoresSavedWorld(ModuleLoadContext context);
bool restoresSavedSession(ModuleLoadContext context);
bool preservesSavedPlacement(ModuleLoadContext context);

struct SavedObjectReference;
struct SerializedScriptSituation;
class SavedScriptContinuation;
class ModuleSnapshotBuilder;

class Game : boost::noncopyable {
public:
    enum class Screen {
        None,
        MainMenu,
        Loading,
        CharacterGeneration,
        InGame,
        InGameMenu,
        Conversation,
        Container,
        PartySelection,
        SaveLoad,
        GalaxyMap,
        SwoopRace,
        PazaakWager,
        PazaakSetup,
        PazaakBoard,
        Turret,
        Death
    };

    Game(
        resource::GameID gameId,
        std::filesystem::path path,
        OptionsView &options,
        ServicesView &services,
        IConsole &console,
        std::shared_ptr<PresentationPointer> pointer = nullptr) :
        _gameId(gameId),
        _path(std::move(path)),
        _options(options),
        _services(services),
        _console(console),
        _party(*this),
        _combat(*this, services),
        _swoopRace(*this),
        _turret(*this, services),
        _journal(services.resource.gffs, services.resource.strings),
        _floatingText(*this, services),
        _areaMusic(services),
        _pointer(pointer ? std::move(pointer) : std::make_shared<PresentationPointer>(services.resource.cursors)) {
        initJournalNotifications();
        // TSL keeps combat lines in a list of their own.
        _messageLog.setCombatBufferEnabled(gameId == resource::GameID::TSL);
    }

    void init();

    bool handle(const input::Event &event);
    void update(float frameTime);
    void render();

    /**
     * Report, and clear, whether a break in gameplay time has occurred since
     * the last call.
     *
     * Loading a module blocks for as long as reading it takes, and that wall
     * time is not time the game world experienced. The caller owns the frame
     * clock, so it is the caller that has to open a new epoch; this only says
     * that one is due.
     */
    bool consumeTimingDiscontinuity();
    /**
     * The world waits while the window is out of focus: none of that time
     * passes in it, and its clock takes up again from the next update.
     */
    void holdWorldClock() { _worldClockSample.reset(); }

    /**
     * Play a movie, ending any module transition movies.
     *
     * @param streamingMusic whether area music set to play through movies
     *        plays on while it runs
     */
    void playVideo(const std::string &name, bool streamingMusic = false);

    bool isPaused() const { return _paused; }
    /**
     * The player's pause, or a menu that pauses the world the same way: the
     * clock stands still, actions still run, and combat and conversation
     * starts wait.
     */
    bool holdsWorld() const;
    bool isFreeLook() const { return _freeLook; }
    /**
     * While blocked, the player's presses, pointer motion and wheel are
     * ignored; releases still arrive, so nothing stays held.
     */
    void setPlayerInputBlocked(bool blocked) { _playerInputBlocked = blocked; }
    /** Stops the leader's steering and the camera's turning and mouse-look. */
    void stopMovement();
    /**
     * For the given world milliseconds from now, a click neither starts a
     * conversation nor uses a placeable.
     */
    void markNoClickEvent(uint32_t milliseconds);
    /** Whether a click may start a conversation or use a placeable. */
    bool canClick() const;
    bool isTSL() const { return _gameId == resource::GameID::TSL; }
    resource::GameID gameId() const { return _gameId; }

    Camera *getActiveCamera() const;

    OptionsView &options() { return _options; }
    const OptionsView &options() const { return _options; }
    Party &party() { return _party; }
    const Party &party() const { return _party; }
    Combat &combat() { return _combat; }
    const Combat &combat() const { return _combat; }
    Journal &journal() { return _journal; }
    MessageLog &messageLog() { return _messageLog; }
    FloatingText &floatingText() { return _floatingText; }
    ScriptRunner &scriptRunner() { return *_scriptRunner; }
    /** The next place in the order in which creatures join their factions. */
    uint32_t nextFactionJoinOrder() { return ++_factionJoinOrder; }
    Map &map() { return *_map; }
    script::IRoutines &routines() { return *_routines; }

    std::shared_ptr<Module> module() const { return _module; }
    CameraType cameraType() const { return _cameraType; }
    const std::set<std::string> &moduleNames() const { return _moduleNames; }
    const std::set<std::string> &saveNames() const { return _saveNames; }

    void initLocalServices();
    std::shared_ptr<Spell> getSpell(SpellType type) const;
    void setSceneSurfaces();

    void setCursorType(resource::CursorType type);
    void setPaused(bool paused, PauseReason reason = PauseReason::Other) {
        _paused = paused;
        if (paused) {
            _pauseReason = reason;
        } else {
            _autoPaused = false;
        }
    }
    /**
     * The player's pause control: toggles play and ends any autopause.
     * Returns whether play is now paused.
     */
    bool togglePlayerPause() {
        setPaused(!_paused, PauseReason::Player);
        return _paused;
    }
    PauseReason pauseReason() const { return _pauseReason; }

    // Time stop

    /**
     * While time is stopped the world clock stands still and only the objects
     * excluded from the stop act; they run on a clock of their own, which
     * rejoins world time whenever the stop starts or ends.
     */
    bool isTimeStopped() const { return _timeStopped; }
    /** Starts or ends the time stop; the excluded objects' clock rejoins world time. */
    void toggleTimeStop();
    /** \p object acts through the time stop. */
    void addTimeStopExclusion(Object &object);
    /** \p object no longer acts through the time stop. */
    void removeTimeStopExclusion(const Object &object);
    /** Whether \p object acts through the time stop. */
    bool isExcludedFromTimeStop(const Object &object) const;
    /** Whether \p object is held still by the time stop. */
    bool isFrozenByTimeStop(const Object &object) const {
        return _timeStopped && !isExcludedFromTimeStop(object);
    }
    /** The time the object lives by: its own clock while it acts in a time stop. */
    uint64_t activeTimeMilliseconds(const Object &object) const {
        return _timeStopped && isExcludedFromTimeStop(object) ? _timeStopMilliseconds : _worldTimeMilliseconds;
    }
    /** The calendar day and the time of day of the object's own time. */
    uint32_t activeTimeDay(const Object &object) const {
        return static_cast<uint32_t>(activeTimeMilliseconds(object) / millisecondsPerWorldDay());
    }
    uint32_t activeTimeOfDay(const Object &object) const {
        return static_cast<uint32_t>(activeTimeMilliseconds(object) % millisecondsPerWorldDay());
    }
    /** Time stands still only while an excluded creature keeps a Time Stop effect. */
    void updateTimeStop();

    // END Time stop

    /** A pause key: toggles play, with the pause tutorial or the action sound. */
    void pressPauseKey();
    /** The HUD pause toggle: toggles play and asks for the pause tutorial. */
    void pressPauseButton();
    /**
     * The player's stealth control: the leader uses its Stealth skill, once
     * the party is in solo mode or has no one else.
     */
    void requestStealth();
    /** Ask whether to switch solo mode (and, for stealth, then use the skill). */
    void showSoloModeQuery(bool forStealth);
    void updateSoloModeQuery();
    /**
     * Conversation locks set by scripts, kept for the whole session: a
     * creature with its orientation locked is not turned toward the other
     * speaker, and one with head-follow locked turns its body instead of its head.
     */
    void setDialogOrientationLocked(uint32_t objectId, bool locked) {
        if (locked) {
            _dialogOrientationLocks.insert(objectId);
        } else {
            _dialogOrientationLocks.erase(objectId);
        }
    }
    bool isDialogOrientationLocked(uint32_t objectId) const { return _dialogOrientationLocks.count(objectId) > 0; }
    void setDialogHeadFollowLocked(uint32_t objectId, bool locked) {
        if (locked) {
            _dialogHeadFollowLocks.insert(objectId);
        } else {
            _dialogHeadFollowLocks.erase(objectId);
        }
    }
    bool isDialogHeadFollowLocked(uint32_t objectId) const { return _dialogHeadFollowLocks.count(objectId) > 0; }
    /**
     * The party leader, rather than the player character, answers in
     * conversations. A script starting a conversation asks for it or not; it
     * holds until a conversation ends.
     */
    void setLeaderAnswersInDialog(bool leader) { _leaderAnswersInDialog = leader; }
    /** The party leader, rather than the player character, answers in conversations. */
    bool leaderAnswersInDialog() const { return _leaderAnswersInDialog; }
    /**
     * Whether a conversation is under way: from the moment one is asked for,
     * by a click to talk or a script, through the walk to the one talked to,
     * until it ends, its start fails or its request is dropped, or a module
     * starts.
     */
    bool isDialogUnderWay() const { return _dialogUnderWay; }
    void setDialogUnderWay(bool underWay) { _dialogUnderWay = underWay; }

    /** Show a message box dismissed with OK. False when the popup cannot be loaded. */
    bool showMessagePopup(
        const std::string &message,
        std::shared_ptr<graphics::Texture> icon = nullptr,
        std::function<void()> onConfirm = {});
    /** Close an open in-game popup as its key would. */
    void closeMessagePopup();
    /**
     * Pause play for a situation the player enabled in the autopause options.
     * In the window after a door is opened, unlocked or bashed, the pause is
     * deferred instead.
     *
     * @param reason the situation
     */
    void requestAutoPause(AutoPauseReason reason);
    /**
     * Opens the window in which the next autopause waits a second, unless a
     * deferred autopause is already waiting. Opening, unlocking or bashing a
     * door opens it.
     */
    void delayNextEnemyAutoPause();
    const AutoPauseOptions &autoPauseOptions() const { return _options.game.autoPause; }
    bool isAutoPaused() const { return _autoPaused; }
    void setAutoPauseOptions(const AutoPauseOptions &options);
    /** Write the autopause options to the configuration. */
    void saveAutoPauseOptions() const;
    bool clientCombatMode() const { return _clientCombatMode; }
    /** Combat mode follows the leader's, in the area it stands in. */
    void syncClientCombatMode();
    /**
     * Step the target through the nearby objects in bearing order: direction 0
     * turns counter-clockwise from the leader's facing, direction 1 clockwise.
     * In combat mode only visible hostile creatures qualify while any remain.
     */
    std::shared_ptr<Object> selectNearestObject(int direction);
    void setKeepStealthInDialog(bool value) { _keepStealthInDialog = value; }
    void setRelativeMouseMode(bool relative);

    void openMainMenu();
    void openInGame();
    /** Covers the screen with \p color at opacity \p alpha, over what is drawn so far. */
    void renderScreenFill(const glm::vec3 &color, float alpha);

    bool hasPlayableRuntimeSession() const { return _runtimeSessionPlayable; }

    // Swoop race (developer skeleton)

    void openSwoopRace();
    void closeSwoopRace();

    // Exit the active race: returns to the lifecycle origin if a lifecycle race
    // is in progress, otherwise just stops the dev race in place.
    void exitSwoopRace();

    // END Swoop race

    // Turret minigame

    void openTurret();
    void closeTurret();

    // Exit the active turret: returns to the lifecycle origin if a lifecycle
    // session is in progress, otherwise just stops the dev session in place.
    void exitTurret();

    // END Turret minigame

    void openInGameMenu(InGameMenuTab tab);
    /** The roster index whose inventory the menu shows, or -1 when it is closed. */
    int inventoryMenuCharacter() const;
    /** Script cost multipliers per base item; they last for the process and are not saved. */
    float baseItemCostMultiplier(int baseItem) const;
    void setBaseItemCostMultiplier(int baseItem, float multiplier) { _baseItemCostMultipliers[baseItem] = multiplier; }
    /** Control returns to this party member when the conversation it handed on ends. */
    void setPostDialogCharacterSwitch(const std::shared_ptr<Creature> &creature) { _postDialogCharacterSwitch = creature; }
    void cancelPostDialogCharacterSwitch() { _postDialogCharacterSwitch.reset(); }
    /**
     * When a member was stored, pass the lead on one member at a time, at most
     * once round the party, until that member leads.
     */
    void finishPostDialogCharacterSwitch();
    void openLevelUp();
    /**
     * A party member has enough experience to level up; newly when this
     * award brought it there. The level-up chime is the main interface's.
     */
    void notifyLevelUpAvailable(const Creature &creature, bool newlyAvailable);
    void openContainer(const std::shared_ptr<Object> &container);
    void closeContainer(const Object &container);
    void openPartySelection(const PartySelectionContext &ctx);

    /**
     * Whether the current Area already contains one coherent live runtime
     * representation for every requested logical NPC slot and no other
     * selected companion.
     */
    bool isPartySelectionRealized(
        const std::vector<int> &selectedNpcs) const;

    /**
     * Reconcile requested logical NPC slots with exact roster bindings and
     * current-Area residency. A coherent unchanged selection is a no-op.
     */
    bool reconcilePartySelection(
        const std::vector<int> &selectedNpcs);
    void openSaveLoad(SaveLoadMode mode);
    void openGalaxyMap(int initialPlanet);
    /** Whether the galaxy map may take the screen over from the given one. */
    static bool canOpenGalaxyMapFrom(Screen screen);

    // KotOR I Pazaak lifecycle

    bool playPazaak(
        int opponentDeck,
        std::string continuationScript,
        int maximumWager,
        bool tutorialRequested,
        const std::shared_ptr<Object> &opponent);

    PazaakSession *pazaakSession() { return _pazaakSession.get(); }
    const PazaakSession *pazaakSession() const { return _pazaakSession.get(); }
    const std::optional<PazaakCompletedResult> &lastPazaakResult() const { return _lastPazaakResult; }

    void showPazaakSetup();
    void showPazaakBoard();
    void cancelPazaak();
    void abortPazaak();
    void completePazaakIfReady();

    // END KotOR I Pazaak lifecycle

    void startCharacterGeneration();

    /**
     * Sets the music the next loading screens play to the one the
     * loading-screen table gives the module named \p moduleName.
     */
    void setLoadMusicForModule(const std::string &moduleName);
    /** Stops the loading-screen music; the track stays set for the next loading screen. */
    void stopLoadMusic();
    /**
     * Fades the menu music out for a fixed wait and then stops it, holding
     * everything else meanwhile, as starting a new game or loading a save
     * from the main menu does.
     */
    void fadeOutMenuMusic();
    void startDialog(const std::shared_ptr<Object> &owner, const std::string &resRef,
                     GlobalFade::DialogTicket admission = {},
                     const std::shared_ptr<Object> &listener = nullptr);

    GlobalFade &globalFade() { return _globalFade; }
    const GlobalFade &globalFade() const { return _globalFade; }
    AreaMusicPlayer &areaMusic() { return _areaMusic; }

    void pauseConversation();
    void resumeConversation();
    /** A caller that is still standing pauses its conversation and holds its place in it until it resumes. */
    void pauseConversationBy(Object &caller);
    /** Resuming releases the caller's hold on the conversation and ends its stealth. */
    void resumeConversationBy(Object &caller);
    /** The object stops taking part in the running conversation. */
    void stopConversationParticipation(const Object &object);

    void setBarkBubbleText(std::string text, float durartion);
    /** Show a combat-mode line for the controlled character. */
    void presentCombatMessage(int strref);
    /** End the choice of a party member to use an item on, if the main interface is making one. */
    void cancelPartyItemTarget();
    /** Append a feedback line from a string reference, filling the given custom tokens. */
    void addFeedbackMessage(int strRef, const std::map<int, std::string> &tokens = {});

    /** Submit one of the fixed status-summary categories. */
    void submitStatusSummary(StatusSummaryCategory category, int amount = 0, std::vector<std::string> items = {});
    StatusSummaryAccumulator &statusSummary() { return _statusSummary; }
    /**
     * A status summary was taken down or passed by unseen. TSL then looks for
     * the story's reaction to the player character's alignment, and, the first
     * time the player's own character can level up, runs the level-up dialog.
     */
    void finishStatusSummaryCycle();

    int getPlotXP(const std::string &plotName);

    /** Award the whole-number percentage used by the GivePlotXP script routine. */
    void awardPlotXP(const std::string &plotName, int percentage);

    /** Award an authored DLG/JRL fraction, where 0.2 means twenty percent. */
    void awardPlotXPByIndex(int plotIndex, float fraction);

    Screen currentScreen() const {
        return _screen;
    }

    ServicesView &services() const { return _services; }

    /** True while a conversation owns the screen, i.e. a dialogue is running. */
    bool isConversationActive() const {
        return _screen == Screen::Conversation;
    }

    /** A conversation owns the screen, and its dialog is an animated cutscene. */
    bool isAnimatedCutsceneShowing() const {
        return isConversationActive() && _conversation && _conversation->isAnimatedCutscene();
    }

    /** The object is the running dialogue's current speaker or listener. */
    bool isConversationSpeakerOrListener(const Object &object) const {
        return isConversationActive() && _conversation == _dialog.get() && _dialog->isSpeakerOrListener(object);
    }

    std::shared_ptr<movie::IMovie> movie() const {
        return _movie;
    }

    void quit() {
        _quitRequested = true;
    }

    bool isQuitRequested() {
        return _quitRequested;
    }

    resource::CursorType cursorType() const {
        return _pointer->type();
    }

    /** Where the pointer last moved to, in screen coordinates. */
    glm::ivec2 pointerPosition() const {
        return _pointer->position();
    }

    bool relativeMouseMode() const;

    // Module loading

    /**
     * @param entry waypoint tag to spawn at, or empty string to spawn at default location
     */
    bool loadModule(
        const std::string &name,
        std::string entry = "",
        bool initialSaveRestore = false);

    void scheduleModuleTransition(const std::string &moduleName, const std::string &entry);
    void scheduleModuleTransitionWithMovies(const std::string &moduleName, const std::string &entry, std::vector<std::string> movies);
    bool isModuleTransitionScheduled() const { return !_nextModule.empty(); }

    // Load a savegame. The slot is the durable identity discovered by
    // discoverSavedGames(); it is mounted verbatim rather than re-resolved, so
    // the slot the player picked is the slot that gets loaded.
    bool loadGame(const resource::SaveSlotDescriptor &slot);

    struct PreparedDestinationModule {
        std::string name;
        std::unique_ptr<resource::PreparedModuleLoad> resources;
        std::shared_ptr<resource::Gff> ifo;
        std::shared_ptr<resource::Gff> are;
        std::shared_ptr<resource::Gff> git;
        ModuleLoadContext context {ModuleLoadContext::FreshModule};
    };

    /**
     * Candidate save load, resolved and validated but not yet committed.
     *
     * Everything here is read from the unpublished session, so preparing it
     * cannot disturb the running game. The decoded records are carried into
     * restoration rather than parsed a second time once the candidate has been
     * published.
     */
    struct PreparedSaveLoad {
        struct AutosaveRestoreState {
            std::string startWaypoint;
            uint32_t pauseDay {0};
            uint32_t pauseTime {0};
            /** The loading-screen music the save asks for. */
            std::string loadMusic;
        };

        std::unique_ptr<resource::SaveSessionState> session;
        resource::NFO nfo;
        std::shared_ptr<resource::Gff> saveInfo;
        std::shared_ptr<resource::Gff> globalVars;
        std::shared_ptr<resource::Gff> partyTable;
        std::shared_ptr<resource::Gff> inventory;
        std::shared_ptr<resource::Gff> playerInfo;
        std::optional<AutosaveRestoreState> autosave;
        PreparedDestinationModule destination;
    };

    std::vector<SavedGame> savedGames() const;
    const std::filesystem::path &gamePath() const { return _path; }

    SaveResult requestSave(SaveRequest request);
    SaveResult requestManualSave(uint32_t slot, std::string displayName);
    SaveResult requestQuickSave();
    SaveResult requestAutoSave();
    const std::optional<SaveResult> &lastSaveResult() const {
        return _lastSaveResult;
    }
    SaveEligibilityReason saveEligibility(bool requireStablePoint = false) const;

    // Clear state of the current game before loading a new game.
    void resetGame();

    // Retire only active-module runtime objects. Party/session objects and
    // committed save-wide state survive so an ordinary transition can build
    // and publish its destination without becoming a full-session load.
    void retireActiveModuleRuntime();

    // Retire instantiated gameplay state without changing committed resource or
    // save-wide logical state. Runtime reconstruction must explicitly publish a
    // new playable session afterwards.
    void retireRuntimeSession();

    // END Module loading

    // Objects

    std::shared_ptr<Object> getObjectById(uint32_t id) const;
    uint32_t lastTarget() const {
        auto target = _lastTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    /** A look asks the next selection pass to present the target anew, when it changes. */
    void setLastTarget(uint32_t objectId, bool look = false) {
        auto object = getObjectById(objectId);
        if (look && object != _lastTarget.resolve()) {
            _lastTargetLook = true;
        }
        _lastTarget = RuntimeObjectRef<Object>(object);
    }
    bool floatingTextEnabled() const {
        return _options.game.feedback.floatingNumbers;
    }
    /**
     * The GUI font for a base font name: its small variant when the small-font
     * option is on, else its large variant. The console font has no variants.
     */
    std::string guiFontName(const std::string &baseFont) const {
        if (baseFont == "fnt_console") {
            return baseFont;
        }
        return baseFont + (_options.game.feedback.smallFonts ? 'a' : 'b');
    }
    bool tutorialWindowsEnabled() const {
        return _options.game.feedback.tutorialPopups;
    }
    void setTutorialWindowsEnabled(bool enabled) {
        _options.game.feedback.tutorialPopups = enabled;
    }
    const FeedbackOptions &feedbackOptions() const { return _options.game.feedback; }
    void setFeedbackOptions(const FeedbackOptions &options) { _options.game.feedback = options; }
    /** Write the feedback options to the configuration. */
    void saveFeedbackOptions() const;
    /** The difficulty level the player chose: a difficultyopt.2da row. */
    int clientDifficulty() const { return _options.game.clientDifficulty; }
    void setClientDifficulty(int level) { _options.game.clientDifficulty = level; }
    /** Write the difficulty level to the configuration. */
    void saveDifficultyLevel() const;
    /** Whether the mouse turns the camera unless the right button or a Ctrl key is held. */
    bool mouseLook() const { return _options.game.mouse.mouseLook; }
    void setMouseLook(bool on) { _options.game.mouse.mouseLook = on; }
    /** Write the mouse options to the configuration. */
    void saveMouseOptions() const;
    /** Whether the alternate actions key, either Shift, was last pressed rather than released. */
    bool alternateActionsHeld() const { return _alternateActionsHeld; }
    /**
     * Ask for tutorial window id (a tutorial.2da row). It shows on the next
     * frame, at most once per game, while tutorials are on and no conversation
     * runs. True when it will show: the caller then drops the action it was
     * taking, and the window takes it once dismissed.
     */
    bool requestTutorialWindow(
        int id,
        uint32_t actor = script::kObjectInvalid,
        uint32_t subject = script::kObjectInvalid,
        uint32_t param = 0);
    /** The string of a feedbacktext.2da row; none for a blank cell or a row outside the table. */
    std::optional<int> feedbackTextStrRef(int row) const;
    /**
     * The traps.2da row of a trap type. A type outside the table reads as a
     * blank row, as every 2DA row outside its table does.
     */
    const TrapTypeRow &trapType(int type) const;
    /**
     * Whether the caster can cast a Force power from the action menu: it needs
     * a Jedi class in its first two positions, except (TSL) for forms and the
     * powers granted by feats.
     */
    bool canMenuCast(const Creature &caster, const Spell &spell) const;
    /**
     * A menu cast by a caster that cannot cast it: nothing is cast. A droid's
     * cast goes out as the use of an item that does not exist.
     */
    void refuseMenuCast(Creature &caster);
    /**
     * The use of an item's property at \p target that a menu sends, carrying
     * \p location. Out of combat the user's actions clear first. A use that
     * useItem refuses, or one with no cast-spell property, reports string
     * 1434; a user that cannot be commanded uses nothing.
     */
    void useMenuItem(Creature &user, const std::shared_ptr<Item> &item, std::optional<size_t> property,
                     const std::shared_ptr<Object> &target, const glm::vec3 &location);
    /**
     * A creature's use of an item's property, from a menu or a talent: the
     * use goes on the creature's round when the creature may use the item and
     * the property is a usable cast spell whose upgrade, if it waits for one,
     * is installed. Once the creature may use the item and the property
     * exists, a party member breaks the forfeit condition that forbids items
     * or, with anything but forearm bands, the one that forbids all but a
     * shield. Returns
     * whether the use went on the round.
     */
    bool useItem(Creature &user, const Item &item, size_t property, const std::shared_ptr<Action> &use);
    /**
     * A Force power cast from a menu: out of combat the caster's actions
     * clear first, the cast goes on the caster's round, and a party member
     * breaks the forfeit condition that forbids Force powers. A caster that
     * cannot be commanded casts nothing.
     */
    void sendMenuCast(Creature &caster, const std::shared_ptr<Spell> &spell, const std::shared_ptr<Object> &target);
    /**
     * A party member breaks a forfeit condition: when scripts have set it, it
     * becomes the last violation and the member's area is signalled user
     * event 4001. Returns whether the condition was set.
     */
    bool breakForfeitCondition(const Creature &member, int condition);
    /**
     * A party member's item newly in its body slot or a primary hand breaks
     * the first forfeit condition it offends: armour in the body; in a hand,
     * any weapon, then anything but the Dxun sword, then a ranged weapon,
     * then a lightsaber. The second weapon set never counts.
     */
    void breakEquipForfeit(const Creature &member, int slot, const Item &item);
    /**
     * What follows every item going on, in this order: the module is told
     * (event 38), a forfeit condition may break, and in TSL body armour may
     * change the wearer's appearance.
     */
    void finishEquip(Creature &wearer, int slot, const std::shared_ptr<Item> &item);
    /**
     * Signals the module that the wearer put the item on, unless into the
     * second weapon set. The module's equip script runs on it.
     */
    void signalItemEquipped(Creature &wearer, int slot, const std::shared_ptr<Item> &item);
    /**
     * An item put on while its wearer is read. The module is told once the
     * wearer and the item are live.
     */
    void recordEquippedOnLoad(int slot, const std::shared_ptr<Item> &item);
    /**
     * During a module load, tells the module of the items the wearer was read
     * wearing: each creature's come just before it is announced to its area.
     */
    void signalEquippedOnLoad(const Creature &wearer);
    /**
     * The tutorial window a Force power from the action menu asks for: 4 for
     * powers with a hostile slot, else 3; none for the (TSL) forms and powers
     * granted by feats.
     */
    std::optional<int> forcePowerTutorial(const Spell &spell) const;
    /**
     * The Clear One button and key: only in combat mode, and the first time
     * through its tutorial window, which then clears.
     */
    void clearOneAction();
    /**
     * Drop the leader's most recently scheduled round entry; with none
     * pending, force-clear its actions: only a cast not yet committed stays.
     */
    void clearOneCombatAction(Creature &leader);
    /**
     * The Cancel Combat key: in combat mode, the leader leaves it and its
     * orders are cleared as the Clear All button clears them.
     */
    void cancelCombat();
    /**
     * While a conversation runs: every creature of the area outside the party
     * that regards the controlled creature as an enemy drops its actions,
     * forced, and its orders.
     */
    void clearPlayerHostileActions();
    /** A menu attack opens a three-second window of game time in which another shows the repeated-attack tutorial. */
    bool attackMashActive() const { return _attackMashTime > 0.0f; }
    /**
     * The attack menu entry and default action, before the attack is sent:
     * the repeated-attack and attack tutorial windows may take it (a repeated
     * attack is then dropped); otherwise the attacker enters combat mode.
     * Returns whether the attack goes out.
     */
    bool prepareMenuAttack(Creature &attacker, const std::shared_ptr<Object> &target);
    /**
     * The bash menu entry, before the attack is sent: the first time through
     * its tutorial window. A door is bashed after the basher's actions and
     * pending round entries are dropped, and opens the autopause delay
     * window. Returns whether the attack goes out.
     */
    bool prepareMenuBash(Creature &basher, const std::shared_ptr<Object> &target);
    /**
     * A menu attack puts the attacker, and in TSL the whole party when the
     * attacker is a member, into combat mode, then opens the repeated-attack window.
     */
    void beginMenuAttack(Creature &attacker);
    /**
     * The player's attack order: an attack on the attacker's round. In TSL,
     * party members with no attack target join an attack on a creature. Each
     * such member, and the attacker when it is attempting another target,
     * remembers the order.
     */
    void sendAttack(Creature &attacker, const std::shared_ptr<Object> &target, FeatType feat = FeatType::Invalid);
    int scaleDamageForDifficulty(int damage, const Object &target) const;
    /** TSL shifts trap detect and disarm DCs by -5 on easy and +5 on difficult. */
    int trapDifficultyModifier() const;
    bool isRuntimeObjectLive(const Object &object) const;

    // End the semantic lifetime of this exact object and every runtime object
    // it owns. Registry and saved-identity cleanup are pointer guarded, so a
    // stale owner can never invalidate a newer object using the same number.
    void destroyRuntimeObjectGraph(const std::shared_ptr<Object> &object);

    // Build replacement children outside the live registry, publish ownership
    // with a no-throw swap, then atomically publish the candidates and retire
    // obsolete children. A failed build leaves the old graph and all saved-ID
    // bindings untouched.
    template <class Build, class Publish>
    void replaceRuntimeObjectGraph(
        std::vector<std::shared_ptr<Object>> &obsoleteObjects,
        Build &&build,
        Publish &&publish) {
        static_assert(
            noexcept(std::declval<Publish &>()()),
            "runtime object graph publication must not throw");
        if (_stagedRuntimeObjectGraph) {
            auto obsoleteGraph = collectRuntimeObjectGraph(obsoleteObjects);
            build();
            publish();
            discardStagedRuntimeObjects(obsoleteGraph);
            return;
        }
        beginRuntimeObjectGraphReplacement(obsoleteObjects);
        std::vector<EquippedOnLoad> equipped;
        try {
            build();
            publish();
            equipped = commitRuntimeObjectGraphReplacement(obsoleteObjects);
        } catch (...) {
            abortRuntimeObjectGraphReplacement();
            throw;
        }
        releaseEquippedOnLoad(equipped);
    }

    inline std::shared_ptr<Module> newModule() {
        return newObject<Module>(*this, _services);
    }
    inline std::shared_ptr<Module> newSavedModule() {
        // The module is not part of the serialized object graph, but it still
        // needs a live identity: authored OnLoad scripts run with the module
        // as OBJECT_SELF and may schedule commands back onto it.
        return newObject<Module>(*this, _services);
    }
    inline std::shared_ptr<Item> newItem() {
        return newObject<Item>(*this, _services);
    }
    inline std::shared_ptr<Item> newPresentationItem() {
        return newPresentationObject<Item>(*this, _services);
    }
    // Items serialized inside an owning inventory/equipment record use an
    // owner-local identity scope. Their saved ObjectId can legitimately match
    // another owned item or an independently registered world object, so the
    // runtime registry must assign them a fresh unambiguous identity.
    inline std::shared_ptr<Item> newOwnedItem() {
        return newItem();
    }
    std::shared_ptr<Item> newOwnedItem(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    std::shared_ptr<Item> newItem(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    std::shared_ptr<Item> newItemFromBlueprint(const std::string &resRef);
    std::shared_ptr<Item> newItemClone(const Item &source);

    inline std::shared_ptr<Area> newArea(std::string sceneName = kSceneMain) {
        return newObject<Area>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Area> newSavedArea(
        uint32_t id,
        const SerializedIdentityContext &identityContext,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Creature> newCreature(std::string sceneName = kSceneMain) {
        return newObject<Creature>(std::move(sceneName), *this, _services);
    }
    inline std::shared_ptr<Creature> newPresentationCreature(
        std::string sceneName) {
        return newPresentationObject<Creature>(
            std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Creature> newCreature(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    std::shared_ptr<Creature> newCreatureFromBlueprint(
        const std::string &resRef,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Placeable> newPlaceable(std::string sceneName = kSceneMain) {
        return newObject<Placeable>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Placeable> newPlaceable(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    std::shared_ptr<Placeable> newPlaceableFromBlueprint(
        const std::string &resRef,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Door> newDoor(std::string sceneName = kSceneMain) {
        return newObject<Door>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Door> newDoor(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Waypoint> newWaypoint(std::string sceneName = kSceneMain) {
        return newObject<Waypoint>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Waypoint> newWaypoint(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    /** A waypoint made from its blueprint; one whose blueprint is missing is made all the same. */
    std::shared_ptr<Waypoint> newWaypointFromBlueprint(const std::string &resRef);

    inline std::shared_ptr<Trigger> newTrigger(std::string sceneName = kSceneMain) {
        return newObject<Trigger>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Trigger> newTrigger(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Sound> newSound(std::string sceneName = kSceneMain) {
        return newObject<Sound>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Sound> newSound(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<AnimatedCamera> newAnimatedCamera(std::string sceneName = kSceneMain) {
        return newObject<AnimatedCamera>(std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<DialogCamera> newDialogCamera(std::string sceneName = kSceneMain) {
        return newObject<DialogCamera>(std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<FirstPersonCamera> newFirstPersonCamera(float fovy, std::string sceneName = kSceneMain) {
        return newObject<FirstPersonCamera>(fovy, std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<StaticCamera> newStaticCamera(std::string sceneName = kSceneMain) {
        return newObject<StaticCamera>(std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<ThirdPersonCamera> newThirdPersonCamera(CameraStyle style, std::string sceneName = kSceneMain) {
        return newObject<ThirdPersonCamera>(std::move(style), std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<AreaOfEffect> newAreaOfEffect(std::string sceneName = kSceneMain) {
        return newObject<AreaOfEffect>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<AreaOfEffect> newAreaOfEffect(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Encounter> newEncounter(std::string sceneName = kSceneMain) {
        return newObject<Encounter>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Encounter> newEncounter(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Store> newStore(std::string sceneName = kSceneMain) {
        return newObject<Store>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Store> newStore(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    /** A store made from its blueprint; one whose blueprint is missing is made all the same. */
    std::shared_ptr<Store> newStoreFromBlueprint(const std::string &resRef);

    void prepareSavedRuntimeNamespace(const resource::Gff &ifo, const SerializedIdentityContext &identityContext);
    void restoreWorldTime(
        const resource::Gff &moduleIfo,
        uint64_t pauseDay,
        uint64_t pauseTime);
    void reserveSavedObjectIds(const resource::Gff &gff, const SerializedIdentityContext &identityContext, SerializedGraphRoot graphRoot);
    void resolveSavedObjectReferences();
    void bindSavedRuntimeState();
    void publishSavedRuntimeState();

    /**
     * World time as absolute elapsed world/simulation milliseconds.
     *
     * This is the canonical runtime clock. It advances with simulation dt and
     * is never rescaled; Mod_MinPerHour only changes how the calendar divides
     * it. The game saves store a day and a time of day instead, so that split
     * happens at the save and load boundaries and at a module start, which
     * keeps the day and time of day and counts them in the new module's hours.
     */
    uint64_t worldTimeMilliseconds() const { return _worldTimeMilliseconds; }
    /** Apply an effect to the target through the module's event queue, after the delay. */
    void queueEffectApplication(Object &target, EffectInstance effect, uint32_t delayMilliseconds = 0);
    /** SignalEvent: the event runs on the target through the module's event queue. */
    void queueScriptEvent(Object &target, std::optional<std::reference_wrapper<const Object>> caller, const Event &event);
    /** Remove an effect from the target through the module's event queue. */
    void queueEffectRemoval(Object &target, EffectId id);
    /**
     * Destroy the target after the delay. A destruction already pending for
     * it stays pending; whichever falls due first destroys it.
     */
    void postObjectDestruction(Object &target, float delay);
    /** World time after a script delay, or none when the delay is dropped. */
    std::optional<uint64_t> delayedWorldTime(float seconds) const;
    /** DelayCommand: a timed event that runs command as owner when due. */
    void postDelayedCommand(Object &owner, std::shared_ptr<Action> command, float seconds);
    /**
     * Visual programs 1601 and 1602 count their holders across all objects.
     * The speed blur turns on when the first holder is the player and turns
     * off when the last one goes; a minigame owns the blur while it runs.
     */
    void addMotionBlurProgram(bool onPlayer);
    void removeMotionBlurProgram();
    void setSpeedBlur(bool enabled);
    void setSpeedBlurRatio(float ratio);
    /**
     * Shows a videoeffects row over the world in place of the one showing:
     * its scan noise, its desaturation with channel modulation, Force Sight,
     * and the clairvoyance and fury overlays, each when the row enables it.
     * A negative row only takes the current one away. A row that enables any
     * of its parts becomes the current video effect.
     */
    void enableVideoEffect(int row);
    void disableVideoEffect();
    /**
     * In TSL the leader's Force Sight and Fury choose the video effect each
     * frame outside a conversation; their rows go away with the effects and
     * during a conversation.
     */
    void updateLeaderVideoEffect();
    /**
     * The same, asked for by a script. In TSL a script's effect is held: a
     * conversation shot without an effect of its own leaves it showing until
     * a script takes it away.
     */
    void enableScriptVideoEffect(int row);
    void disableScriptVideoEffect();
    bool isVideoEffectHeldByScript() const { return _scriptVideoEffectHeld; }
    /** A swoop race or turret session is running. */
    bool isMiniGameActive() const;
    /** DestroyObject: a destroy event for target after the delay. */
    void postObjectDestruction(Object &target, std::optional<std::reference_wrapper<const Object>> caller, float seconds, bool keepsCallerFade);
    /**
     * Every frame: the party falls when every member is down, and downed
     * members recover once no hostile has seen a member for a while.
     */
    void updateTemporaryDeath();
    /** Every frame: how fast the world runs while the game is over, and its end. */
    void updateDeathSequence();
    /** The fallen party's game is over. */
    void runDeathSequence();
    /** The title's death GUI: the sequel's gameover panel, or the first title's message. */
    void displayDeathMessage();
    bool isDeathMessageDisplayed() const;
    /** Menu keys pressed while the fallen party's game is over bring the fade to black forward. */
    void hurryDeathSequence();
    /** Menu keys while the controlled character lies dead hurry the death sequence. */
    bool handleDeathSequenceKey(const input::KeyEvent &event);
    /** The camera of a following death sequence orbits the last party member to fall. */
    void setLastPartyMemberTempKilled(const Creature &creature);
    /** Leave the game-over state, ending the game or cancelling a pending end. */
    void leaveGameOver(bool endGame) {
        _gameOver = false;
        _endGamePending = endGame;
        _endGameDelay = 0.0f;
        _showEndGameGui = true;
    }
    /**
     * EndGame: a script ends the game. With the end-game GUI the title's death
     * message comes up first; without it the game returns to the main menu on
     * the next frame.
     */
    void endGame(bool showGui);
    /**
     * StartCreditSequence: put up the closing credits, unless they are already
     * up. The area's music and ambient stop.
     */
    void startCreditSequence(bool transparentBackground, const std::string &music);
    bool isCreditSequenceInProgress() const { return static_cast<bool>(_credits); }
    /** How loading the most recent save after death went. */
    enum class LastSaveLaunch {
        NoSaves,
        Launched,
        Unavailable
    };
    /** Load the current character's most recent save instead of ending the game. */
    LastSaveLaunch launchMostRecentSave();
    /** Whether a panel holds the input instead of the world: a popup in play, or an exclusive screen. */
    bool hasModalPanel() const;
    /** End the game on the next frame, returning to the main menu. */
    void requestEndGame() {
        _endGamePending = true;
        _endGameDelay = 0.0f;
        _showEndGameGui = true;
    }

    uint8_t minutesPerHour() const { return _minutesPerHour; }

    /**
     * Whether the world currently being built came from loading a save from
     * disk, as opposed to a new game or an ordinary module transition.
     *
     * Authored scripts read this to skip entry work - spawns, cutscenes,
     * destruction - whose results the save already holds. It is true only
     * while the initial module of a save load is being restored, so an
     * ordinary transition or a revisit to a module the save already knows
     * reports false. An area's entered event runs its script with the answer
     * captured when the event was made.
     */
    bool isLoadingFromSaveGame() const { return _loadingFromSaveGame; }
    void setLoadingFromSaveGame(bool loading) { _loadingFromSaveGame = loading; }

    /**
     * Persist the current state of one roster NPC over its availnpc record.
     *
     * Scripts call this when a companion's state has to survive independently
     * of the party it is or is not currently in - the roster record is what a
     * later spawn reads back. Purely a write: membership, availability,
     * control and placement are all left alone, and a slot holding no creature
     * is silently nothing to save.
     */
    void saveNpcState(int npc);

    /** Persist one live creature as a detached PartyTable roster record. */
    void saveRosterState(
        const RosterIdentity &identity,
        const Creature &creature);

    /** Materialize an available, unbound slot from AVAILNPC/AVAILPUP. */
    std::shared_ptr<Creature> materializeRosterCreature(
        const RosterIdentity &identity);

    /** End a bound roster representation without changing availability. */
    bool killRosterCreature(const RosterIdentity &identity);

    /**
     * Length of a game day in world-time milliseconds.
     *
     * Mod_MinPerHour shortens the day - an in-game hour lasts that many
     * minutes of world time - it does not change the rate at which the clock
     * advances. The day length is
     * MinutesPerHour * 60 * 1000 * HOURS_IN_DAY and the raw timer accumulates
     * elapsed time.
     */
    uint32_t millisecondsPerWorldDay() const {
        return static_cast<uint32_t>(_minutesPerHour == 0 ? 5 : _minutesPerHour) *
               60u * 1000u * 24u;
    }

    /**
     * Calendar day, derived from the canonical clock. Calendar time is a real
     * engine concept - day/night cycles, NPC schedules, waiting - and not just
     * a detail of the save format.
     */
    uint32_t worldTimeDay() const {
        return static_cast<uint32_t>(
            _worldTimeMilliseconds / millisecondsPerWorldDay());
    }

    /** World milliseconds elapsed within the current calendar day. */
    uint32_t worldTimeOfDay() const {
        return static_cast<uint32_t>(
            _worldTimeMilliseconds % millisecondsPerWorldDay());
    }

    /** The hour of the current calendar day, from 0 to 23. */
    uint32_t worldTimeHour() const {
        return worldTimeOfDay() / (millisecondsPerWorldDay() / 24u);
    }
    /** World milliseconds since a calendar day and time; none for a moment still ahead. */
    uint64_t worldTimeSince(uint32_t day, uint32_t time) const {
        const uint64_t then = static_cast<uint64_t>(day) * millisecondsPerWorldDay() + time;
        return _worldTimeMilliseconds > then ? _worldTimeMilliseconds - then : 0;
    }
    /**
     * The world time from one calendar day and time back to another, as whole
     * days and a time of day in the current day length. When both times lie
     * within the current day and the first is the earlier, the result is left
     * as it was. A time beyond the current day length is taken as it stands.
     */
    void subtractWorldTimes(uint32_t day, uint32_t time, uint32_t sinceDay, uint32_t sinceTime,
                            uint32_t &days, uint32_t &timeOfDay) const {
        const uint32_t length = millisecondsPerWorldDay();
        if (time < length && sinceTime < length &&
            (day < sinceDay || (day == sinceDay && time < sinceTime)))
            return;
        days = day - sinceDay;
        timeOfDay = time - sinceTime;
        if (timeOfDay >= length) {
            --days;
            timeOfDay += length;
        }
    }
    std::optional<float> remainingEffectDuration(const EffectInstance &effect) const;

    template <class T>
    inline std::shared_ptr<T> getObjectById(uint32_t id) const {
        return std::dynamic_pointer_cast<T>(getObjectById(id));
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newObject(Args &&...args) {
        while (_objectById.count(_nextObjectId) ||
               _publishedRuntimeObjectIds.count(_nextObjectId) ||
               (_stagedRuntimeObjectGraph &&
                _stagedRuntimeObjectGraph->objectById.count(_nextObjectId)) ||
               _reservedSavedObjectIds.count(_nextObjectId)) {
            ++_nextObjectId;
        }
        return newObjectAtId<T>(_nextObjectId++, false, std::forward<Args>(args)...);
    }

    /** Construct a presentation-only object outside the gameplay registry. */
    template <class T, class... Args>
    inline std::shared_ptr<T> newPresentationObject(Args &&...args) {
        auto object = std::make_shared<T>(
            _nextPresentationObjectId--, std::forward<Args>(args)...);
        object->_runtimeState = Object::RuntimeState::Presentation;
        object->_runtimeIncarnation = _nextRuntimeIncarnation++;
        return object;
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newAction(Args &&...args) {
        return std::make_shared<T>(*this, _services, std::forward<Args>(args)...);
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newEffect(Args &&...args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    EffectId allocateEffectId() { return _effectIds.allocate(); }
    EffectIdImportResult importEffectId(EffectId id) { return _effectIds.importId(id); }
    bool setNextEffectId(EffectId id) { return _effectIds.setNextId(id); }
    EffectId nextEffectId() const { return _effectIds.nextId(); }
    bool hasEffectId(EffectId id) const { return _effectIds.contains(id); }
    size_t effectIdCount() const { return _effectIds.size(); }
    bool bindEffectCreator(EffectInstance &effect) const;
    bool bindSavedObjectReference(SavedObjectReference &reference) const;
    std::shared_ptr<Object> resolveSerializedObjectReference(
        uint32_t id,
        const SerializedIdentityContext &identityContext) const;
    std::shared_ptr<Object> getObjectBySavedId(uint32_t id) const;
    void registerSavedObjectIdentity(
        uint32_t id,
        const std::shared_ptr<Object> &object,
        const SerializedIdentityContext &identityContext);

    const SaveResourceShadows &saveResourceShadows() const {
        return _saveResourceShadows;
    }
    void captureSaveResourceShadow(
        SaveResourceKey key,
        const resource::Gff &source) {
        _saveResourceShadows.capture(std::move(key), source);
    }

    template <class... Args>
    inline std::shared_ptr<Event> newEvent(Args &&...args) {
        return std::make_shared<Event>(std::forward<Args>(args)...);
    }

    template <class... Args>
    inline std::shared_ptr<Location> newLocation(Args &&...args) {
        return std::make_shared<Location>(std::forward<Args>(args)...);
    }

    template <class... Args>
    inline std::shared_ptr<Talent> newTalent(Args &&...args) {
        return std::make_shared<Talent>(std::forward<Args>(args)...);
    }

    // END Objects

    // Global variables

    bool getGlobalBoolean(const std::string &name) const;
    int getGlobalNumber(const std::string &name) const;
    std::shared_ptr<Location> getGlobalLocation(const std::string &name) const;
    std::string getGlobalString(const std::string &name) const;

    struct GVCompare {
        bool operator()(const std::string &lhs, const std::string &rhs) const {
            return boost::algorithm::ilexicographical_compare(lhs, rhs);
        }
    };

    const std::map<std::string, std::string, GVCompare> &globalStrings() const { return _globalStrings; }
    const std::map<std::string, bool, GVCompare> &globalBooleans() const { return _globalBooleans; }
    const std::map<std::string, int, GVCompare> &globalNumbers() const { return _globalNumbers; }
    const std::map<std::string, std::shared_ptr<Location>, GVCompare> &globalLocations() const { return _globalLocations; }

    void setCustomToken(int token, std::string value);
    /**
     * Custom tokens up to this number carry the values of feedback lines and
     * the like: scripts cannot set them, and saves leave them out.
     */
    static constexpr int kLastReservedCustomToken = 9;
    /**
     * Resolves the tokens of a talk-table string for the party leader: custom
     * tokens, the tokens of stringtokens.2da (names, gender forms, race,
     * class and the like), "<<" and "{{" escapes, and {...} notes, which are
     * dropped. A token that resolves to nothing reads "<UNRECOGNIZED TOKEN>".
     */
    std::string substituteCustomTokens(std::string str) const;
    /**
     * An interface string: the talk-table string of strRef resolved as
     * substituteCustomTokens does. Talk-table strings read any other way keep
     * their tokens and {...} notes as written.
     */
    std::string getInterfaceText(int strRef) const;
    /**
     * As getInterfaceText, after setting the given custom tokens to the given
     * values. The tokens keep those values afterwards, as the tokens set for a
     * feedback line do.
     */
    std::string getInterfaceText(int strRef, const std::map<int, std::string> &tokens);
    /**
     * A feedback line's text: the talk-table string of strRef resolved with
     * no subject, so a token that names a creature (its name, race, class,
     * alignment or gender form) reads its default, and with actions shown.
     */
    std::string getFeedbackText(int strRef) const;
    /**
     * As getFeedbackText, after setting the given custom tokens to the given
     * values, which they keep afterwards.
     */
    std::string getFeedbackText(int strRef, const std::map<int, std::string> &tokens);
    /**
     * As substituteCustomTokens, for text shown with its actions hidden: the
     * text of <StartAction> and <StartCheck> sections, up to </Start>, is
     * hidden. Used for message-screen and journal lines, item names and
     * descriptions, and the names shown over a target and in the menus.
     */
    std::string substituteLogTokens(std::string str) const;
    /** As substituteLogTokens, with the tokens resolved for the given creature. */
    std::string substituteLogTokens(std::string str, const Creature &subject) const;

    void setGlobalBoolean(const std::string &name, bool value);
    void setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location);
    void setGlobalNumber(const std::string &name, int value);
    void setGlobalString(const std::string &name, const std::string &value);

    // END Global variables

    std::map<int, std::string> parseCustomTokens(
        const resource::Gff &ifoGff) const;
    void replaceCustomTokens(std::map<int, std::string> tokens);
    PreparedSaveLoad prepareSaveLoad(const resource::SaveSlotDescriptor &slot);
    PreparedDestinationModule prepareDestinationModule(
        const std::string &name,
        bool initialSaveRestore,
        std::shared_ptr<const resource::SaveWorkingState> workingState);
    bool restoreSaveLoad(PreparedSaveLoad prepared);
    bool loadPreparedModule(
        PreparedDestinationModule prepared,
        std::string entry,
        bool initialSaveRestore,
        bool resourcesCommitted,
        std::shared_ptr<const resource::SaveWorkingState> sourceWorkingState = nullptr);
    void validatePreparedDestination(
        const PreparedDestinationModule &prepared) const;
    void validatePartyLoad(const resource::Gff *partyTable) const;
    void retireToMainMenu();

    void deserializeGlobalVariables(resource::Gff &gvtGff);
    void deserializeParty(
        resource::Gff &ifoGff,
        const std::shared_ptr<resource::Gff> &ptGff,
        const SerializedIdentityContext &moduleIdentityContext);
    void publishPartyRuntimeState(
        resource::Gff &ifoGff,
        const std::shared_ptr<resource::Gff> &ptGff,
        const std::shared_ptr<resource::Gff> &pcGff,
        const SerializedIdentityContext &moduleIdentityContext);
    /** The party table's contents: the party's state and the message lists saved with it. */
    struct PartyTable {
        Party::PersistedState party;
        std::vector<MessageLog::DialogEntry> dialogMessages;
        std::vector<MessageLog::Entry> logMessages;
    };
    PartyTable parsePartyTable(const resource::Gff &ptGff) const;
    void replacePartyTable(PartyTable table);
    void deserializePazaakPartyTable(resource::Gff &ptGff);
    void deserializeGalaxyMap(resource::Gff &ptGff);
    void resetGalaxyMap();
    void serializePazaakPartyTable(resource::Gff &ptGff) const;
    void deserializePartyMembers(resource::Gff &ptGff);
    void deserializeJournal(const resource::Gff &ptGff);
    void deserializeInventory(resource::Gff &inventoryGff);

private:
    friend class Area;
    friend class Object;
    friend class TestGameModule;
    friend class ModuleSnapshotBuilder;
    friend class SaveWideSnapshotBuilder;
    friend struct SerializedScriptSituation;
    friend class SavedScriptContinuation;

    /** No videoeffects row is the current effect. */
    static constexpr int kNoVideoEffect = -2;
    /** A menu attack within this many seconds of the last one is a repeated attack. */
    static constexpr float kAttackMashWindow = 3.0f;

    /**
     * A video effect (videoeffects.2da) in this game's colours: the overlays it
     * lays, its fury level and its desaturation. A blank cell is zero.
     */
    struct VideoEffectRow {
        /** Whether the effect lays scan noise. */
        bool scanNoise {false};
        /** Whether the effect desaturates the screen. */
        bool saturation {false};
        /** Either clairvoyance column lays the same overlay. */
        bool clairvoyance {false};
        /** Whether the effect lays the Force Sight overlay. */
        bool forceSight {false};
        /** The effect's fury level. */
        int fury {0};
        /** The colour the screen is modulated by. */
        glm::vec3 modulation {0.0f};
        /** How much colour is kept while desaturated. */
        float saturationAmount {0.0f};
    };

    /** A dialogue text token (stringtokens.2da). A blank cell is -1. */
    struct StringTokenRow {
        /** The strings the token may give, strref1 to strref4. */
        std::array<int, 4> strRefs {-1, -1, -1, -1};
        /** The string the token gives when none of the others applies. */
        int defaultStrRef {-1};
        /** The action the token's strings depend on. */
        int action {-1};
    };

    /** The keys an input event takes by default. */
    struct InputEventKeys {
        /** The key. */
        int key {0};
        /** The alternate key of a two-key event; none for a single-key one. */
        std::optional<int> alternateKey;
    };

    /** A tutorial window (tutorial.2da): this game's message of each page, none where blank, and its icon. */
    struct TutorialRow {
        /** The message of each page, none where blank. */
        std::vector<std::optional<int>> messages;
        /** The window's icon. */
        std::string icon;
    };

    resource::GameID _gameId;
    std::filesystem::path _path;
    OptionsView &_options;
    ServicesView &_services;
    IConsole &_console;

    // Tables

    std::vector<VideoEffectRow> _videoEffects;
    /** By token; the first row of a token is the one used. */
    std::unordered_map<std::string, StringTokenRow> _stringTokens;
    /** The input events, by number: the built-in ones and keymap.2da's. */
    std::unordered_map<int, InputEventKeys> _inputEvents;
    /** The key-name strings of bindablekeys.2da, by keyboard key from the first. */
    std::vector<int> _keyNameStrRefs;
    std::vector<TutorialRow> _tutorials;
    /** The feedbacktext.2da strings; none where blank. */
    std::vector<std::optional<int>> _feedbackTexts;
    /** The trap types of traps.2da, by row. */
    std::vector<TrapTypeRow> _trapTypes;

    // END Tables

    Screen _screen {Screen::None};
    std::map<int, float> _baseItemCostMultipliers;
    bool _soloModeQueryOpen {false};
    bool _freeLook {false};
    bool _playerInputBlocked {false};
    /** Whether the alternate actions key, either Shift, was last pressed rather than released. */
    bool _alternateActionsHeld {false};
    /** The world time a click was last shut off at, and for how long. */
    uint32_t _noClickDay {0};
    uint32_t _noClickTime {0};
    uint32_t _noClickMilliseconds {0};
    /** The videoeffects row that is the current effect, whether or not any of its parts is drawn. */
    int _videoEffectType {kNoVideoEffect};
    bool _scriptVideoEffectHeld {false};
    RuntimeObjectRef<Creature> _postDialogCharacterSwitch;

    struct DeveloperOverlay {
        bool visible {false};
        bool triggers {true};
        bool actorLabels {true};
        bool longActorLabels {false};
        bool watchedValues {true};
    };

    DeveloperOverlay _developerOverlay;
    std::shared_ptr<graphics::Font> _developerFont;

    // Non-blocking minigame lifecycle session: origin module/state -> minigame
    // module -> auto-start -> forced-success finish -> return to origin. Passive
    // bookkeeping only; it does not touch party membership, inventory, or story.
    struct MinigameLifecycle {
        bool active {false};        // a lifecycle session is in progress (return pending)
        bool haveOrigin {false};    // origin position/facing captured
        std::string originModule;   // module resref to return to
        glm::vec3 originPosition {0.0f};
        float originFacing {0.0f};
        bool forcedSuccess {true}; /**< Finish is always non-blocking success */
    };

    MinigameLifecycle _swoopLifecycle;
    MinigameLifecycle _turretLifecycle;

    // A turret session scheduled by the startturretgame console command. The
    // transition goes through the normal deferred module load, so whether the
    // target really is a turret area is only known once it has loaded; this
    // carries the return origin across that gap.
    struct PendingTurretRequest {
        bool active {false};
        std::string targetModule;
        std::string originModule;
        glm::vec3 originPosition {0.0f};
        float originFacing {0.0f};
        bool haveOrigin {false};
    };

    PendingTurretRequest _pendingTurret;

    std::shared_ptr<movie::IMovie> _movie;
    std::queue<std::string> _moduleTransitionMovies;
    std::shared_ptr<PresentationPointer> _pointer;
    float _gameSpeed {1.0f};
    CameraType _cameraType {CameraType::ThirdPerson};
    CameraType _savedCameraType {CameraType::ThirdPerson};
    bool _paused {false};
    int _motionBlurPrograms {0};
    bool _autoPaused {false};
    PauseReason _pauseReason {PauseReason::Other};

    // Target presentation

    struct HostileHilite {
        RuntimeObjectRef<Creature> creature;
        float timer {0.0f};
    };

    bool _lastTargetLook {false};
    float _attackMashTime {0.0f};
    std::set<uint32_t> _dialogOrientationLocks;
    std::set<uint32_t> _dialogHeadFollowLocks;
    bool _leaderAnswersInDialog {false};
    /** See isDialogUnderWay. */
    bool _dialogUnderWay {false};
    std::vector<HostileHilite> _hostileHilites;

    void showTarget(const std::shared_ptr<Object> &object, bool look, bool camera);
    void updateHostileHilites(float dt);

    // END Target presentation

    // Tutorial windows

    /** A tutorial window and the action it interrupted, to be taken once it is dismissed. */
    struct TutorialRequest {
        int id {0};
        uint32_t actor {script::kObjectInvalid};
        uint32_t subject {script::kObjectInvalid};
        uint32_t param {0};
    };

    /** The window asked for this frame; it opens on the next. */
    std::optional<TutorialRequest> _tutorialPending;
    /** The window open, if any. */
    std::optional<TutorialRequest> _tutorial;
    int _tutorialPage {0};
    /** The open window paused play, and resumes it when dismissed. */
    bool _tutorialPausedGame {false};

    bool isTutorialWindowValid(int id) const;
    void commitTutorialWindow();
    bool showTutorialPage();
    void finishTutorialWindow(bool takeAction);

    // END Tutorial windows
    float _partyKilledAutoPauseDelay {0.0f};
    /** Seconds left in which a requested autopause is deferred. */
    float _autoPauseDelayWindow {0.0f};
    /** Seconds until the deferred autopause pauses; none is waiting at 0 or less. */
    float _deferredAutoPauseDelay {0.0f};
    /** The reason of the deferred autopause. */
    AutoPauseReason _deferredAutoPauseReason {AutoPauseReason::EnemySighted};

    /**
     * Pauses for \p reason over live play that is not paused already, or
     * defers the pause while the delay window is open.
     *
     * @param reason the situation, already allowed by the autopause options
     */
    void autoPause(AutoPauseReason reason);
    /** Counts the deferred autopause down and pauses when it is due, or else counts the delay window down. */
    void updateDeferredAutoPause(float frameTime);

    bool _enemySighted {false};
    float _enemySightingHold {0.0f};
    bool _mineSighted {false};
    float _mineSightingHold {0.0f};

    /** An object around the leader that can hold the target. */
    struct NearestObject {
        RuntimeObjectRef<Object> object;
        bool inCone {false};
        bool hostile {false};
        /** Whether the leader sees it, once asked. */
        std::optional<bool> seen;
    };

    /** How the leader's target came to be held this pass, which decides how it is presented. */
    struct TargetPass {
        std::shared_ptr<Object> target;
        bool keep {false};
        bool sighted {false};
        bool repicked {false};
        bool unseenGrace {false};
    };

    /** The objects around the leader that can hold the target, in bearing order. */
    std::vector<NearestObject> _nearestObjects;
    /**
     * A creature that goes down while it is the target stops holding it; its
     * lootable remains stand in for a separate body and are a new candidate.
     */
    RuntimeObjectRef<Object> _passTarget;
    bool _passTargetUp {false};
    bool _passTargetGone {false};
    float _targetUnseenTime {0.0f};
    float _deadTargetHold {0.0f};
    float _targetMoveTime {0.0f};
    bool _clientCombatMode {false};
    RuntimeObjectRef<Creature> _clientCombatLeader;
    RuntimeObjectRef<Area> _clientCombatArea;
    bool _keepStealthInDialog {false};
    bool _timingDiscontinuity {false};
    GlobalFade _globalFade;
    GlobalFade::ArrivalTicket _fadeArrival;
    std::weak_ptr<Module> _fadeArrivalModule;
    std::set<std::string> _moduleNames;
    std::set<std::string> _saveNames;
    bool _quitRequested {false};
    bool _relativeMouseMode {false};
    bool _showImGui {false};

    static constexpr uint32_t kFirstRuntimeObjectId = 2; // ids 0 and 1 are reserved
    uint32_t _nextObjectId {kFirstRuntimeObjectId};
    uint32_t _factionJoinOrder {0};
    std::map<uint32_t, std::shared_ptr<Object>> _objectById;
    // A numeric runtime ID names at most one incarnation during a runtime
    // session. Saved cursors and developer-specified IDs may move allocation
    // backwards, but cannot revive stale numeric gameplay references.
    std::set<uint32_t> _publishedRuntimeObjectIds;
    std::map<uint32_t, std::weak_ptr<Object>> _objectBySavedId;
    // One authoritative graph object has one canonical saved identity. Roster
    // doubles live in detached graphs and never create cross-graph aliases.
    std::map<const Object *, uint32_t> _savedIdByObject;
    /**
     * An item a creature was read wearing, and its slot. The wearer is the
     * item's owner; the module is told once both are live.
     */
    struct EquippedOnLoad {
        int slot {0};
        std::shared_ptr<Item> item;
    };
    struct StagedRuntimeObjectGraph {
        uint32_t initialNextObjectId {kFirstRuntimeObjectId};
        std::map<uint32_t, std::shared_ptr<Object>> objectById;
        std::set<uint32_t> publishedRuntimeObjectIds;
        std::map<uint32_t, std::weak_ptr<Object>> objectBySavedId;
        std::map<const Object *, uint32_t> savedIdByObject;
        std::set<uint32_t> replaceableObjectIds;
        std::set<uint32_t> reservedSavedObjectIdsToRelease;
        std::vector<std::shared_ptr<Object>> obsoleteGraph;
        std::vector<std::shared_ptr<Object>> candidateObjects;
        std::vector<EquippedOnLoad> equippedOnLoad;
    };
    std::optional<StagedRuntimeObjectGraph> _stagedRuntimeObjectGraph;
    /** Equipment read while a module loads, waiting for its place in the load. */
    std::vector<EquippedOnLoad> _equippedOnLoad;
    uint64_t _nextRuntimeIncarnation {1};
    uint32_t _nextPresentationObjectId {
        std::numeric_limits<uint32_t>::max() - 1};
    std::set<uint32_t> _reservedSavedObjectIds;
    std::optional<std::string> _reservedSavedIdentityNamespace;
    std::map<uint32_t, std::string> _reservedSavedObjectIdClaims;
    EffectIdNamespace _effectIds;
    bool _runtimeSessionPlayable {false};
    bool _cheatUsed {false};
    uint64_t _runtimeSessionGeneration {1};
    uint64_t _savedGraphGeneration {1};
    bool _loadingFromSaveGame {false};
    uint64_t _worldTimeMilliseconds {0};
    uint8_t _minutesPerHour {5};
    double _worldTimeFraction {0.0};
    bool _timeStopped {false};
    std::vector<RuntimeObjectRef<Object>> _timeStopExclusions;
    uint64_t _timeStopMilliseconds {0};
    double _timeStopFraction {0.0};
    std::optional<uint64_t> _worldClockSample;
    double _playedTimeFraction {0.0};

    std::optional<SaveRequest> _pendingSave;
    std::optional<SaveResult> _lastSaveResult;
    uint64_t _nextSaveRequestId {1};
    bool _saveInProgress {false};
    bool _transitionInProgress {false};
    bool _atStableSavePoint {false};
    SaveOrchestrationSeams _saveSeams;
    graphics::Texture *_lastRenderedSceneOutput {nullptr};

    // Services

    RuntimeObjectRef<Object> _lastTarget;
    Party _party;
    Combat _combat;
    SwoopRace _swoopRace;
    Turret _turret;
    Journal _journal;
    MessageLog _messageLog;
    FloatingText _floatingText;
    StatusSummaryAccumulator _statusSummary;

    std::unique_ptr<script::IRoutines> _routines;
    std::unique_ptr<ScriptRunner> _scriptRunner;

    // END Services

    // GUI

    std::unique_ptr<MainMenu> _mainMenu;
    std::unique_ptr<CharacterGeneration> _charGen;
    std::unique_ptr<HUD> _hud;
    bool _captureHUDPresentation {false};
    std::unique_ptr<InGameMenu> _inGame;
    std::unique_ptr<DialogGUI> _dialog;
    std::unique_ptr<ComputerGUI> _computer;
    std::unique_ptr<ConfirmPopup> _confirmPopup;
    std::unique_ptr<ConfirmPopup> _deathMessage;
    std::unique_ptr<DeathDisplay> _deathDisplay;
    /** The closing credits, while they are up. */
    std::unique_ptr<CreditsGUI> _credits;
    TemporaryDeathRecovery _temporaryDeathRecovery;
    bool _gameOver {false};
    bool _endGamePending {false};
    /**
     * While an end is pending and this delay is positive, the death GUI stays
     * up when the end asked for it. The first title counts the delay down in
     * world time; the sequel waits for a gameover panel button.
     */
    float _endGameDelay {0.0f};
    bool _showEndGameGui {true};
    uint32_t _lastPartyMemberTempKilled {script::kObjectInvalid};
    /**
     * How fast the world, its presentation and the fade run: a quarter while
     * the second title's gameover panel is up, else full speed. The first
     * title's death sequence keeps full speed.
     */
    float _deathTimeScale {1.0f};
    std::unique_ptr<ContainerGUI> _container;
    std::unique_ptr<PartySelection> _partySelect;
    std::unique_ptr<SaveLoad> _saveLoad;
    std::unique_ptr<GalaxyMap> _galaxyMap;
    std::unique_ptr<PazaakWagerGUI> _pazaakWager;
    std::unique_ptr<PazaakSetupGUI> _pazaakSetup;
    std::unique_ptr<PazaakBoardGUI> _pazaakBoard;

    std::unique_ptr<PazaakSession> _pazaakSession;
    std::optional<PazaakCompletedResult> _lastPazaakResult;
    Screen _pazaakOriginScreen {Screen::None};
    bool _pazaakGUIsReady {false};
    bool _pazaakDevelopmentLaunch {false};
    bool _pazaakSelectionPersisted {false};
    bool _pazaakSettlementApplied {false};
    bool _pazaakShowcaseHands {false};
    float _pazaakOpponentEventElapsed {0.0f};

    // Narrow injectable seams used by focused lifecycle tests.
    PazaakSession::HandSelector _pazaakPlayerHandSelector;
    PazaakSession::HandSelector _pazaakOpponentHandSelector;
    PazaakSession::MainDeckFactory _pazaakMainDeckFactory;
    PazaakSession::FirstParticipantSelector _pazaakFirstParticipantSelector;
    bool _pazaakPaceAutomaticDraws {true};
    std::function<bool()> _pazaakGuiLoadOverride;
    std::function<void(const std::string &, uint32_t)> _pazaakContinuationOverride;
    RuntimeObjectRef<Object> _pazaakDevelopmentSelectedObjectOverride;
    std::optional<pazaak::SideDeck> _pazaakOpponentDeckOverride;

    std::unique_ptr<Map> _map;
    std::unique_ptr<LoadingScreen> _loadScreen;

    Conversation *_conversation {nullptr}; /**< pointer to either DialogGUI or ComputerGUI  */
    Conversation::AutoSkip _conversationAutoSkip;

    // END GUI

    // Modules

    std::string _nextModule;
    std::string _nextEntry;
    std::shared_ptr<Module> _module;
    std::map<std::string, std::shared_ptr<Module>> _loadedModules;

    // END Modules

    // Audio

    std::string _musicResRef;
    std::shared_ptr<audio::AudioSource> _music;
    AreaMusicPlayer _areaMusic;
    /**
     * The music loading screens play. It is set by a new game, a save load
     * and the module picker, and kept from one load, and one session, to the
     * next.
     */
    std::string _loadMusicResRef;
    /** The loading-screen music that is playing, or paused under a movie. */
    std::shared_ptr<audio::AudioSource> _loadMusic;
    /** The coming module load follows transition movies, and plays no loading-screen music. */
    bool _loadComesWithMovies {false};

    // END Audio

    // Global variables

    std::map<std::string, std::string, GVCompare> _globalStrings;
    std::map<std::string, bool, GVCompare> _globalBooleans;
    std::map<std::string, int, GVCompare> _globalNumbers;
    std::map<std::string, std::shared_ptr<Location>, GVCompare> _globalLocations;
    std::map<int, std::string> _customTokens;
    SaveResourceShadows _saveResourceShadows;

    // END Global variables

    /**
     * Resolve the tokens and notes of a talk-table string for the subject, if
     * any. The hiding state is shared by nested parses of interface text.
     */
    std::string parseTokens(const std::string &text, const std::map<int, std::string> &customTokens,
                            std::optional<std::reference_wrapper<const Creature>> subject, bool hideActions,
                            bool &hidden) const;
    /** The value of one token, by its name: a custom token or a row of stringtokens.2da. */
    std::string getTokenValue(const std::string &name, const std::map<int, std::string> &customTokens,
                              std::optional<std::reference_wrapper<const Creature>> subject, bool hideActions,
                              bool &hidden) const;

    /** TSL: runs the level-up dialog script the first time only. */
    void runLevelUpDialogOnce();

    void advanceWorldTime(double dt);
    void advanceTimeStopClock(double dt);
    void advancePlayedTime(float dt);
    std::shared_ptr<const resource::SaveWorkingState>
    prepareCurrentModuleWorkingState();
    void processPendingSave();
    void finalizeSaveRequest(const SaveRequest &request, SaveResult result);
    SaveResult executeSave(SaveRequest request);
    SaveMetadataInput buildSaveMetadata(const SaveRequest &request) const;
    resource::SaveSlotDescriptor saveTarget(const SaveRequest &request) const;
    std::map<std::string, ByteBuffer> currentLooseSavePassthrough() const;
    std::optional<ByteBuffer> captureSaveScreenshot();

    uint32_t savedObjectId(const resource::Gff &gff) const;
    void retireSavedObjectGraph();
    void registerSavedModuleReferenceTarget(
        const std::shared_ptr<Module> &module,
        const SerializedIdentityContext &identityContext);
    void registerObject(
        const std::shared_ptr<Object> &object,
        bool allowReserved);
    void beginRuntimeObjectGraphReplacement(
        const std::vector<std::shared_ptr<Object>> &obsoleteObjects);
    /** Publishes the staged graph; returns the equipment its creatures were read wearing. */
    std::vector<EquippedOnLoad> commitRuntimeObjectGraphReplacement(
        const std::vector<std::shared_ptr<Object>> &obsoleteObjects);
    /**
     * Each item a creature was read wearing tells the module, in the order
     * read. While a module loads, the items wait for their place in the load.
     */
    void releaseEquippedOnLoad(const std::vector<EquippedOnLoad> &equipped);
    /** The live creature wearing equipment read on load, if any. */
    std::shared_ptr<Creature> equippedOnLoadWearer(const EquippedOnLoad &equipped) const;
    /** The arriving party tells the module of everything it wears. */
    void signalPartyEquipment();
    /** The equipment read during a load that no announcement has told yet tells the module. */
    void signalKeptEquippedOnLoad();
    void abortRuntimeObjectGraphReplacement();
    void unregisterRuntimeObject(const std::shared_ptr<Object> &object);
    bool isRuntimeObjectAttachable(const Object &object) const;
    std::vector<std::shared_ptr<Object>> collectRuntimeObjectGraph(
        const std::vector<std::shared_ptr<Object>> &roots) const;
    void discardStagedRuntimeObjects(
        const std::vector<std::shared_ptr<Object>> &objects);
    void retireActiveAreaRuntime();

    template <class T, class... Args>
    inline std::shared_ptr<T> newObjectAtId(
        uint32_t id,
        bool allowReserved,
        Args &&...args) {
        auto object = std::make_shared<T>(id, std::forward<Args>(args)...);
        registerObject(object, allowReserved);
        return object;
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newObjectFromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        Args &&...args) {
        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<T> object;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                object = newObject<T>(std::forward<Args>(args)...);
                if (identityContext.hasAuthoritativeObjectIds()) {
                    registerSavedObjectIdentity(
                        savedObjectId(gff), object, identityContext);
                }
                object->deserialize(gff, identityContext);
            },
            []() noexcept {});
        return object;
    }
    /**
     * What a resident party creature still had queued when it left the
     * source module, in save-facing form, to be published in the destination.
     */
    struct PartyTransitionState {
        RuntimeObjectRef<Creature> creature;
        SavedActionQueue actions;
        std::vector<bool> actionReferencesBound;
    };

    void clearDepartingActions();
    std::vector<PartyTransitionState> captureResidentPartyActions();
    /** End the source module's session: minigames, the conversation and the module's runtime. */
    void endSourceSession(const std::string &destination);
    /** Create the destination module and make it the current one. */
    void publishDestinationModule(const std::string &name, const resource::Gff &ifo, bool restoringSavedWorld);
    void restorePartyTransitionState(std::vector<PartyTransitionState> &states);

    void loadDefaultParty();
    bool loadParty();
    void loadNextModule();
    void playMusic(const std::string &resRef);
    /** Starts, or resumes, the loading-screen music, as a loading screen does when it is shown. */
    void startLoadMusic();
    /** Hands the playing loading-screen music over to the background music when it asks for the same track. */
    std::shared_ptr<audio::AudioSource> takeLoadMusic(const std::string &resRef);
    void toggleInGameCameraType();
    void enterFreeLook();
    void exitFreeLook();
    bool handleFreeLookKey(const input::KeyEvent &event);
    void updateFreeLookExits();

    // Stop the active lifecycle race and return to the stored origin module
    // (restoring the leader's position/facing). Safe no-op if no lifecycle race.
    void finishSwoopLifecycle(bool success);

    // Same, for the turret minigame. The outcome is carried through rather than
    // reduced to a success flag: a win, a loss and an abandoned session all
    // return to the origin, but only a win emits the completion state.
    void finishTurretLifecycle(Turret::Outcome outcome);

    /**
     * Apply post-turret globals for K1 M12ab after a victory.
     * Other modules are unchanged.
     */
    void applyTurretResult(const std::string &turretModule, Turret::Outcome outcome);

    // Give up on a scheduled turret session and go back where it started.
    void abandonPendingTurret(const std::string &reason);

    // Send the party back to a lifecycle session's origin, restoring the
    // leader's recorded position and facing.
    void returnToLifecycleOrigin(const std::string &module,
                                 bool haveOrigin,
                                 const glm::vec3 &position,
                                 float facing);

    /** Show or hide active party creatures while a minigame represents the player. */
    void setPartyVisible(bool visible);

    /** Return the race-end waypoint tag for the module, or an empty string if unknown. */
    std::string swoopReturnWaypoint(const std::string &raceModule) const;

    /**
     * Record a winning finish time for K1 Taris so the post-race script can
     * process the result. Other planets are unchanged.
     */
    void applySwoopForcedSuccessResult(const std::string &raceModule);
    void applyTarisForcedWinningTime();

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseButtonUp(const input::MouseButtonEvent &event);
    bool handleDeveloperKeyDown(const input::KeyEvent &event);

    void onModuleSelected(const std::string &name);
    void renderHUD();

    GameGUI *getScreenGUI() const;
    CameraType getConversationCamera(int &cameraId) const;

    // Updates

    /** Start a movie; \p streamingMusic lets area music set to play through movies play on. */
    bool startVideo(const std::string &name, bool streamingMusic);
    bool playNextModuleTransitionMovie();
    void updateMovie(float dt);
    void updateMusic();
    void updatePassiveSelection(float frameTime);
    void releaseTargetOnMove(float frameTime);
    void selectPassTarget(TargetPass &pass, const Creature &leader, const Area &area, float frameTime);
    bool holdsDeadTarget(const std::shared_ptr<Object> &target, float frameTime);
    void takeSightedTarget(TargetPass &pass, bool hostileCreature, bool hostileTrigger,
                           const Creature &leader, const Area &area, float frameTime);
    bool isTargetLost(TargetPass &pass, const Creature &leader, const Area &area, float frameTime);
    std::shared_ptr<Object> pickTarget(const std::vector<size_t> &cone, bool hostileCreature,
                                       const Creature &leader, const Area &area);
    void presentPassTarget(const TargetPass &pass);
    std::vector<size_t> collectNearestObjects(const Creature &leader, const Area &area);
    bool isNearestObjectSeen(NearestObject &entry, const Creature &leader, const Area &area);
    bool isDownForSelection(const Creature &creature) const;
    void updateCamera(float dt);
    void updateSceneGraph(float dt);
    void updateImGui(float dt);

    // END Updates

    // Rendering

    void renderScene();
    void renderGUI();
    void renderGlobalFade();
    void settleFadeArrival();
    void renderDeveloperOverlay();
    void renderDeveloperBanner();
    void renderDeveloperTriggerOverlay(const glm::mat4 &projection, const glm::mat4 &view);
    void renderDeveloperActorLabels(const glm::mat4 &projection, const glm::mat4 &view);
    void renderDeveloperWatchedValues();
    void renderDeveloperText(const std::string &text, const glm::vec3 &position, const glm::vec3 &color, graphics::TextGravity gravity = graphics::TextGravity::LeftTop);
    void renderDeveloperPanel(const std::vector<std::string> &lines, glm::vec2 position, glm::vec3 color);
    void renderDeveloperRect(glm::vec2 position, glm::vec2 size, glm::vec4 color);

    // END Rendering

    // GUI

    void loadInGameMenus();
    bool loadPazaakGUIs();
    bool startPazaakFlow(
        PazaakSessionParams params,
        bool developmentLaunch);
    bool startDevelopmentPazaak(std::string opponentName, int maximumWager = 0);
    void finishPazaak(PazaakCompletedResult result);
    void releasePazaakFlow(bool restoreOrigin);
    Screen safePazaakOriginScreen() const;

    void changeScreen(Screen screen);

    /** Runs \p block behind the loading screen, showing the loading picture of the module named \p moduleName. */
    void withLoadingScreen(const std::string &moduleName, const std::function<void()> &block);

    template <class T>
    std::unique_ptr<T> tryLoadGUI() {
        auto gui = std::make_unique<T>(*this, _services);
        try {
            gui->init();
            return gui;
        } catch (const std::exception &e) {
            error(str(boost::format("Error loading GUI: %s") % std::string(e.what())));
            return nullptr;
        }
    }

    // END GUI

    // Console commands

    void initConsole();
    void initJournalNotifications();
    /** Read the video effects, dialogue tokens, tutorial windows and feedback texts, and the party's tables. */
    void initTables();
    /** Builds the input events and their default keys, and reads the key names. */
    void initInputEvents();
    int getPlotXPByIndex(int plotIndex);

    using ConsoleCommandHandler = void (Game::*)(const ConsoleArgs &);
    void registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler);

    /**
     * Returns the currently selected object, or the party leader.
     */
    std::shared_ptr<Object> getConsoleTargetObject();

    /**
     * Returns the currently selected Creature object, or the party leader.
     */
    std::shared_ptr<Creature> getConsoleTargetCreature();

    /**
     * Returns the leader creature.
     */
    std::shared_ptr<Creature> getConsoleLeader();

    /**
     * Returns the current Area.
     */
    std::shared_ptr<Area> getConsoleArea();

    void consoleInfo(const ConsoleArgs &tokens);
    void consoleListGlobals(const ConsoleArgs &tokens);
    void consoleListLocals(const ConsoleArgs &tokens);
    void consoleListAnim(const ConsoleArgs &tokens);
    void consolePlayAnim(const ConsoleArgs &tokens);
    void consoleKill(const ConsoleArgs &tokens);
    void consoleAddItem(const ConsoleArgs &tokens);
    void consoleGiveXP(const ConsoleArgs &tokens);
    void consoleGiveGold(const ConsoleArgs &tokens);
    void consoleWarp(const ConsoleArgs &tokens);
    void consoleCamera(const ConsoleArgs &tokens);
    void consoleCamPos(const ConsoleArgs &tokens);
    void consoleCamLook(const ConsoleArgs &tokens);
    void consoleCamStatus(const ConsoleArgs &tokens);
    void consoleOpenMenu(const ConsoleArgs &tokens);
    void consoleOpenCharacterGeneration(const ConsoleArgs &tokens);
    void consoleSkipMovie(const ConsoleArgs &tokens);
    void consoleShowBark(const ConsoleArgs &tokens);
    void consoleShowPopup(const ConsoleArgs &tokens);
    void consoleShowGalleryMode(const ConsoleArgs &tokens);
    void consoleSeed(const ConsoleArgs &tokens);
    void consoleGraphics(const ConsoleArgs &tokens);
    void consoleShowHUD(const ConsoleArgs &tokens);
    void consoleShowTransition(const ConsoleArgs &tokens);
    void consoleOpenContainer(const ConsoleArgs &tokens);
    void consoleSelectDialogOption(const ConsoleArgs &tokens);
    void consoleRunScript(const ConsoleArgs &tokens);
    void consoleShowAABB(const ConsoleArgs &tokens);
    void consoleShowWalkmesh(const ConsoleArgs &tokens);
    void consoleShowTriggers(const ConsoleArgs &tokens);
    void consoleSpawnCreature(const ConsoleArgs &tokens);
    void consoleSpawnCompanion(const ConsoleArgs &tokens);
    void consoleAddAvailableNpc(const ConsoleArgs &tokens);
    void consoleSelectObjectById(const ConsoleArgs &tokens);
    void consoleSelectObjectByTag(const ConsoleArgs &tokens);
    void consoleSelectLeader(const ConsoleArgs &tokens);
    void consoleSetFaction(const ConsoleArgs &tokens);
    void consoleSetPosition(const ConsoleArgs &tokens);
    void consoleProfessionalTools(const ConsoleArgs &tokens);
    void consoleKillRoom(const ConsoleArgs &tokens);
    void consoleAutoSkipEnable(const ConsoleArgs &tokens);
    void consoleAutoSkipEntries(const ConsoleArgs &tokens);
    void consoleAutoSkipReplies(const ConsoleArgs &tokens);
    void consoleStartConversation(const ConsoleArgs &tokens);
    void consoleCutsceneAttack(const ConsoleArgs &tokens);
    void consoleSetAbility(const ConsoleArgs &tokens);
    void consoleSetSkill(const ConsoleArgs &tokens);
    void consoleAddOrRemoveFeat(const ConsoleArgs &tokens);
    void consoleAddOrRemoveSpell(const ConsoleArgs &tokens);
    void consoleCastSpellAtObject(const ConsoleArgs &tokens);
    void consoleOpenCloseDoor(const ConsoleArgs &tokens);
    void consoleListGames(const ConsoleArgs &tokens);
    void consoleLoadGame(const ConsoleArgs &tokens);
    void consoleSaveGame(const ConsoleArgs &tokens);
    void consoleStartPazaak(const ConsoleArgs &tokens);
    void consoleMiniGameInfo(const ConsoleArgs &tokens);
    void consoleStartSwoop(const ConsoleArgs &tokens);
    void consoleStopSwoop(const ConsoleArgs &tokens);
    void consoleSwoopState(const ConsoleArgs &tokens);
    void consoleStartSwoopRace(const ConsoleArgs &tokens);
    void consoleFinishSwoop(const ConsoleArgs &tokens);
    void consoleStartTurret(const ConsoleArgs &tokens);
    void consoleStopTurret(const ConsoleArgs &tokens);
    void consoleTurretState(const ConsoleArgs &tokens);
    void consoleStartTurretGame(const ConsoleArgs &tokens);
    void consoleShowImGui(const ConsoleArgs &tokens);
    void consoleShowPath(const ConsoleArgs &tokens);

    // END Console commands
};

} // namespace game

} // namespace reone
