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

#include <algorithm>
#include <functional>

#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/useskill.h"
#include "reone/game/combat.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/forcerules.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/input/event.h"
#include "reone/resource/strings.h"

namespace reone {
namespace game {

static constexpr int kActionWidth = 26;
/** The tutorial window that scrolling an action menu brings. */
static constexpr int kActionMenuTutorial = 5;
/** Each form announces its selection with its own message, in form order. */
static constexpr uint32_t kFirstFormMessage = 124098;

/** The tutorial window and message a behaviour selection brings. */
struct BehaviorSelection {
    int tutorial;
    uint32_t message;
};

/**
 * The selection of behaviour \p style. Support has a Jedi form and a
 * grenadier form, chosen by the leader's last class. Other styles bring none.
 */
static std::optional<BehaviorSelection> behaviorSelection(NPCAIStyle style, const Creature &leader) {
    switch (style) {
    case NPCAIStyle::PartyAggro:
        return BehaviorSelection {61, 123708};
    case NPCAIStyle::PartyRanged:
        return BehaviorSelection {63, 123710};
    case NPCAIStyle::PartyStationary:
        return BehaviorSelection {64, 123711};
    case NPCAIStyle::PartySupport:
        if (isForceUsingClass(leader.attributes().getEffectiveClass(), true)) {
            return BehaviorSelection {65, 126014};
        }
        return BehaviorSelection {66, 126054};
    default:
        return std::nullopt;
    }
}

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
    up->setSharpenBorderFillAlpha(true);
    down->setSharpenBorderFillAlpha(true);

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

    // A wheel notch over a slot scrolls it like its arrows.
    auto cycleOnMouseWheel = [slotIndex, this](int x, int y) {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, x, y);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
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
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
    });
    down->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, 0, -1);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
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
    // A power the leader cannot use is refused with its reason.
    if (!ctxAction.form && !ctxAction.implant && ctxAction.type == ActionType::CastSpellAtObject) {
        if (!ctxAction.item) {
            ctxAction.availability = leader->powerMenuStatus(*ctxAction.spell);
        }
        if (!ctxAction.availability.available()) {
            _feedback.reject(ctxAction.availability.reason, _services);
            return;
        }
    }
    // Every entry runs inside the bar's wrapper. Out of combat mode it first
    // drops the leader's pending round entries; when the entry is done and
    // combat mode is still off, it clears the leader's actions, before the
    // entry's order arrives.
    if (!leader->clientCombatMode()) {
        _game.combat().removeAllScheduled(*leader);
        _feedback.accept(_services);
    } else if (_game.isAutoPaused()) {
        // In combat mode an entry taken during an autopause shows its own
        // pause reason.
        _game.setPaused(true, PauseReason::CombatOrder);
    }
    std::function<void()> order;
    // A leader that cannot be commanded takes no power or mine; a form, an
    // implant mode or a behaviour takes effect whatever its state.
    auto queue = [&](std::shared_ptr<Action> action) {
        order = [leader, action]() {
            if (!leader->isCommandable()) {
                return;
            }
            action->setUserAction(true);
            leader->addAction(action);
        };
    };
    if (ctxAction.form) {
        // A form takes effect at once and announces itself.
        leader->setCurrentForm(static_cast<CombatForm>(ctxAction.spell->type));
        _feedback.announce(kFirstFormMessage + static_cast<uint32_t>(ctxAction.spell->type) -
                           static_cast<uint32_t>(SpellType::FormSaberIShiiCho));
    } else if (ctxAction.implant) {
        // So does an implant mode.
        leader->switchImplantMode(ctxAction.implant->mode);
        _feedback.announce(ctxAction.implant->messageStrRef);
    } else if (ctxAction.behavior) {
        // So does a behaviour: the leader fights in its style from now on. The
        // selection asks for its tutorial window and announces itself.
        // An entry of any other style changes nothing and announces an empty
        // line.
        if (auto selection = behaviorSelection(ctxAction.behavior->style, *leader)) {
            leader->setAIStyle(ctxAction.behavior->style);
            _game.requestTutorialWindow(selection->tutorial);
            _feedback.announce(selection->message);
        } else {
            _feedback.announce(std::nullopt);
        }
    } else if (ctxAction.type == ActionType::UseSkill) {
        // A mine from the mines slot is set where the leader stands. Using the
        // kit clears the leader's actions out of combat, and so does setting
        // the mine whenever the area takes another, unless the leader is busy.
        queue(_game.newAction<UseSkillAction>(ctxAction.skill, leader, 0, ctxAction.item));
        order = [this, leader, queued = std::move(order)]() {
            if (!leader->isInCombat() || _game.module()->area()->playerCanSetMines()) {
                leader->clearOrdersUnlessBusy();
            }
            queued();
        };
    } else {
        assert((ctxAction.type == ActionType::CastSpellAtObject) && "unexpected action");
        // A leader without a Jedi class casts nothing. A power on the leader
        // asks for its tutorial window the first time, which then casts it.
        bool cast = true;
        if (!ctxAction.item) {
            if (!_game.canMenuCast(*leader, *ctxAction.spell)) {
                order = [this, leader]() { _game.refuseMenuCast(*leader); };
                cast = false;
            } else if (auto tutorial = _game.forcePowerTutorial(*ctxAction.spell);
                       tutorial && _game.requestTutorialWindow(*tutorial, leader->id(), leader->id(),
                                                               static_cast<uint32_t>(ctxAction.spell->type))) {
                cast = false;
            }
        }
        if (cast) {
            // An item goes out as its use on the leader, a power as its cast.
            if (auto item = ctxAction.item) {
                order = [this, leader, item, spell = ctxAction.spell]() {
                    _game.useMenuItem(*leader, item, item->spellProperty(spell->type), leader,
                                      menuItemLocation(*leader, *leader));
                };
            } else {
                order = [this, leader, spell = ctxAction.spell]() { _game.sendMenuCast(*leader, spell, leader); };
            }
            debug(str(boost::format("Spell selected: actor=%u spell=%d target=%u") % leader->id() % static_cast<int>(ctxAction.spell->type) % leader->id()), LogChannel::Combat);
        }
    }
    if (!leader->clientCombatMode()) {
        _game.combat().clearActions(*leader);
    }
    if (order) {
        order();
    }
}

static std::string getDescription(const ContextAction &action, const Creature &creature, const Game &game,
                                  ServicesView &services) {
    if (action.implant) {
        return game.getInterfaceText(action.implant->nameStrRef);
    }
    if (action.behavior) {
        return game.getInterfaceText(action.behavior->nameStrRef);
    }
    if (action.type != ActionType::CastSpellAtObject && !(action.type == ActionType::UseSkill && action.item)) {
        return "";
    }

    if (action.item) {
        const std::string &name = action.item->localizedName();
        int stackSize = action.item->stackSize();
        std::string text = stackSize == 1 ? name : str(boost::format("%s (%d)") % name % stackSize);
        // TSL shows a usable item's entry with its actions hidden.
        return game.isTSL() ? game.substituteLogTokens(std::move(text)) : text;
    }

    // A power a feat grants is named for its feat.
    if (action.feat != FeatType::Invalid) {
        return services.game.feats.get(action.feat)->name;
    }
    return action.spell->name;
}

bool ActionBar::ownsActionControl(const std::string &tag) const {
    return std::any_of(_slots.begin(), _slots.end(), [&](const auto &slot) {
        return slot.action->tag() == tag;
    });
}

void ActionBar::showCurrentSelection(ActionSlot &slot, const Creature &leader) const {
    for (size_t index = 0; index < slot.actions.size(); ++index) {
        const ContextAction &action = slot.actions[index];
        const bool current =
            (action.form && static_cast<CombatForm>(action.spell->type) == leader.currentForm()) ||
            (action.implant && action.implant->mode == leader.implantMode()) ||
            (action.behavior && action.behavior->style == leader.aiStyle());
        if (current) {
            slot.indexSelected = index;
            return;
        }
    }
}

void ActionBar::update(float dt) {
    _feedback.update(dt);
    std::shared_ptr<Creature> leader = _game.party().getLeader();
    // With no one leading the menus are not filled.
    if (!leader) {
        return;
    }
    // The item slots offer what the leader can use from the party's inventory
    // and from what it wears: healing kits (and TSL stims) in one, KotOR stims
    // and self-targeted item spells in the other.
    const auto inventory = _game.party().sharedInventoryReceiver(leader);
    _carried.clear();
    for (const auto &item : inventory->items()) {
        _carried.push_back(item);
    }
    for (const auto &[slotIndex, item] : leader->equipment()) {
        if (item && std::find(_carried.begin(), _carried.end(), item) == _carried.end()) {
            _carried.push_back(item);
        }
    }
    auto addUsableItems = [&](int flags, std::vector<ContextAction> &actions) {
        for (const auto &item : _carried) {
            const auto spellType = item->activateSpell();
            if (!spellType || !isLeaderUsableItem(*leader, *item, flags)) {
                continue;
            }
            if (auto spell = _services.game.spells.get(*spellType)) {
                actions.push_back(ContextAction(item, spell));
            }
        }
    };
    const bool leaderChanged = leader->id() != _leaderId;
    _leaderId = leader->id();

    for (size_t i = 0; i < _slots.size(); ++i) {
        Slot &guiSlot = _slots[i];
        ActionSlot &slot = guiSlot.slot;
        slot.actions.clear();
        switch (i) {
        case 0: {
            slot.actions = leader->powerMenuActions(std::nullopt, false);
            break;
        }
        case 1:
            addUsableItems(LeaderUsableFlags::healing, slot.actions);
            break;
        case 2:
            addUsableItems(LeaderUsableFlags::self, slot.actions);
            break;
        case 3: {
            // Mines: kits the leader can set, unless the area restricts the player.
            const auto &area = *_game.module()->area();
            if (area.playerRestrictMode() || !leader->attributes().hasSkill(SkillType::Demolitions)) {
                break;
            }
            for (const auto &item : inventory->items()) {
                if (item->isMineKit()) {
                    slot.actions.push_back(ContextAction(SkillType::Demolitions, 0, item));
                }
            }
            break;
        }
        case 4: {
            // TSL: behaviours.
            if (_game.isTSL()) {
                slot.actions = leader->behaviorMenuActions();
            }
            break;
        }
        case 5: {
            // Forms.
            slot.actions = leader->formMenuActions();
            break;
        }
        default:
            break;
        }

        if (!slot.actions.empty()) {
            slot.indexSelected = std::min(slot.indexSelected, slot.actions.size() - 1);
            // A slot shows the current form, implant mode or behaviour again
            // when the leader changes, and whenever it shows one while the
            // pointer is off it.
            const ContextAction &shown = slot.actions[slot.indexSelected];
            if (!guiSlot.button->isSelected() && (leaderChanged || shown.form || shown.implant || shown.behavior)) {
                showCurrentSelection(slot, *leader);
            }
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
        _desc->setTextMessage(getDescription(selectedAction, *leader, _game, _services));
    }

    if (_feedback.active()) {
        showDesc = true;
        _desc->setTextMessage(_feedback.text(_game));
    } else if (_feedback.announcing()) {
        showDesc = true;
        _desc->setTextMessage(_feedback.announcementText(_game));
    }
    _desc->setTextOpacity(_feedback.descriptionOpacity());
    _desc->setVisible(showDesc);
    _descBg->setVisible(showDesc);
}

void ActionBar::render(float layoutScale) {
    for (const Slot &slot : _slots) {
        const std::vector<ContextAction> &actions = slot.slot.actions;
        if (actions.empty()) {
            continue;
        }

        gui::Control::Extent slotExtent = slot.button->extent();
        // Icons are artwork in the button's layout space, not text.
        float iconSize = kActionWidth * layoutScale;
        int centerX, centerY;
        slotExtent.getCenter(centerX, centerY);
        float x = centerX - iconSize / 2;
        float y = centerY - iconSize / 2;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
        transform = glm::scale(transform, glm::vec3(iconSize, iconSize, 1.0f));
        renderContextActionIcon(actions[slot.slot.indexSelected], transform, _services);
    }
}

} // namespace game
} // namespace reone
