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

#include "types.h"

namespace reone {

namespace resource {

class ITwoDAs;
class TwoDA;

} // namespace resource

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
 * The animations.2da row that an animation ID from 10000 a PlayAnimation
 * constant stands for shows on a character or a creature model. The pause
 * (10000) and the ready loop (10001) are not covered: they follow the
 * creature's state.
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

/**
 * A looping or fire-and-forget dialog animation: dialoganimations.2da rows
 * from ID 10000, animations.2da rows below its row count, otherwise the cut
 * clip IDs (one-shot 1000-1327, looping 1400-1727).
 */
bool isDialogAnimation(resource::ITwoDAs &twoDas, int id);
/** The row of animations.2da or dialoganimations.2da is a looping or fire-and-forget dialog animation. */
bool isDialogAnimationRow(const resource::TwoDA &table, int row);

} // namespace game

} // namespace reone
