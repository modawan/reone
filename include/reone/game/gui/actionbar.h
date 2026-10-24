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

#pragma once

#include "reone/script/types.h"

#include "actionslot.h"

namespace reone {

namespace gui {
class Button;
class Label;
} // namespace gui

namespace graphics {
class Texture;
} // namespace graphics

namespace game {

struct ServicesView;
struct Spell;
class Creature;
class Game;
class Item;

class ActionBar {
public:
    ActionBar(Game &game, ServicesView &services) :
        _game(game), _services(services) {}

    void addDescription(std::shared_ptr<gui::Label> desc,
                        std::shared_ptr<gui::Label> background);
    /**
     * TSL: the box the bar's form, implant-mode and behaviour selections
     * announce themselves in, and that prompts for the party member to use a
     * healing item on. A click on the prompt ends the choice.
     */
    void addNotice(std::shared_ptr<gui::Button> notice);

    void addSlot(std::shared_ptr<gui::Button> button,
                 std::shared_ptr<gui::Button> action,
                 std::shared_ptr<gui::Button> up,
                 std::shared_ptr<gui::Button> down);

    void update(float dt);
    /** \p tag names the action button or a scroll arrow of one of the bar's slots, which sound for themselves. */
    bool ownsActionControl(const std::string &tag) const;
    void render(float layoutScale);
    /** Take the rejection message off the bar's description. */
    void clearRejection() { _feedback.clearRejection(); }

    /** TSL: the bar waits for the party member to use a healing item on. */
    bool isChoosingPartyItemTarget() const { return _partyItemTarget.has_value(); }
    /**
     * Use the item the bar waits to place on the party member at
     * \p memberIndex, if that member can use it, and stop waiting.
     */
    void usePartyItemOn(int memberIndex);
    /** The party member at \p memberIndex is pointed at, or none is, while the bar waits for one. */
    void pointAtPartyItemTarget(std::optional<int> memberIndex);
    /** Stop waiting for the party member to use a healing item on. */
    void cancelPartyItemTarget();

private:
    friend class ActionBarTestAccess;

    /** A slot's position on the bar: the powers, the healing items, the items used on oneself, the mines. */
    static constexpr size_t kHealingSlot = 1;
    static constexpr size_t kSelfItemSlot = 2;
    static constexpr size_t kMinesSlot = 3;

    struct Slot {
        ActionSlot slot;
        std::shared_ptr<gui::Button> button;
        std::shared_ptr<gui::Button> action;
        std::shared_ptr<gui::Button> up;
        std::shared_ptr<gui::Button> down;
        std::shared_ptr<graphics::Texture> upBorderFill;
        std::shared_ptr<graphics::Texture> upHilightFill;
        std::shared_ptr<graphics::Texture> downBorderFill;
        std::shared_ptr<graphics::Texture> downHilightFill;
        /** The entry the slot is set to; while the slot no longer offers it, the slot shows its first entry. */
        std::optional<ContextAction> selection;
    };

    /** An item the leader can draw on, and whether the leader wears it. */
    struct CarriedItem {
        std::shared_ptr<Item> item;
        bool worn {false};
    };

    /** A healing item the bar waits to use on the party member the player picks. */
    struct PartyItemTarget {
        std::shared_ptr<Item> item;
        std::shared_ptr<Spell> spell;
        /** The party as it stood when the wait began; any of them leaving ends it. */
        std::vector<uint32_t> members;
    };

    Game &_game;
    ServicesView &_services;

    std::shared_ptr<gui::Label> _desc;
    std::shared_ptr<gui::Label> _descBg;
    std::shared_ptr<gui::Button> _notice;

    std::vector<Slot> _slots;
    ActionMenuFeedback _feedback;
    /** The leader the slots were last filled for. */
    uint32_t _leaderId {script::kObjectInvalid};
    /** The leader is down, which greys every slot. */
    bool _leaderDown {false};
    /** The items the leader can draw on, gathered anew each frame: what it wears on its arms and belt, then the party's inventory. */
    std::vector<CarriedItem> _carried;
    std::optional<PartyItemTarget> _partyItemTarget;
    /** The party member pointed at while the bar waits for one. */
    std::optional<int> _pointedMember;

    /** Scroll \p guiSlot to its previous entry for a positive \p y, otherwise to its next. */
    void handleMouseWheel(Slot &guiSlot, int x, int y);
    void handleMouseButtonDown(size_t slotIndex);
    /** Select the entry of \p guiSlot that is the leader's current form, implant mode or behaviour. */
    void showCurrentSelection(Slot &guiSlot, const Creature &leader) const;
    /**
     * TSL: a healing item from its slot goes to a party member the player
     * picks. A party of one, or a squad recovery kit, uses it on the leader
     * at once, if the leader can use it.
     */
    void selectPartyItemTarget(const std::shared_ptr<Creature> &leader, const std::shared_ptr<Item> &item,
                               const std::shared_ptr<Spell> &spell);
    /** The prompt of the choice of a party member, naming the one pointed at. */
    std::string partyItemTargetPrompt() const;
    /** Show the prompt, or else a selection's announcement, in the notice box. */
    void updateNotice();
};

} // namespace game
} // namespace reone
