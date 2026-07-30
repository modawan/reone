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
#include <cmath>

#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/graphics/font.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/strings.h"
#include "reone/scene/node/camera.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

static constexpr float kFloatingTextOffsetY = 32.0f;
static constexpr float kFloatingTextDuration = 1.5f;
static constexpr int kFloatingTextWidth = 200;
static constexpr int kMaxFloatingTextEntries = 5;
static constexpr int kMissStrRef = 1373;

static const glm::vec3 kDamageColor(0.74f, 0.11f, 0.0f);
static const glm::vec3 kHealColor(0.28f, 0.92f, 0.11f);
static const glm::vec3 kMissColor(1.0f);

static std::shared_ptr<Object> findAreaObject(const Area &area, uint32_t objectId) {
    auto it = std::find_if(
        area.objects().begin(),
        area.objects().end(),
        [objectId](const auto &object) {
            return object && object->id() == objectId;
        });
    return it != area.objects().end() ? *it : nullptr;
}

void FloatingText::init(gui::IGUI &gui) {
    hideLabels();
    _labels.clear();
    _font.reset();
    _fontLoadFailed = false;
    _gui = _game.isTSL() ? nullptr : &gui;
}

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
        add(object, std::to_string(std::max(0, amount)), Style::Damage, kFloatingTextDuration);
    } else if (object.id() == leader->id()) {
        add(object, std::to_string(std::max(0, adjustedAmount)), Style::Damage, kFloatingTextDuration);
    }
}

void FloatingText::addHeal(const Object &object, int amount) {
    if (_game.isTSL()) {
        return;
    }

    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    auto selected = area ? area->selectedObject() : nullptr;
    if (!_game.party().isMember(object) && (!selected || selected->id() != object.id())) {
        return;
    }

    add(object, std::to_string(std::max(0, amount)), Style::Heal, kFloatingTextDuration);
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
    if (text.empty() || duration <= 0.0f) {
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

bool FloatingText::ensureLabelCount(std::size_t count) {
    if (!_gui || !_font) {
        return false;
    }

    while (_labels.size() < count) {
        auto control = _gui->newControl(
            gui::ControlType::Label,
            "__FLOATING_TEXT_" + std::to_string(_labels.size()));
        if (!control) {
            return false;
        }

        std::shared_ptr<gui::Control> base(std::move(control));
        auto label = std::dynamic_pointer_cast<gui::Label>(base);
        if (!label) {
            return false;
        }

        gui::Control::Text text;
        text.font = _font;
        text.align = gui::Control::TextAlign::CenterCenter;
        label->setExtent({0, 0, kFloatingTextWidth, static_cast<int>(std::ceil(_font->height()))});
        label->setText(std::move(text));
        label->setSelectable(false);
        label->setVisible(false);

        _gui->addControlToBack(label);
        _labels.push_back(std::move(label));
    }
    return true;
}

void FloatingText::hideLabels() {
    for (const auto &label : _labels) {
        label->setVisible(false);
    }
}

void FloatingText::prepare() {
    hideLabels();

    if (_game.isTSL() || !_gui || _entries.empty()) {
        return;
    }
    if (!_font && !_fontLoadFailed) {
        _font = _services.resource.fonts.getExact("fnt_d16x16");
        _fontLoadFailed = !_font || _font->height() <= 0.0f;
        if (_fontLoadFailed) {
            warn("FloatingText: unable to load KOTOR 1 combat font");
            _font.reset();
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
    const int screenWidth = _game.options().graphics.width;
    const int screenHeight = _game.options().graphics.height;
    const int lineHeight = std::max(1, static_cast<int>(std::ceil(_font->height())));
    const glm::ivec2 &controlOffset = _gui->controlOffset();

    std::size_t labelIndex = 0;
    for (const Entry &entry : _entries) {
        auto object = findAreaObject(*area, entry.objectId);
        if (!object) {
            continue;
        }

        glm::vec3 screen = area->getSelectableScreenCoords(object, projection, view);
        if (!std::isfinite(screen.x) ||
            !std::isfinite(screen.y) ||
            !std::isfinite(screen.z) ||
            screen.z >= 1.0f) {
            continue;
        }
        if (!ensureLabelCount(labelIndex + 1)) {
            break;
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

        int x = static_cast<int>(std::lround(screenWidth * screen.x));
        int y = static_cast<int>(std::lround(screenHeight * (1.0f - screen.y)));
        int left = x - kFloatingTextWidth / 2 - controlOffset.x;
        int top = y - static_cast<int>(kFloatingTextOffsetY) - entry.stack * lineHeight - controlOffset.y;

        auto &label = *_labels[labelIndex++];
        label.setExtent({left, top, kFloatingTextWidth, lineHeight});
        label.setTextFont(_font);
        label.setTextMessage(entry.text);
        label.setTextColor(color);
        label.setTextAlpha(glm::clamp(entry.remaining / entry.duration, 0.0f, 1.0f));
        label.setVisible(true);
    }
}

void FloatingText::reset() {
    _entries.clear();
    hideLabels();
    _font.reset();
    _fontLoadFailed = false;
}

} // namespace game

} // namespace reone
