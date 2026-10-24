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
/** The words an item entry adds to its name: KotOR's for every item, TSL's for a worn one. */
static constexpr int kItemEntryLabel = 38005;
/**
 * The prompt for the party member to use a healing item on: with no one
 * pointed at; leading the name of one who can, or of one who cannot, use
 * the item; and the line under either.
 */
static constexpr int kPartyItemTargetPrompt = 124446;
static constexpr int kPartyItemTargetCanUse = 124447;
static constexpr int kPartyItemTargetCannotUse = 124448;
static constexpr int kPartyItemTargetHint = 124449;
/** The party members a squad recovery kit heals besides the leader. */
static constexpr int kSquadMembersBesideLeader = 2;

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

static bool isHurt(const Creature &creature) {
    return creature.currentHitPoints() < creature.maxHitPoints();
}

/** The two entries are the same power, item, feat, skill, form, implant mode or behaviour. */
static bool isSameEntry(const ContextAction &left, const ContextAction &right) {
    auto spellType = [](const ContextAction &entry) {
        return entry.spell ? std::optional<SpellType>(entry.spell->type) : std::nullopt;
    };
    auto implantMode = [](const ContextAction &entry) {
        return entry.implant ? std::optional<int>(entry.implant->mode) : std::nullopt;
    };
    auto behaviorStyle = [](const ContextAction &entry) {
        return entry.behavior ? std::optional<NPCAIStyle>(entry.behavior->style) : std::nullopt;
    };
    return left.type == right.type && left.feat == right.feat && left.skill == right.skill &&
           left.subSkill == right.subSkill && left.item == right.item && left.form == right.form &&
           spellType(left) == spellType(right) && implantMode(left) == implantMode(right) &&
           behaviorStyle(left) == behaviorStyle(right);
}

/**
 * Whether the entry for \p item can be used. KotOR refuses a medical or
 * repair kit while the leader is unhurt and not poisoned, and a squad
 * recovery kit while neither the leader nor the next two party members is
 * hurt. Otherwise a worn item, but for a TSL wrist launcher, takes its use
 * from its cast-spell property.
 */
static PowerMenuStatus itemEntryStatus(Game &game, const Creature &leader, const Item &item, const Spell &spell, bool worn) {
    PowerMenuStatus status;
    const bool tsl = game.isTSL();
    const int type = item.itemType();
    if (!tsl && (type == Item::kMedicalItemType || type == Item::kDroidRepairItemType) && !isHurt(leader)) {
        if (leader.activePoisonEffectId() == kUnassignedEffectId) {
            status.disallow(PowerUnavailableReason::NoOneToHeal);
        }
        return status;
    }
    if (!tsl && type == Item::kSquadRecoveryItemType) {
        bool anyoneHurt = isHurt(leader);
        for (int i = 1; i <= kSquadMembersBesideLeader; ++i) {
            auto member = game.party().getMember(i);
            if (member && isHurt(*member)) {
                anyoneHurt = true;
            }
        }
        if (!anyoneHurt) {
            status.disallow(PowerUnavailableReason::NoOneToHeal);
        }
        return status;
    }
    if (worn && !(tsl && type == Item::kWristLauncherItemType) && !item.properties()[*item.spellProperty(spell.type)].usable) {
        status.disallow(PowerUnavailableReason::None);
    }
    return status;
}

void ActionBar::addDescription(std::shared_ptr<gui::Label> desc,
                               std::shared_ptr<gui::Label> background) {
    _desc = desc;
    _descBg = background;

    _desc->setVisible(false);
    _descBg->setVisible(false);
}

void ActionBar::addNotice(std::shared_ptr<gui::Button> notice) {
    _notice = std::move(notice);
    _notice->setVisible(false);
    _notice->setOnClick([this]() { cancelPartyItemTarget(); });
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
    Slot slot;
    slot.button = button;
    slot.action = action;
    slot.up = up;
    slot.down = down;
    slot.upBorderFill = up->border().fill;
    slot.upHilightFill = up->hilight().fill;
    slot.downBorderFill = down->border().fill;
    slot.downHilightFill = down->hilight().fill;
    _slots.push_back(std::move(slot));
    up->setBorderFill(std::shared_ptr<graphics::Texture>());
    up->setHilightFill(std::shared_ptr<graphics::Texture>());
    down->setBorderFill(std::shared_ptr<graphics::Texture>());
    down->setHilightFill(std::shared_ptr<graphics::Texture>());

    // A wheel notch over a slot scrolls it like its arrows.
    auto cycleOnMouseWheel = [slotIndex, this](int x, int y) {
        handleMouseWheel(_slots[slotIndex], x, y);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
    };
    action->setOnMouseWheel(cycleOnMouseWheel);
    up->setOnMouseWheel(cycleOnMouseWheel);
    down->setOnMouseWheel(cycleOnMouseWheel);

    action->setOnClick([slotIndex, this]() {
        handleMouseButtonDown(slotIndex);
    });
    up->setOnClick([slotIndex, this]() {
        handleMouseWheel(_slots[slotIndex], 0, 1);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
    });
    down->setOnClick([slotIndex, this]() {
        handleMouseWheel(_slots[slotIndex], 0, -1);
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

void ActionBar::handleMouseWheel(Slot &guiSlot, int x, int y) {
    // Scrolling ends the choice of a party member to use an item on. A slot
    // of two or more entries moves to the previous or next one, wrapping,
    // and sounds it.
    cancelPartyItemTarget();
    ActionSlot &slot = guiSlot.slot;
    if (slot.actions.size() < 2) {
        return;
    }

    if (y > 0) {
        slot.indexSelected = slot.indexSelected == 0
                                 ? slot.actions.size() - 1
                                 : slot.indexSelected - 1;
    } else {
        slot.indexSelected = (slot.indexSelected + 1) % slot.actions.size();
    }
    guiSlot.selection = slot.actions[slot.indexSelected];
    _feedback.scroll(_services);
}

void ActionBar::handleMouseButtonDown(size_t slotIndex) {
    // Any entry ends the choice of a party member to use an item on.
    cancelPartyItemTarget();
    ActionSlot &slot = _slots[slotIndex].slot;
    if (slot.actions.empty()) {
        return;
    }

    std::shared_ptr<Creature> leader = _game.party().getLeader();
    if (!leader) {
        return;
    }

    ContextAction &ctxAction = slot.actions[slot.indexSelected];
    // A power the leader cannot use, or an item entry the bar offers but
    // refuses, is refused with its reason.
    if (!ctxAction.form && !ctxAction.implant && ctxAction.type == ActionType::CastSpellAtObject && !ctxAction.item) {
        ctxAction.availability = leader->powerMenuStatus(*ctxAction.spell);
    }
    if (!ctxAction.availability.available()) {
        _feedback.reject(ctxAction.availability.reason, _services);
        return;
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
            if (auto item = ctxAction.item; item && slotIndex == kHealingSlot && _game.isTSL()) {
                // TSL asks whom a healing item is for.
                order = [this, leader, item, spell = ctxAction.spell]() { selectPartyItemTarget(leader, item, spell); };
            } else if (item) {
                // An item goes out as its use on the leader, a power as its cast.
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

void ActionBar::selectPartyItemTarget(const std::shared_ptr<Creature> &leader, const std::shared_ptr<Item> &item,
                                      const std::shared_ptr<Spell> &spell) {
    Party &party = _game.party();
    if (party.getSize() == 1 || item->itemType() == Item::kSquadRecoveryItemType) {
        if (canUseItem(*leader, *item, true)) {
            _game.useMenuItem(*leader, item, item->spellProperty(spell->type), leader,
                              menuItemLocation(*leader, *leader));
        }
        return;
    }
    PartyItemTarget target;
    target.item = item;
    target.spell = spell;
    for (int i = 0; i < party.getSize(); ++i) {
        target.members.push_back(party.getMember(i)->id());
    }
    _partyItemTarget = std::move(target);
}

void ActionBar::usePartyItemOn(int memberIndex) {
    PartyItemTarget target = std::move(*_partyItemTarget);
    cancelPartyItemTarget();
    std::shared_ptr<Creature> leader = _game.party().getLeader();
    std::shared_ptr<Creature> member = _game.party().getMember(memberIndex);
    // An item used up meanwhile is no longer there to use.
    if (!target.item->isRuntimeLive() || !canUseItem(*member, *target.item, true)) {
        return;
    }
    _game.useMenuItem(*leader, target.item, target.item->spellProperty(target.spell->type), member,
                      menuItemLocation(*leader, *member));
}

void ActionBar::pointAtPartyItemTarget(std::optional<int> memberIndex) {
    if (_partyItemTarget) {
        _pointedMember = memberIndex;
    }
}

void ActionBar::cancelPartyItemTarget() {
    _partyItemTarget.reset();
    _pointedMember.reset();
}

std::string ActionBar::partyItemTargetPrompt() const {
    if (!_pointedMember) {
        return _game.getInterfaceText(kPartyItemTargetPrompt) + "\n" + _game.getInterfaceText(kPartyItemTargetHint);
    }
    std::shared_ptr<Creature> member = _game.party().getMember(*_pointedMember);
    const Item &item = *_partyItemTarget->item;
    const bool usable = item.isRuntimeLive() && canUseItem(*member, item, true);
    return _game.getInterfaceText(usable ? kPartyItemTargetCanUse : kPartyItemTargetCannotUse) + " " +
           member->name() + "\n" + _game.getInterfaceText(kPartyItemTargetHint);
}

void ActionBar::updateNotice() {
    // The prompt takes the box from any announcement, and only the prompt
    // can be clicked.
    if (_partyItemTarget) {
        _feedback.clearAnnouncement();
        _notice->setTextMessage(partyItemTargetPrompt());
        _notice->setDisabled(false);
        _notice->setVisible(true);
    } else if (_feedback.announcing()) {
        _notice->setTextMessage(_feedback.announcementText(_game));
        _notice->setDisabled(true);
        _notice->setVisible(true);
    } else {
        _notice->setVisible(false);
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
        const int stackSize = action.item->stackSize();
        if (!game.isTSL()) {
            // KotOR labels every item, then counts a stack.
            std::string text = str(boost::format("%s (%s)") % name % game.getInterfaceText(kItemEntryLabel));
            return stackSize > 1 ? str(boost::format("%s (%d)") % text % stackSize) : text;
        }
        // TSL labels a worn item, counts a stack, and shows the entry with its
        // actions hidden.
        std::string text;
        if (action.worn) {
            const std::string label = game.getInterfaceText(kItemEntryLabel);
            text = stackSize >= 2 ? str(boost::format("%s (%s) (%d)") % name % label % stackSize)
                                  : str(boost::format("%s (%s)") % name % label);
        } else {
            text = stackSize >= 2 ? str(boost::format("%s (%d)") % name % stackSize) : name + " ";
        }
        return game.substituteLogTokens(std::move(text));
    }

    // A power a feat grants is named for its feat.
    if (action.feat != FeatType::Invalid) {
        return services.game.feats.get(action.feat)->name;
    }
    return action.spell->name;
}

bool ActionBar::ownsActionControl(const std::string &tag) const {
    return std::any_of(_slots.begin(), _slots.end(), [&](const auto &slot) {
        return slot.action->tag() == tag || slot.up->tag() == tag || slot.down->tag() == tag;
    });
}

void ActionBar::showCurrentSelection(Slot &guiSlot, const Creature &leader) const {
    ActionSlot &slot = guiSlot.slot;
    for (size_t index = 0; index < slot.actions.size(); ++index) {
        const ContextAction &action = slot.actions[index];
        const bool current =
            (action.form && static_cast<CombatForm>(action.spell->type) == leader.currentForm()) ||
            (action.implant && action.implant->mode == leader.implantMode()) ||
            (action.behavior && action.behavior->style == leader.aiStyle());
        if (current) {
            slot.indexSelected = index;
            guiSlot.selection = action;
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
    Party &party = _game.party();
    // The choice of a party member to use an item on ends when the leader is
    // down or helpless, or anyone leaves the party.
    if (_partyItemTarget) {
        const bool memberLeft = std::any_of(_partyItemTarget->members.begin(), _partyItemTarget->members.end(), [&party](uint32_t id) {
            return std::none_of(party.members().begin(), party.members().end(), [id](const auto &member) {
                return member.creature && member.creature->id() == id;
            });
        });
        if (memberLeft || leader->isDebilitated() || leader->isDead() || leader->isTemporarilyDead()) {
            cancelPartyItemTarget();
        }
    }
    _leaderDown = leader->isDead() || leader->isTemporarilyDead();
    // The item slots offer what the leader can use from what it wears on its
    // arms and belt, unless the area restricts the player, then from the
    // party's inventory: healing kits (and TSL stims) in one, KotOR stims
    // and self-targeted item spells in the other, mine kits in a third.
    _carried.clear();
    if (!_game.module()->area()->playerRestrictMode()) {
        for (int slot : {InventorySlots::leftArm, InventorySlots::rightArm, InventorySlots::belt}) {
            if (auto item = leader->getEquippedItem(slot)) {
                _carried.push_back({item, true});
            }
        }
    }
    for (const auto &item : party.sharedInventoryReceiver(leader)->items()) {
        _carried.push_back({item, false});
    }
    auto addUsableItems = [&](int flags, std::vector<ContextAction> &actions) {
        for (const auto &[item, worn] : _carried) {
            const auto spellType = item->activateSpell();
            if (!spellType || !isLeaderUsableItem(*leader, *item, flags)) {
                continue;
            }
            if (auto spell = _services.game.spells.get(*spellType)) {
                ContextAction entry(item, spell);
                entry.worn = worn;
                entry.availability = itemEntryStatus(_game, *leader, *item, *spell, worn);
                actions.push_back(std::move(entry));
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
        case kHealingSlot:
            addUsableItems(LeaderUsableFlags::healing, slot.actions);
            break;
        case kSelfItemSlot:
            addUsableItems(LeaderUsableFlags::self, slot.actions);
            break;
        case kMinesSlot: {
            // Mines: kits the leader can set.
            for (const auto &[item, worn] : _carried) {
                if (isLeaderUsableItem(*leader, *item, LeaderUsableFlags::mines)) {
                    ContextAction entry(SkillType::Demolitions, 0, item);
                    entry.worn = worn;
                    slot.actions.push_back(std::move(entry));
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

        // A slot stays on the entry it was set to; while it no longer offers
        // that entry, it shows its first.
        slot.indexSelected = 0;
        if (guiSlot.selection) {
            auto selected = std::find_if(slot.actions.begin(), slot.actions.end(), [&](const ContextAction &action) {
                return isSameEntry(action, *guiSlot.selection);
            });
            if (selected != slot.actions.end()) {
                slot.indexSelected = static_cast<size_t>(selected - slot.actions.begin());
            }
        }
        if (!slot.actions.empty()) {
            // A slot shows the current form, implant mode or behaviour again
            // when the leader changes, and whenever it shows one while the
            // pointer is off it.
            const ContextAction &shown = slot.actions[slot.indexSelected];
            if (!guiSlot.button->isSelected() && (leaderChanged || shown.form || shown.implant || shown.behavior)) {
                showCurrentSelection(guiSlot, *leader);
            }
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
    }
    _desc->setTextOpacity(_feedback.descriptionOpacity());
    _desc->setVisible(showDesc);
    _descBg->setVisible(showDesc);
    if (_game.isTSL()) {
        updateNotice();
    }
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
        // Every slot is greyed while the leader is down.
        const ContextAction &action = actions[slot.slot.indexSelected];
        renderContextActionIcon(action, transform, action.availability.available() && !_leaderDown, _services);
    }
}

} // namespace game
} // namespace reone
