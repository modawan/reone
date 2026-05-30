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

#include "reone/game/gui/actionbar.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/d20/spell.h"
#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/input/event.h"

namespace reone {
namespace game {

static constexpr int kActionWidth = 26;

static void rotateActionBarArrow(std::shared_ptr<gui::Button> button) {
    if (!button) {
        return;
    }
    button->setBorderFillTransform(gui::Control::Border::FillTransform::Rotate180);
    button->setHilightFillTransform(gui::Control::Border::FillTransform::Rotate180);
}

void ActionBar::addDescription(std::shared_ptr<gui::Label> desc,
                               std::shared_ptr<gui::Label> background) {
    _desc = desc;
    _descBg = background;

    _desc->setVisible(false);
    _descBg->setVisible(false);
}

void ActionBar::addSlot(std::shared_ptr<gui::Button> button,
                        std::shared_ptr<gui::Button> action,
                        std::shared_ptr<gui::Button> up,
                        std::shared_ptr<gui::Button> down) {
    rotateActionBarArrow(down);
    button->setSelectable(false);

    size_t slotIndex = _slots.size();
    _slots.push_back({
        ActionSlot(),
        button,
        action,
        up,
        down,
        up->border().fill,
        up->hilight().fill,
        down->border().fill,
        down->hilight().fill});
    up->setBorderFill(std::shared_ptr<graphics::Texture>());
    up->setHilightFill(std::shared_ptr<graphics::Texture>());
    down->setBorderFill(std::shared_ptr<graphics::Texture>());
    down->setHilightFill(std::shared_ptr<graphics::Texture>());

    auto cycleOnMouseWheel = [slotIndex, this](int x, int y) {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, x, y);
    };
    action->setOnMouseWheel(cycleOnMouseWheel);
    up->setOnMouseWheel(cycleOnMouseWheel);
    down->setOnMouseWheel(cycleOnMouseWheel);

    action->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseButtonDown(slot);
    });
    up->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, 0, 1);
    });
    down->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, 0, -1);
    });

    auto handleSelectionChanged = [slotIndex, this](bool selected) {
        _slots[slotIndex].button->setSelected(selected);
    };
    action->setOnSelectionChanged(handleSelectionChanged);
    up->setOnSelectionChanged(handleSelectionChanged);
    down->setOnSelectionChanged(handleSelectionChanged);
}

void ActionBar::handleMouseWheel(ActionSlot &slot, int x, int y) {
    if (slot.actions.empty()) {
        return;
    }

    if (y > 0) {
        slot.indexSelected = slot.indexSelected == 0
                                 ? slot.actions.size() - 1
                                 : slot.indexSelected - 1;
    } else {
        slot.indexSelected = (slot.indexSelected + 1) % slot.actions.size();
    }
}

void ActionBar::handleMouseButtonDown(ActionSlot &slot) {
    if (slot.actions.empty()) {
        return;
    }

    std::shared_ptr<Creature> leader = _game.party().getLeader();
    if (!leader) {
        return;
    }

    ContextAction &ctxAction = slot.actions[slot.indexSelected];
    assert((ctxAction.type == ActionType::CastSpellAtObject) && "unexpected action");

    std::optional<std::shared_ptr<Item>> item = ctxAction.item ? std::optional(ctxAction.item) : std::nullopt;

    std::shared_ptr<Action> action = _game.newAction<CastSpellAtObjectAction>(
        ctxAction.spell, leader, item);
    action->setUserAction(true);
    leader->addAction(std::move(action));
}

static std::string getDescription(const ContextAction &action, const Creature &creature) {
    if (action.type != ActionType::CastSpellAtObject) {
        return "";
    }

    if (action.item) {
        const std::string &name = action.item->localizedName();
        int stackSize = action.item->stackSize();
        if (stackSize == 1) {
            return name;
        }
        return str(boost::format("%s (%d)") % name % stackSize);
    }

    return action.spell->name;
}

void ActionBar::update() {
    std::shared_ptr<Creature> leader = _game.party().getLeader();
    const ItemAttributes &itemAttrs = leader->itemAttributes();

    for (size_t i = 0; i < _slots.size(); ++i) {
        Slot &guiSlot = _slots[i];
        ActionSlot &slot = guiSlot.slot;
        slot.actions.clear();
        switch (i) {
        case 0: {
            // TODO: Force powers.
            break;
        }
        case 1: {
            // Healing items: medpacks, stimulants.
            for (const auto &[item, spell] : itemAttrs.healingSpells()) {
                slot.actions.push_back(ContextAction(item, spell));
            }

            break;
        }
        case 2: {
            // Defensive items: shields.
            for (const auto &[item, spell] : itemAttrs.defensiveSpells()) {
                slot.actions.push_back(ContextAction(item, spell));
            }
            break;
        }
        case 3: {
            // TODO: Mines.
            break;
        }
        default:
            // TODO: TSL slots.
            break;
        }

        if (!slot.actions.empty()) {
            slot.indexSelected = std::min(slot.indexSelected, slot.actions.size() - 1);
        } else {
            slot.indexSelected = 0;
        }
        bool showArrows = slot.actions.size() > 1;
        guiSlot.up->setBorderFill(showArrows ? guiSlot.upBorderFill : nullptr);
        guiSlot.up->setHilightFill(showArrows ? guiSlot.upHilightFill : nullptr);
        guiSlot.down->setBorderFill(showArrows ? guiSlot.downBorderFill : nullptr);
        guiSlot.down->setHilightFill(showArrows ? guiSlot.downHilightFill : nullptr);
    }

    bool showDesc = false;
    for (Slot &guiSlot : _slots) {
        ActionSlot &slot = guiSlot.slot;

        if (slot.actions.empty() || !guiSlot.button->isSelected()) {
            continue;
        }

        showDesc = true;
        const ContextAction &selectedAction = slot.actions[slot.indexSelected];
        _desc->setTextMessage(getDescription(selectedAction, *leader));
    }

    _desc->setVisible(showDesc);
    _descBg->setVisible(showDesc);
}

void ActionBar::render() {
    for (const Slot &slot : _slots) {
        const std::vector<ContextAction> &actions = slot.slot.actions;
        if (actions.empty()) {
            continue;
        }

        gui::Control::Extent slotExtent = slot.button->extent();
        int x, y;
        slotExtent.getCenter(x, y);
        x -= kActionWidth / 2;
        y -= kActionWidth / 2;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
        transform = glm::scale(transform, glm::vec3(kActionWidth, kActionWidth, 1.0f));
        renderContextActionIcon(actions[slot.slot.indexSelected], transform, _services);
    }
}

} // namespace game
} // namespace reone
