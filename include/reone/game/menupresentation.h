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
