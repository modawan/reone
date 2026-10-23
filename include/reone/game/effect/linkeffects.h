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

#include "../effect.h"

namespace reone {

namespace game {

class LinkEffectsEffect : public CopyableEffect<LinkEffectsEffect> {
public:
    LinkEffectsEffect(std::shared_ptr<Effect> childEffect, std::shared_ptr<Effect> parentEffect) :
        CopyableEffect(EffectType::LinkEffects),
        _childEffect(std::move(childEffect)),
        _parentEffect(std::move(parentEffect)) {
    }

    LinkEffectsEffect(const LinkEffectsEffect &other) :
        CopyableEffect(other),
        _childEffect(other._childEffect ? other._childEffect->cloneEffect() : nullptr),
        _parentEffect(other._parentEffect ? other._parentEffect->cloneEffect() : nullptr) {
    }

    void setCategory(EffectCategory category) override;
    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override;
    const std::shared_ptr<Effect> &childEffect() const { return _childEffect; }
    const std::shared_ptr<Effect> &parentEffect() const { return _parentEffect; }

private:
    std::shared_ptr<Effect> _childEffect;
    std::shared_ptr<Effect> _parentEffect;
};

} // namespace game

} // namespace reone
