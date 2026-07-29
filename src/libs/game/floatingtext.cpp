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

static std::shared_ptr<Object> findAreaObject(const Area &area, uint32_t objectId) {
    auto it = std::find_if(area.objects().begin(), area.objects().end(), [objectId](const auto &object) {
        return object && object->id() == objectId;
    });
    return it != area.objects().end() ? *it : nullptr;
}

void FloatingText::addDamage(
    const Object &object, int amount, int adjustedAmount, uint32_t damager) {

    auto leader = _game.party().getLeader();
    if (!leader) {
        return;
    }

    if (damager == leader->id()) {
        add(object, std::to_string(std::max(0, amount)), Style::Damage, kFloatingTextDuration);
    } else if (object.id() == leader->id()) {
        add(object, std::to_string(std::max(0, adjustedAmount)), Style::Damage, kFloatingTextDuration);
    }
}

void FloatingText::addHeal(const Object &object, int amount) {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    auto selected = area ? area->selectedObject() : nullptr;
    if (!_game.party().isMember(object) && (!selected || selected->id() != object.id())) {
        return;
    }

    add(object, std::to_string(std::max(0, amount)), Style::Heal, kFloatingTextDuration);
}

void FloatingText::addMiss(const Creature &attacker, const Object &target) {
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
    if (text.empty() || duration <= 0.0f || !isInActiveArea(object)) {
        return;
    }

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

bool FloatingText::isInActiveArea(const Object &object) const {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    return area && findAreaObject(*area, object.id());
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
    if (_entries.empty()) {
        return;
    }
    if (!_font) {
        _font = _services.resource.fonts.getExact("fnt_d16x16");
        if (!_font || _font->height() <= 0.0f) {
            _font = _services.resource.fonts.getExact("dialogfont16x16");
        }
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
    const float width = static_cast<float>(_game.options().graphics.width);
    const float height = static_cast<float>(_game.options().graphics.height);

    _services.graphics.uniforms.setGlobals([width, height](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            width,
            height,
            0.0f,
            0.0f,
            100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });

    auto renderEntries = [this, &area, &projection, &view, &graphicsCamera, width, height]() {
        for (const Entry &entry : _entries) {
            auto object = findAreaObject(*area, entry.objectId);
            if (!object) {
                continue;
            }

            auto sceneNode = object->sceneNode();
            if (!sceneNode || !sceneNode->isEnabled()) {
                continue;
            }

            glm::vec3 selectablePosition = object->getSelectablePosition();
            if (glm::dot(
                    graphicsCamera->forward(),
                    selectablePosition - graphicsCamera->position()) <= 0.0f) {
                continue;
            }

            glm::vec3 screen = area->getSelectableScreenCoords(
                object,
                projection,
                view);
            if (screen.z >= 1.0f) {
                continue;
            }

            float alpha = glm::clamp(entry.remaining / entry.duration, 0.0f, 1.0f);
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

            glm::vec3 position(
                width * screen.x,
                height * (1.0f - screen.y) - kFloatingTextOffsetY -
                    static_cast<float>(entry.stack - 1) * _font->height(),
                0.0f);

            _font->render(
                entry.text,
                position,
                glm::vec4(color, alpha),
                TextGravity::CenterTop);
        }
    };

    _services.graphics.context.withDepthTestMode(DepthTestMode::None, [this, &renderEntries]() {
        _services.graphics.context.withDepthMask(false, [this, &renderEntries]() {
            _services.graphics.context.withBlendMode(BlendMode::Normal, renderEntries);
        });
    });
}

void FloatingText::reset() {
    _entries.clear();
    _font.reset();
}

} // namespace game

} // namespace reone
