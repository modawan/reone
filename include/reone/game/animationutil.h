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

#include <optional>

#include "types.h"

namespace reone {

namespace resource {

class TwoDA;

}

namespace game {

bool isAnimationLooping(AnimationType animation);

/**
 * The animation ID a creature's PlayAnimation constant stands for: IDs from
 * 10000 name dialog and state animations, and lower ones raw animations.2da
 * rows (in TSL constant 42 and the constants from 10001). Constants that
 * name none give the pause, 10000. The injection is its dialog animation
 * for a character or droid model and the pause for any other.
 */
int scriptAnimationId(int constant, bool tsl, bool characterModel);

/**
 * The animation ID a placeable's PlayAnimation constant stands for: the
 * placeable constants 200-213 name the placeable animations, and any other
 * constant the pause, 10000.
 */
int placeableScriptAnimationId(int constant);

/**
 * The animations.2da row an animation ID shows on a placeable: the
 * placeable animations from 10000 name rows of their own, and a lower ID
 * names its own row.
 *
 * @return the row, or -1 for an ID that names none
 */
int placeableAnimationRow(int id);

/**
 * The animations.2da row an animation ID shows on a door: the door's states
 * and reactions from 10000 name rows of their own, and a lower ID names its
 * own row.
 *
 * @return the row, or -1 for an ID that names none
 */
int doorAnimationRow(int id);

/**
 * The swing a placeable or door plays before the loop with animation ID
 * \p next takes over from the loop \p shown, while the animation \p current
 * plays: a door swings open from its closed loop and shut into it, and either
 * opens, closes, switches on or off, and dies; a later rule takes precedence
 * over an earlier one, and the animation loops 10106-10116 neither open,
 * close nor switch.
 *
 * @return the animations.2da row of the swing, if there is one
 */
std::optional<int> stateLoopSwingRow(int shown, int current, int next, bool door);

/**
 * The clip an animation ID shows on a placeable when its row names none: an
 * ID from 1000 to 1727 its cut clip, which the IDs between the bands lack,
 * and any other ID the default pose.
 */
std::string placeableFallbackClip(int id);

/**
 * The animations.2da row that an animation ID from 10000 a PlayAnimation
 * constant stands for shows on a character or a creature model. The ready
 * loop (10001) is not covered: it follows the creature's state.
 *
 * @return the row, or -1 when the model has none for the ID
 */
int scriptAnimationRow(int id, bool characterModel);

/**
 * The animation ID plays as an overlay on the animation beneath, at its
 * natural speed.
 */
bool playsAsOverlay(int id, bool tsl);

/** The animation ID plays from its end back to its start. */
bool playsBackwards(int id);

/**
 * The overlay of the animation ID blends out when it is switched off,
 * rather than stopping at once.
 */
bool overlayFadesOut(int id);

/**
 * The clip that leads from one loop to the next, as an animation ID (-1 for
 * none), and the one-shot that follows it before the new loop, if any.
 */
struct LoopTransition {
    int clip {-1};
    int follow {-1};
    /** The transition cuts the running one-shot short. */
    bool cutsOneShot {false};
};

/**
 * The transition shown when the loop with animation ID \p newId takes over
 * from the loop with \p oldId, while the clip with \p currentId shows.
 */
LoopTransition loopTransition(int newId, int oldId, int currentId, bool characterModel);

/** The animations.2da row a transition's animation ID names, or -1 when the model has none. */
int loopTransitionRow(int id, bool characterModel);

/** The row of animations.2da or dialoganimations.2da is a looping or fire-and-forget dialog animation. */
bool isDialogAnimationRow(const resource::TwoDA &table, int row);
/** The animation ID is a cut clip a conversation plays: one-shot 1000-1327 or looping 1400-1727. */
bool isDialogCutClip(int id);

/**
 * The cut clip an animation ID names: "cut", the clip's number within its
 * band of 128 IDs counted from 001, and the band's suffix: none from 1000,
 * "w" from 1200, "l" from 1400 and "wl" from 1600.
 *
 * @return the clip name, or an empty string for an ID outside the bands
 */
std::string cutClipName(int id);

/** The animation ID names a cut clip authored in world space: 1200-1327 or 1600-1727. */
bool isWorldSpaceCutClip(int id);

/** The animation ID names a cut clip that an object plays once: 1000-1127 or 1200-1327. */
bool isFireForgetCutClip(int id);

} // namespace game

} // namespace reone
