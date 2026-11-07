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
    input::GamepadButton gamepadButton;
    input::GamepadAxis gamepadAxis;

    bool check(input::KeyCode keyCode) {
        return this->keyCode == keyCode || keyCodeAlt == keyCode;
    }
    bool check(input::GamepadButton button) {
        return gamepadButton == button;
    }
    bool check(input::GamepadAxis axis) {
        return gamepadAxis == axis;
    }
};
struct KeybindOptions {
    Keybind moveForward             {input::KeyCode::W,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftY};
    Keybind moveBackward            {input::KeyCode::S,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftY};
    Keybind moveLeft                {input::KeyCode::Z,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftX};
    Keybind moveRight               {input::KeyCode::C,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftX};
    Keybind flourish                {input::KeyCode::X,         input::KeyCode::Unknown,    input::GamepadButton::North,        input::GamepadAxis::Invalid};
    Keybind walk                    {input::KeyCode::B,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind cameraRotateLeft        {input::KeyCode::A,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::RightX};
    Keybind cameraRotateRight       {input::KeyCode::D,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::RightX};
    Keybind cycleTargetLeft         {input::KeyCode::Q,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftTrigger};
    Keybind cycleTargetRight        {input::KeyCode::E,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::RightTrigger};
    Keybind toggleStealth           {input::KeyCode::G,         input::KeyCode::Unknown,    input::GamepadButton::Back,         input::GamepadAxis::Invalid};
    Keybind clearOneAction          {input::KeyCode::Y,         input::KeyCode::Unknown,    input::GamepadButton::North,        input::GamepadAxis::Invalid};
    Keybind cancelCombat            {input::KeyCode::F,         input::KeyCode::Unknown,    input::GamepadButton::East,         input::GamepadAxis::Invalid};
    Keybind defaultAction           {input::KeyCode::R,         input::KeyCode::Unknown,    input::GamepadButton::South,        input::GamepadAxis::Invalid};
    Keybind pause                   {input::KeyCode::Space,     input::KeyCode::Pause,      input::GamepadButton::LeftShoulder, input::GamepadAxis::Invalid};
    Keybind changeCharacter         {input::KeyCode::Tab,       input::KeyCode::Unknown,    input::GamepadButton::RightShoulder,input::GamepadAxis::Invalid};
    Keybind freeLook                {input::KeyCode::CapsLock,  input::KeyCode::Unknown,    input::GamepadButton::LeftStick,    input::GamepadAxis::Invalid};
    Keybind quickSave               {input::KeyCode::F4,        input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind quickLoad               {input::KeyCode::F5,        input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiNavigateScrollUp     {input::KeyCode::Unknown,   input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::RightY};
    Keybind guiNavigateScrollDown   {input::KeyCode::Unknown,   input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::RightY};
    Keybind guiNavigateUp           {input::KeyCode::Up,        input::KeyCode::Unknown,    input::GamepadButton::DPadUp,       input::GamepadAxis::LeftY};
    Keybind guiNavigateDown         {input::KeyCode::Down,      input::KeyCode::Unknown,    input::GamepadButton::DPadDown,     input::GamepadAxis::LeftY};
    Keybind guiNavigateLeft         {input::KeyCode::Left,      input::KeyCode::Unknown,    input::GamepadButton::DPadLeft,     input::GamepadAxis::LeftX};
    Keybind guiNavigateRight        {input::KeyCode::Right,     input::KeyCode::Unknown,    input::GamepadButton::DPadRight,    input::GamepadAxis::LeftX};
    Keybind guiNavigateExit         {input::KeyCode::Escape,    input::KeyCode::Unknown,    input::GamepadButton::East,         input::GamepadAxis::Invalid};
    Keybind guiNavigateConfirm      {input::KeyCode::Return,    input::KeyCode::KeypadEnter,input::GamepadButton::South,        input::GamepadAxis::Invalid};
    Keybind guiNavigateAlt1         {input::KeyCode::Unknown,   input::KeyCode::Unknown,    input::GamepadButton::West,         input::GamepadAxis::Invalid};
    Keybind guiNavigateAlt2         {input::KeyCode::Unknown,   input::KeyCode::Unknown,    input::GamepadButton::North,        input::GamepadAxis::Invalid};
    Keybind guiMenuMessages         {input::KeyCode::J,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuMap              {input::KeyCode::M,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuQuest            {input::KeyCode::L,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuSkillsFeats      {input::KeyCode::K,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuOptions          {input::KeyCode::O,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuCharacter        {input::KeyCode::P,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuInventory        {input::KeyCode::I,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiMenuEquipment        {input::KeyCode::U,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiNextMenu             {input::KeyCode::E,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind guiPreviousMenu         {input::KeyCode::Q,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::Invalid};
    Keybind mgMoveFoward            {input::KeyCode::W,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftY};
    Keybind mgMoveBackward          {input::KeyCode::S,         input::KeyCode::Unknown,    input::GamepadButton::Invalid,      input::GamepadAxis::LeftY};
    Keybind mgMoveLeft              {input::KeyCode::A,         input::KeyCode::Left,       input::GamepadButton::Invalid,      input::GamepadAxis::LeftX};
    Keybind mgMoveRight             {input::KeyCode::D,         input::KeyCode::Right,      input::GamepadButton::Invalid,      input::GamepadAxis::LeftX};
    Keybind mgAction                {input::KeyCode::Space,     input::KeyCode::Unknown,    input::GamepadButton::South,        input::GamepadAxis::Invalid};
    Keybind mgPause                 {input::KeyCode::Escape,    input::KeyCode::Unknown,    input::GamepadButton::LeftShoulder, input::GamepadAxis::Invalid};

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
