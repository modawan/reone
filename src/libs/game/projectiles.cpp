/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/projectiles.h"
#include "reone/game/object/creature.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/arrayref.h"

#include <algorithm>

using namespace reone::resource;

namespace reone {

namespace game {

/**
 * Droid animation indices into animations.2da for each attack type. These
 * indices are also column names of droiddischarge.2da.
 */
using DroidAnimIDs = SmallVector<std::pair<ProjectileAttackType, int>, 4>;

struct DroidSpec {
    int appearanceType;
    std::vector<ProjectileAttackType> validAttacks;
};

static int filterRowByDroid(TwoDA &twoDa, int start, int end, bool isDroid) {
    std::string droid("droid");
    while (start != end) {
        // Some rows are completely empty, skip them.
        bool isValid = twoDa.getInt(start, "shots") > 0;

        if (isValid && twoDa.getBool(start, "droid", true) == isDroid) {
            return start;
        }
        ++start;
    }
    return end;
}

static void findDroidAnimations(resource::TwoDA &animDA, DroidAnimIDs &droidAnims) {
    SmallVector<std::pair<ProjectileAttackType, std::string>, 4> anims;
    anims.push_back({ProjectileAttackType::Basic, "b0a1"});
    anims.push_back({ProjectileAttackType::Rapid, "b0a2"});
    anims.push_back({ProjectileAttackType::Sniper, "b0a3"});
    anims.push_back({ProjectileAttackType::Power, "b0a4"});

    for (int i = 0; i < animDA.getRowCount(); ++i) {
        for (const auto &kv : anims) {
            ProjectileAttackType kind = kv.first;
            const std::string &animName = kv.second;
            if (animName == animDA.getString(i, "name")) {
                droidAnims.push_back({kind, i});
            }
        }
    }
}

static bool parseDroidDischargeRow(resource::TwoDA &droidDa, int row,
                                   const DroidAnimIDs &droidAnims,
                                   DroidSpec &droidSpec) {
    int commonAppearanceType = -1;

    for (const auto &animId : droidAnims) {
        int animAppearanceType = droidDa.getInt(row, std::to_string(animId.second), -1);
        if (animAppearanceType < 0) {
            continue;
        }
        if (commonAppearanceType == -1) {
            commonAppearanceType = animAppearanceType;
        }
        if (commonAppearanceType != animAppearanceType) {
            // All columns must have the same appearance type, otherwise the row
            // is malformed.
            return false;
        }

        droidSpec.validAttacks.push_back(animId.first);
    }

    if (commonAppearanceType == -1) {
        return false;
    }

    droidSpec.appearanceType = commonAppearanceType;
    return true;
}

static bool parseWeaponDischargeRow(resource::TwoDA &twoDa, int row,
                                    ProjectileSpec &spec) {
    int shots = twoDa.getInt(row, "shots");
    int hits = twoDa.getInt(row, "hits");
    if (shots <= 0 || hits > shots) {
        return false;
    }

    spec.misses = shots - hits;

    std::string switchmask = twoDa.getString(row, "switchmask");
    if (!switchmask.empty() && switchmask.length() != shots) {
        return false;
    }

    for (int i = 0; i < shots; ++i) {
        std::string columnShot = str(boost::format("shot%d") % (i + 1));
        int time_ms = twoDa.getInt(row, columnShot);
        int kind = switchmask.empty() ? 0 : (switchmask[i] - '0');
        spec.projectiles.emplace_back(time_ms / 1000.0f, kind);
    }

    return true;
}

void Projectiles::parseHumanoidWeaponDischarge(resource::TwoDA &weaponDa) {
    CreatureWieldType wields[] = {
        CreatureWieldType::BlasterPistol,
        CreatureWieldType::DualPistols,
        CreatureWieldType::BlasterRifle,
        CreatureWieldType::HeavyWeapon,
    };

    ProjectileAttackType attacks[] = {
        ProjectileAttackType::Basic,
        ProjectileAttackType::Rapid,
        ProjectileAttackType::Sniper,
        ProjectileAttackType::Power,
    };

    int row = 0;
    int rowEnd = weaponDa.getRowCount();

    for (CreatureWieldType wield : ArrayRef(wields)) {
        for (ProjectileAttackType attack : ArrayRef(attacks)) {
            row = filterRowByDroid(weaponDa, row, rowEnd, /*isDroid*/ false);
            if (row == rowEnd) {
                return;
            }

            ProjectileSpec spec;
            if (parseWeaponDischargeRow(weaponDa, row, spec)) {
                _humanoids[{wield, attack}] = spec;
            }
            ++row;
        }
    }
}

void Projectiles::parseDroidWeaponDischarge(resource::TwoDA &weaponDa,
                                            resource::TwoDA &droidDa,
                                            resource::TwoDA &animDa) {
    DroidAnimIDs droidAnims;
    findDroidAnimations(animDa, droidAnims);

    std::vector<DroidSpec> droidSpecs;
    for (int i = 0; i < droidDa.getRowCount(); ++i) {
        DroidSpec spec;
        if (parseDroidDischargeRow(droidDa, i, droidAnims, spec)) {
            droidSpecs.push_back(spec);
        }
    }

    ProjectileAttackType attacks[] = {
        ProjectileAttackType::Basic,
        ProjectileAttackType::Rapid,
        ProjectileAttackType::Sniper,
        ProjectileAttackType::Power,
    };

    int maxDroidAnims = droidDa.getColumnCount();
    if (maxDroidAnims < 1) {
        return;
    }

    int row = 0;
    int rowEnd = weaponDa.getRowCount();
    int skipRows = 2;
    for (DroidSpec &droid : droidSpecs) {
        int i = 0;
        for (ProjectileAttackType attack : ArrayRef(attacks)) {
            row = filterRowByDroid(weaponDa, row, rowEnd, /*isDroid*/ true);
            if (skipRows) {
                // Droid rows a weird: there are empty lines that we already
                // ignore in filterRowByDroid, but the rest seems to be shifted
                // by 2 rows for some reason.
                row = std::min(row + skipRows, rowEnd);
                skipRows = 0;
            }
            if (row == rowEnd) {
                return;
            }
            ProjectileSpec spec;
            if (parseWeaponDischargeRow(weaponDa, row, spec)) {
                _droids[{droid.appearanceType, attack}] = spec;
            }
            ++row;
            ++i;
            if (i == maxDroidAnims) {
                break;
            }
        }
    }
}

void Projectiles::init() {
    std::shared_ptr<TwoDA> animDa(_twoDas.get("animations"));
    std::shared_ptr<TwoDA> droidDa(_twoDas.get("droiddischarge"));
    std::shared_ptr<TwoDA> weaponDa(_twoDas.get("weapondischarge"));

    if (!weaponDa) {
        return;
    }

    parseHumanoidWeaponDischarge(*weaponDa);

    if (!animDa || !droidDa) {
        return;
    }

    parseDroidWeaponDischarge(*weaponDa, *droidDa, *animDa);
}

void Projectiles::clear() {
    _humanoids.clear();
    _droids.clear();
}

ProjectileSpec *Projectiles::get(ProjectileAttackType attack, CreatureWieldType wield, int appearance) {
    auto droidIt = _droids.find({appearance, attack});
    if (droidIt != _droids.end()) {
        return &droidIt->second;
    }

    switch (wield) {
    case CreatureWieldType::DualSwords:
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon: {
        auto it = _humanoids.find({wield, attack});
        return (it != _humanoids.end()) ? &it->second : nullptr;
    }
    default:
        return nullptr;
    }
}

} // namespace game

} // namespace reone
