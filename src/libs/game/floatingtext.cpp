/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/floatingtext.h"

#include <algorithm>

#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/graphics/camera.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/strings.h"
#include "reone/scene/node/camera.h"

using namespace reone::graphics;

namespace reone {

namespace game {

static constexpr float kFloatingTextOffsetY = 32.0f;
static constexpr float kFloatingTextDuration = 1.5f;
static constexpr int kMaxFloatingTextEntries = 5;
static constexpr int kMissStrRef = 1373;

static const glm::vec3 kDamageColor(0.74f, 0.11f, 0.0f);
static const glm::vec3 kHealColor(0.28f, 0.92f, 0.11f);
static const glm::vec3 kMissColor(1.0f);

void FloatingText::addDamage(
    const Object &object, int amount, int adjustedAmount, uint32_t damager) {

    if (_game.isTSL()) {
        return;
    }

    auto leader = _game.party().getLeader();
    if (!leader) {
        return;
    }

    if (damager == leader->id()) {
        add(object, std::to_string(amount), Style::Damage, kFloatingTextDuration);
    } else if (object.id() == leader->id()) {
        add(object, std::to_string(adjustedAmount), Style::Damage, kFloatingTextDuration);
    }
}

void FloatingText::addHeal(const Object &object, int amount) {
    if (_game.isTSL() || !_game.party().isMember(object)) {
        return;
    }

    add(object, std::to_string(amount), Style::Heal, kFloatingTextDuration);
}

void FloatingText::addMiss(const Creature &attacker, const Object &target) {
    if (_game.isTSL()) {
        return;
    }

    auto leader = _game.party().getLeader();
    if (!leader || attacker.id() != leader->id()) {
        return;
    }

    add(
        target,
        _services.resource.strings.getText(kMissStrRef),
        Style::Miss,
        kFloatingTextDuration);
}

void FloatingText::add(const Object &object, std::string text, Style style, float duration) {
    for (Entry &entry : _entries) {
        if (entry.objectId == object.id()) {
            ++entry.stack;
        }
    }
    _entries.erase(
        std::remove_if(_entries.begin(), _entries.end(), [&object](const Entry &entry) {
            return entry.objectId == object.id() && entry.stack > kMaxFloatingTextEntries;
        }),
        _entries.end());

    _entries.push_back({object.id(), std::move(text), style, duration, duration, 1});
}

void FloatingText::update(float dt) {
    for (Entry &entry : _entries) {
        entry.remaining -= dt;
    }
    _entries.erase(
        std::remove_if(_entries.begin(), _entries.end(), [](const Entry &entry) {
            return entry.remaining <= 0.0f;
        }),
        _entries.end());
}

void FloatingText::render() {
    if (_game.isTSL() || _entries.empty()) {
        return;
    }
    const auto &options = _game.options().graphics;
    const std::string fontResRef(
        options.width > 1279 ? "fnt_d16x16b" : "fnt_d16x16a");
    if (!_font || _fontResRef != fontResRef) {
        _font = _services.resource.fonts.getExact(fontResRef);
        _fontResRef = fontResRef;
    }
    if (!_font) {
        return;
    }

    auto module = _game.module();
    auto camera = _game.getActiveCamera();
    if (!module || !camera) {
        return;
    }

    auto area = module->area();
    auto cameraNode = camera->cameraSceneNode();
    auto graphicsCamera = cameraNode ? cameraNode->camera() : nullptr;
    if (!area || !graphicsCamera) {
        return;
    }

    const glm::mat4 &projection = graphicsCamera->projection();
    const glm::mat4 &view = graphicsCamera->view();
    const float lineHeight = _font->height();

    _services.graphics.uniforms.setGlobals([&options](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(options.width),
            static_cast<float>(options.height),
            0.0f,
            0.0f,
            100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });

    _services.graphics.context.withViewport(
        {0, 0, options.width, options.height},
        [this, area, &projection, &view, &options, lineHeight]() {
            _services.graphics.context.withDepthTestMode(
                DepthTestMode::None,
                [this, area, &projection, &view, &options, lineHeight]() {
                    _services.graphics.context.withDepthMask(
                        false,
                        [this, area, &projection, &view, &options, lineHeight]() {
                            _services.graphics.context.withBlendMode(
                                BlendMode::Normal,
                                [this, area, &projection, &view, &options, lineHeight]() {
                                    for (auto it = _entries.rbegin(); it != _entries.rend(); ++it) {
                                        const Entry &entry = *it;
                                        auto object = _game.getObjectById(entry.objectId);
                                        if (!object) {
                                            continue;
                                        }

                                        glm::vec3 screen = area->getSelectableScreenCoords(
                                            object,
                                            projection,
                                            view);
                                        if (screen.z >= 1.0f) {
                                            continue;
                                        }

                                        glm::vec3 color;
                                        switch (entry.style) {
                                        case Style::Damage:
                                            color = kDamageColor;
                                            break;
                                        case Style::Heal:
                                            color = kHealColor;
                                            break;
                                        case Style::Miss:
                                            color = kMissColor;
                                            break;
                                        }

                                        float x = options.width * screen.x;
                                        float y = options.height * (1.0f - screen.y) -
                                                  kFloatingTextOffsetY -
                                                  (entry.stack - 0.5f) * lineHeight;
                                        float alpha = glm::clamp(
                                            entry.remaining / entry.duration,
                                            0.0f,
                                            1.0f);

                                        _font->render(
                                            entry.text,
                                            glm::vec3(x, y, 0.0f),
                                            glm::vec4(color, alpha),
                                            TextGravity::CenterCenter);
                                    }
                                });
                        });
                });
        });
}

void FloatingText::reset() {
    _entries.clear();
    _font.reset();
    _fontResRef.clear();
}

} // namespace game

} // namespace reone
