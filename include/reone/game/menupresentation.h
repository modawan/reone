/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "reone/audio/options.h"
#include "reone/graphics/options.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"

namespace reone {

namespace game {

// Visual values only: never an object ID, Party binding, or live creature.
struct CreaturePresentation {
    int gender {0};
    int appearance {0};
    int bodyVariation {0}; // appearance.2da model/tex column: a = 0
    int textureVariation {1};
};

struct MenuPresentation {
    int selector {0};
    std::optional<CreaturePresentation> leader;

    static int validateSelector(int value) { return value >= 0 && value <= 4 ? value : 0; }
    std::string modelResRef(bool tsl) const;
    static MenuPresentation load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;
};

struct Keybind {
    input::KeyCode keyCode;
    input::KeyCode keyCodeAlt;

    bool check(input::KeyCode keyCode) {
        return this->keyCode == keyCode || keyCodeAlt == keyCode;
    }
};
struct KeybindOptions {
    Keybind moveForward {input::KeyCode::W};
    Keybind moveBackward {input::KeyCode::S};
    Keybind moveLeft {input::KeyCode::Z};
    Keybind moveRight {input::KeyCode::C};
    Keybind flourish {input::KeyCode::X};
    Keybind walk {input::KeyCode::B};
    Keybind cameraRotateLeft {input::KeyCode::A};
    Keybind cameraRotateRight {input::KeyCode::D};
    Keybind cycleTargetLeft {input::KeyCode::Q};
    Keybind cycleTargetRight {input::KeyCode::E};
    Keybind toggleStealth {input::KeyCode::G};
    Keybind clearOneAction {input::KeyCode::Y};
    Keybind cancelCombat {input::KeyCode::F};
    Keybind defaultAction {input::KeyCode::R};
    Keybind pause {input::KeyCode::Space, input::KeyCode::Pause};

    static KeybindOptions load(const std::filesystem::path &path, const int index);
    void save(const std::filesystem::path &path, const int index) const;
};

/**
 * What the game tells and shows the player, chosen in the feedback options and
 * kept in the configuration's Game Options section.
 */
struct FeedbackOptions {
    /** Equipment lists leave out items the character cannot use. */
    bool hideUnequippable {false};
    /** Tutorial windows open when their situations first arise. */
    bool tutorialPopups {true};
    bool subtitles {true};
    bool miniMap {true};
    /** Damage and other numbers float over the creatures they concern. */
    bool floatingNumbers {true};
    /** Changes to the party's status are summarized in a window. */
    bool statusSummary {true};
    /** The interface uses its small fonts (KotOR only). */
    bool smallFonts {false};
    bool hideInGameGui {false};
    bool tooltips {true};

    /** The options in the configuration; an option it does not set keeps its default. */
    static FeedbackOptions load(const std::filesystem::path &path);
    /** Write the options into the configuration, keeping every other line. */
    void save(const std::filesystem::path &path) const;
};

/** The difficulty level in the configuration's Game Options section, or level when the configuration does not set it. */
int loadDifficultyLevel(const std::filesystem::path &path, int level);
/** Write the difficulty level into the configuration, keeping every other line. */
void saveDifficultyLevel(const std::filesystem::path &path, int level);

int loadActiveKeybinds(const std::filesystem::path &path, int activeKeybinds);
void saveActiveKeybinds(const std::filesystem::path &path, int activeKeybinds);

/** Each bit of the unlocked planet songs opens one group of the main menu's music list; there are eleven. */
constexpr int kAllPlanetSongs = 0x7ff;
/** Write the unlocked planet songs into the configuration, keeping every other line. */
void saveUnlockedPlanetSongs(const std::filesystem::path &path, int songs);

/** Write the sound options' volumes and Force Software into the configuration, keeping every other line. */
void saveSoundOptions(const std::filesystem::path &path, const audio::AudioOptions &options);
/**
 * Write the graphics options the running game applies at once (Shadows, Soft
 * Shadows, Grass, anti-aliasing and V-Sync) into the configuration, keeping
 * every other line.
 */
void saveGraphicsOptions(const std::filesystem::path &path, const graphics::GraphicsOptions &options);

/**
 * The graphics options the game takes only as it starts: the display mode, the
 * texture pack, the textures' anisotropic filtering and the interface size.
 * The options pages keep them in the configuration for the next start, while
 * the running game goes on with the ones it started with.
 */
struct StartGraphicsOptions {
    graphics::DisplayMode displayMode;
    graphics::TextureQuality textureQuality {graphics::TextureQuality::High};
    /** The filtering's samples as a power of two: 0 is none, 4 is 16. */
    int anisotropicFiltering {0};
    /** The interface's share of the largest size that fits the screen. */
    float guiScale {1.0f};

    /** The options the next start takes; a key the configuration lacks starts as \p running did. */
    static StartGraphicsOptions load(const std::filesystem::path &path, const graphics::GraphicsOptions &running);
    /** Write the options into the configuration, keeping every other line. */
    void save(const std::filesystem::path &path) const;
};

/** How the mouse turns the camera, kept in the configuration's Game Options section. */
struct MouseOptions {
    /** The mouse turns the camera unless the right button or a Ctrl key is held. */
    bool mouseLook {false};
    /** How far a frame of mouse travel turns the camera, from 0 to 255. */
    int sensitivity {44};

    /** The options in the configuration; an option it does not set keeps its default. */
    static MouseOptions load(const std::filesystem::path &path);
    /** Write the options into the configuration, keeping every other line. */
    void save(const std::filesystem::path &path) const;
};

/** Gameplay switches, stored in the configuration's Game Options section. */
struct GameplayOptions {
    /**
     * The leader in combat mode may be steered with the movement keys and the
     * mouse while it is busy. Off, a busy leader in combat mode is not
     * steered.
     */
    bool combatMovement {true};

    /** The options in the configuration; an option it does not set keeps its default. */
    static GameplayOptions load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;
};

/** Situations that pause play on their own, stored in the configuration's Autopause Options section. */
struct AutoPauseOptions {
    /** A combat round ends. */
    bool endOfCombatRound {false};
    /** An enemy comes into view. */
    bool enemySighted {true};
    /** A mine comes into view; on by default in KotOR only. */
    bool mineSighted {false};
    /** A party member falls. */
    bool partyKilled {true};
    /** The action menu opens. */
    bool actionMenu {false};
    /** A new target is selected. */
    bool newTargetSelected {true};

    /** The options a new configuration starts with. */
    static AutoPauseOptions defaults(bool tsl);
    /** The options in the configuration; an option it does not set keeps its default. */
    static AutoPauseOptions load(const std::filesystem::path &path, bool tsl);
    /** Write the options into the configuration, keeping every other line. */
    void save(const std::filesystem::path &path) const;
};

} // namespace game

} // namespace reone
