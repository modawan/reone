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

#include "reone/game/animationutil.h"

#include <algorithm>
#include <array>

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kPauseAnimationId = 10000;
// Animation IDs from 10000 name dialoganimations.2da rows.
static constexpr int kDialogAnimationBase = 10000;
// The looping PlayAnimation constants, 0-46; KotOR names only 0-32.
static constexpr int kLastLoopingConstant = 46;
static constexpr int kK1LastLoopingConstant = 32;
// The fire-and-forget PlayAnimation constants, 100-124; KotOR names only 100-120.
static constexpr int kFirstFireForgetConstant = 100;
static constexpr int kLastFireForgetConstant = 124;
static constexpr int kK1LastFireForgetConstant = 120;
// KotOR's constant 116 is its choke.
static constexpr int kK1ChokeConstant = 116;
static constexpr int kK1ChokeAnimationId = 10150;
// The cut clip IDs: one-shot 1000-1327, looping 1400-1727.
static constexpr int kFirstCutClip = 1000;
static constexpr int kLastCutClip = 1327;
static constexpr int kFirstLoopingCutClip = 1400;
static constexpr int kLastLoopingCutClip = 1727;

bool isAnimationLooping(AnimationType animation) {
    int ordinal = static_cast<int>(animation);

    return animation == AnimationType::LoopingChoke ||
           (ordinal >= static_cast<int>(AnimationType::LoopingPause) && ordinal <= static_cast<int>(AnimationType::LoopingMeditateStand));
}

int scriptAnimationId(int constant, bool tsl, bool characterModel) {
    static constexpr int kInjectConstant = 112;
    static constexpr int kInjectAnimationId = 10070;
    // Constant 42 stands for its own row.
    static const std::array<int, kLastLoopingConstant + 1> kLooping {
        10000, 10052, 10030, 10032, 10033, 10038, 10039, 10040, 10041, 10042,
        10059, 10060, 10057, 10058, 10120, 10121, 10122, 10123, 10124, 10001,
        10118, 10023, 10137, 10139, 10151, 10152, 10006, 10154, 10155, 10160,
        10156, 10163, 10164, 10165, 10128, 10418, 10424, 10425, 10426, 10427,
        10428, 10414, 42, 10022, 10133, 10150, 10419};
    static const std::array<int, kLastFireForgetConstant - kFirstFireForgetConstant + 1> kFireForget {
        10053, 10054, 10055, 10056, 10034, 10035, 10029, 10028, 10044, 10044,
        10044, 10000, 10000, 10125, 10126, 10127, 10000, 10129, 10130, 10142,
        10159, 10017, 10021, 10000, 10414};
    if (tsl && constant > kDialogAnimationBase) {
        return constant - kDialogAnimationBase;
    }
    if (constant == kInjectConstant) {
        return characterModel ? kInjectAnimationId : kPauseAnimationId;
    }
    if (constant >= 0 && constant <= kLastLoopingConstant) {
        return tsl || constant <= kK1LastLoopingConstant ? kLooping[constant] : kPauseAnimationId;
    }
    if (constant >= kFirstFireForgetConstant && constant <= kLastFireForgetConstant) {
        if (tsl) {
            return kFireForget[constant - kFirstFireForgetConstant];
        }
        if (constant == kK1ChokeConstant) {
            return kK1ChokeAnimationId;
        }
        return constant <= kK1LastFireForgetConstant ? kFireForget[constant - kFirstFireForgetConstant] : kPauseAnimationId;
    }
    return kPauseAnimationId;
}

int scriptAnimationRow(int id, bool characterModel) {
    // An animation ID, and the rows it names on a character and on a creature
    // model; -1 where the model has none.
    struct ScriptAnimationRows {
        int id;
        int characterRow;
        int creatureRow;
    };
    static constexpr std::array<ScriptAnimationRows, 63> kRows {{
        {10006, 81, 275},  // dead / cdead
        {10017, 63, -1},   // force cast
        {10021, 310, 310}, // open
        {10022, 333, 333}, // closed
        {10023, 77, 268},  // spasm
        {10028, 33, 263},  // taunt
        {10029, 31, -1},   // greeting
        {10030, 18, 18},   // listen
        {10032, 24, -1},   // meditate
        {10033, 24, -1},   // worship
        {10034, 16, -1},   // salute
        {10035, 19, -1},   // bow
        {10038, 25, 261},  // the talk loops
        {10039, 27, 261},
        {10040, 26, 261},
        {10041, 29, 261},
        {10042, 28, 261},
        {10044, 17, 260}, // victory
        {10052, 7, 257},  // second pause
        {10053, 11, 258}, // head turns
        {10054, 10, 259},
        {10055, 12, 256},  // scratching the head
        {10056, 13, 256},  // bored
        {10057, 14, 256},  // tired
        {10058, 15, 256},  // drunk
        {10059, 40, -1},   // getting from low
        {10060, 41, -1},   // getting from waist height
        {10070, 37, -1},   // inject
        {10118, 270, 270}, // deactivated
        {10120, 32, -1},   // flirt
        {10121, 44, -1},   // using a computer
        {10122, 53, -1},   // the dances
        {10123, 54, -1},
        {10124, 74, 74}, // horror
        {10125, 43, 43}, // using a computer once
        {10126, 68, -1}, // persuade
        {10127, 38, -1}, // activate
        {10128, 47, -1}, // unlocking a door
        {10129, 57, -1}, // the throws
        {10130, 58, -1},
        {10133, 5, 253},   // stealth
        {10137, 76, 267},  // sleep
        {10139, 79, -1},   // prone
        {10142, 346, 346}, // custom
        {10150, 72, 264},  // choke
        {10151, 359, -1},  // third pause
        {10152, 360, 369}, // weld
        {10154, 370, 372}, // injured talk
        {10155, 371, 18},  // injured listen
        {10156, 375, 275}, // lying dead
        {10159, 34, -1},   // treating injuries
        {10160, 35, -1},
        {10163, 384, -1}, // talking on the knees
        {10164, 385, -1},
        {10165, 51, -1},  // checking a body
        {10414, 461, -1}, // rage
        {10418, 457, -1}, // sitting and meditating
        {10419, 458, -1}, // meditating standing
        {10424, 316, -1}, // the chair loops
        {10425, 317, -1},
        {10426, 318, -1},
        {10427, 319, -1},
        {10428, 320, -1},
    }};
    const auto found = std::find_if(kRows.begin(), kRows.end(), [id](const ScriptAnimationRows &rows) { return rows.id == id; });
    if (found == kRows.end()) {
        return -1;
    }
    return characterModel ? found->characterRow : found->creatureRow;
}

bool playsAsOverlay(int id, bool tsl) {
    switch (id) {
    case 10029: // greeting
    case 10030: // listen
    case 10034: // salute
    case 10038: // the talk loops
    case 10039:
    case 10040:
    case 10041:
    case 10042:
    case 10070:
    case 10071:
    case 10129: // throw high
    case 10130: // throw low
    case 10136:
    case 10154: // injured talk
    case 10155: // injured listen
    case 10302: // damage flinch
        return true;
    case 10417:
        return tsl;
    default:
        return false;
    }
}

bool playsBackwards(int id) {
    switch (id) {
    case 10003:
    case 10081:
    case 10083:
    case 10212:
    case 10238: // from kneeling back to standing
    case 10256:
    case 10258:
    case 10271: // from the knees back to standing
        return true;
    default:
        return false;
    }
}

bool overlayFadesOut(int id) {
    switch (id) {
    case 10029:
    case 10030:
    case 10034:
    case 10038:
    case 10039:
    case 10040:
    case 10041:
    case 10042:
    case 10070:
    case 10071:
    case 10136:
    case 10145:
    case 10154:
    case 10155:
        return true;
    default:
        return false;
    }
}

LoopTransition loopTransition(int newId, int oldId, int currentId, bool characterModel) {
    static constexpr int kClosedLoop = 10022;
    static constexpr int kOpenLoop = 10050;
    static constexpr int kDeadLoop = 10006;
    static constexpr int kDeadProneLoop = 10156;
    static constexpr int kProneLoop = 10139;
    static constexpr int kWhirlwindLoop = 10117;
    static constexpr int kForcePushedLoop = 10400;
    LoopTransition transition;
    int &clip = transition.clip;
    // Meditating and worshipping are knelt into and risen from.
    if ((newId & ~1) == 10032) {
        clip = 10237;
    }
    if ((oldId & ~1) == 10032) {
        clip = 10238;
    }
    if (newId == kClosedLoop) {
        clip = currentId != kOpenLoop ? 337 : 336;
    }
    if (oldId == kClosedLoop) {
        clip = newId != kOpenLoop ? 335 : 334;
    }
    // Lying on the back is fallen into and got up from.
    if (newId == kProneLoop) {
        clip = 85;
    }
    if (oldId == kProneLoop) {
        clip = 86;
    }
    if (newId == 10418) {
        clip = 456;
    }
    if (newId >= 10424 && newId <= 10428) {
        clip = 20;
    }
    if (newId == 10501) {
        clip = 559;
    }
    // The dead loops are died into and got up from.
    if (oldId == kDeadLoop) {
        clip = 10223;
    }
    if (oldId == 10008) {
        clip = 10224;
    }
    if (oldId == 10007) {
        clip = newId != 10008 ? 10224 : 10222;
    }
    if (oldId == 10005) {
        clip = newId != kDeadLoop ? 10223 : 10221;
    }
    if (newId == kDeadLoop) {
        clip = 10221;
    }
    if (newId == 10005) {
        clip = 10219;
    }
    if (newId == 10007) {
        clip = 10220;
    }
    if (newId == 10008) {
        clip = 10222;
    }
    if (oldId == 10402) {
        clip = -1;
    }
    // Leaving the whirlwind or a push falls and gets up, cutting the running
    // one-shot short.
    if (oldId == kWhirlwindLoop || oldId == kForcePushedLoop) {
        clip = characterModel ? 85 : 272;
        transition.follow = characterModel ? 86 : 273;
        transition.cutsOneShot = true;
    }
    if (newId == kDeadProneLoop) {
        if (oldId == kProneLoop) {
            clip = characterModel ? 374 : -1;
            transition.follow = -1;
        } else {
            clip = characterModel ? 85 : 272;
            transition.follow = characterModel ? 374 : -1;
        }
    } else if (oldId == kDeadProneLoop) {
        clip = characterModel ? 86 : 273;
        transition.follow = -1;
    }
    // Talking on the knees is knelt into and risen from.
    const bool newKneeling = newId == 10163 || newId == 10164;
    const bool oldKneeling = oldId == 10163 || oldId == 10164;
    if (newKneeling) {
        clip = 383;
    }
    if (oldKneeling) {
        clip = newId == kDeadLoop ? 80 : (newKneeling ? 383 : 10271);
    }
    return transition;
}

int loopTransitionRow(int id, bool characterModel) {
    switch (id) {
    case 10237:
    case 10238:
        return characterModel ? 23 : -1;
    case 10271:
        return characterModel ? 383 : -1;
    case 10219:
    case 10220:
        return characterModel ? 85 : 272;
    case 10221:
        return characterModel ? 80 : 274;
    case 10222:
        return characterModel ? 82 : 274;
    case 10223:
        return characterModel ? 381 : 273;
    case 10224:
        return characterModel ? 382 : 273;
    default:
        return id >= 0 && id < 1000 ? id : -1;
    }
}

bool isDialogAnimationRow(const TwoDA &table, int row) {
    return table.getBool(row, "dialog") && (table.getBool(row, "looping") || table.getBool(row, "fireforget"));
}

bool isDialogAnimation(ITwoDAs &twoDas, int id) {
    auto dialogAnimations = getRequiredTwoDA(twoDas, "dialoganimations");
    if (id >= kDialogAnimationBase && id < kDialogAnimationBase + dialogAnimations->getRowCount()) {
        return isDialogAnimationRow(*dialogAnimations, id - kDialogAnimationBase);
    }
    auto animations = getRequiredTwoDA(twoDas, "animations");
    if (id >= 0 && id < std::min(kDialogAnimationBase, animations->getRowCount())) {
        return isDialogAnimationRow(*animations, id);
    }
    return (id >= kFirstCutClip && id <= kLastCutClip) || (id >= kFirstLoopingCutClip && id <= kLastLoopingCutClip);
}

} // namespace game

} // namespace reone
