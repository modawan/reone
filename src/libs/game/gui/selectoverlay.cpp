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

#include "reone/game/gui/selectoverlay.h"

#include <cmath>
#include <functional>

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/doorsaber.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kOffsetToReticle = 8;
static constexpr int kTitleBarWidth = 250;
static constexpr int kTitleBarPadding = 6;
static constexpr int kHealthBarHeight = 6;
static constexpr float kObjectTitleScale = 2.0f / 3.0f;
static constexpr int kNumActionSlots = 3;
static constexpr int kActionBarMargin = 3;
static constexpr int kActionBarPadding = 3;
static constexpr int kActionWidth = 35;
static constexpr int kActionHeight = 59;
static constexpr int kActionArrowHeight = (kActionHeight - kActionWidth) / 2;
/** The message shown when the area holds as many player mines as it allows. */
static constexpr int kMineLimitStrRef = 47861;
/** The tutorial windows that the first combat feat, grenade, mine and action-menu scroll bring. */
static constexpr int kCombatFeatTutorial = 0;
static constexpr int kGrenadeTutorial = 1;
static constexpr int kSetMineTutorial = 2;
static constexpr int kActionMenuTutorial = 5;
// The hover reticle shows only this many pixels inside the edges of the HUD's
// arrow margin, and at this opacity.
static constexpr float kHoverReticleEdge = 32.0f;
static constexpr float kHoverReticleAlpha = 0.5f;

// The hover reticle's sizes in pixels: near, and far for a creature and for
// anything else.
static constexpr int kHoverReticleNearSize = 64;
static constexpr int kHoverReticleFarCreatureSize = 16;
static constexpr int kHoverReticleFarSize = 32;
// The distances in metres within which the hover reticle shows its near size,
// and beyond which its far size: TSL shrinks it sooner for anything but a creature.
static constexpr double kHoverReticleNearDistance = 5.0;
static constexpr double kHoverReticleFarDistance = 30.0;
static constexpr double kHoverReticleFarDistanceTSL = 10.0;

// The hover reticle is 64 pixels across within 5 metres of the leader and
// shrinks steadily to its far size: 16 pixels at 30 metres for a creature,
// 32 pixels for anything else, reached at 10 metres in TSL and at 30 in KotOR.
static int hoverReticleSize(bool creature, bool tsl, float distance2) {
    const double farDistance = !creature && tsl ? kHoverReticleFarDistanceTSL : kHoverReticleFarDistance;
    const int farSize = creature ? kHoverReticleFarCreatureSize : kHoverReticleFarSize;
    const double distance = std::sqrt(static_cast<double>(distance2));
    if (distance >= farDistance) {
        return farSize;
    }
    if (distance <= kHoverReticleNearDistance) {
        return kHoverReticleNearSize;
    }
    const double span = farDistance - kHoverReticleNearDistance;
    return kHoverReticleNearSize - static_cast<int>((distance - kHoverReticleNearDistance) / span * (kHoverReticleNearSize - farSize));
}

// A trap the leader knows about, flagged or found by it, that is hostile to it.
template <class TrappedObject>
static bool isKnownHostileTrap(const TrappedObject &object, const Creature &leader) {
    const TrapDetection &detection = object.trapDetection();
    return object.isTrapped() && object.isTrapHostileTo(leader) &&
           (detection.flagged || detection.isDetectedBy(leader.id()));
}

// A placeable regarding the leader at this standing or less is hostile to it.
static constexpr int kHighestHostileReputation = 10;

// The hover reticle is hostile for a hostile creature, for a door or placeable
// whose trap is hostile to the leader and known to it, and for a placeable of
// a hostile appearance that regards the leader at 10 or less.
static bool isHoverHostile(const Object &object, const Creature &leader, const Module &module, const Game &game) {
    switch (object.type()) {
    case ObjectType::Creature:
        return module.isHostileToPartyLeader(static_cast<const Creature &>(object));
    case ObjectType::Door:
        return isKnownHostileTrap(static_cast<const Door &>(object), leader);
    case ObjectType::Placeable: {
        const auto &placeable = static_cast<const Placeable &>(object);
        return isKnownHostileTrap(placeable, leader) ||
               (placeable.isHostileAppearance() && getObjectReputation(placeable, leader, game) <= kHighestHostileReputation);
    }
    default:
        return false;
    }
}

static void cycleActionSlot(ActionSlot &slot, bool previous) {
    if (slot.actions.empty())
        return;

    if (previous) {
        if (slot.indexSelected == 0) {
            slot.indexSelected = static_cast<uint32_t>(slot.actions.size() - 1);
        } else {
            --slot.indexSelected;
        }
    } else if (++slot.indexSelected == slot.actions.size()) {
        slot.indexSelected = 0;
    }
}

SelectionOverlay::SelectionOverlay(
    Game &game,
    ServicesView &services) :
    _game(game),
    _services(services) {
    _actionSlots.resize(kNumActionSlots);
}

void SelectionOverlay::init(std::shared_ptr<gui::Control> arrowMargin) {
    _arrowMargin = std::move(arrowMargin);
    _font = _services.resource.fonts.get("dialogfont16x16");
    _friendlyReticle = _services.resource.textures.get("friendlyreticle", TextureUsage::GUI);
    _friendlyReticle2 = _services.resource.textures.get("friendlyreticle2", TextureUsage::GUI);
    _hostileReticle = _services.resource.textures.get("hostilereticle", TextureUsage::GUI);
    _hostileReticle2 = _services.resource.textures.get("hostilereticle2", TextureUsage::GUI);
    _friendlyScroll = _services.resource.textures.get("lbl_miscroll_f", TextureUsage::GUI);
    _hostileScroll = _services.resource.textures.get("lbl_miscroll_h", TextureUsage::GUI);
    _hilightedScroll = _services.resource.textures.get("lbl_miscroll_hi", TextureUsage::GUI);
    _actionArrow = _services.resource.textures.get("lbl_miarr_1", TextureUsage::GUI);
    _hilightedActionArrow = _services.resource.textures.get("lbl_miarr_2", TextureUsage::GUI);
    _reticleHeight = _friendlyReticle2->height();
}

bool SelectionOverlay::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::MouseMotion:
        return handleMouseMotion(event.motion);
    case input::EventType::MouseButtonDown:
        return handleMouseButtonDown(event.button);
    case input::EventType::MouseWheel:
        return handleMouseWheel(event.wheel);
    default:
        return false;
    }
}

bool SelectionOverlay::handleMouseMotion(const input::MouseMotionEvent &event) {
    _selectedActionSlot = -1;
    _hilightedActionBand = ActionBand::None;

    if (!_selectedObject)
        return false;

    for (int i = 0; i < kNumActionSlots; ++i) {
        float x, y;
        getActionScreenCoords(i, x, y);
        float scale = layoutScale();
        if (event.x >= x && event.y >= y &&
            event.x < x + kActionWidth * scale &&
            event.y < y + kActionHeight * scale) {
            _selectedActionSlot = i;
            float actionY = event.y - y;
            if (actionY < kActionArrowHeight * scale) {
                _hilightedActionBand = ActionBand::Previous;
            } else if (actionY >= (kActionArrowHeight + kActionWidth) * scale) {
                _hilightedActionBand = ActionBand::Next;
            } else {
                _hilightedActionBand = ActionBand::Icon;
            }
            return true;
        }
    }

    return false;
}

bool SelectionOverlay::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left)
        return false;
    if (_selectedActionSlot == -1 || _selectedActionSlot >= _actionSlots.size())
        return false;

    ActionSlot &slot = _actionSlots[_selectedActionSlot];

    float frameX, frameY;
    getActionScreenCoords(_selectedActionSlot, frameX, frameY);

    float scale = layoutScale();
    if (event.x < frameX || event.y < frameY ||
        event.x >= frameX + kActionWidth * scale ||
        event.y >= frameY + kActionHeight * scale)
        return false;

    float actionY = event.y - frameY;
    if (actionY < kActionArrowHeight * scale) {
        cycleActionSlot(slot, true);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
        return true;
    }
    if (actionY >= (kActionArrowHeight + kActionWidth) * scale) {
        cycleActionSlot(slot, false);
        _game.requestAutoPause(AutoPauseReason::ActionMenu);
        _game.requestTutorialWindow(kActionMenuTutorial);
        return true;
    }

    std::shared_ptr<Creature> leader(_game.party().getLeader());
    if (!leader)
        return false;

    std::shared_ptr<Area> area(_game.module()->area());
    auto selectedObject = area->selectedObject();
    if (!selectedObject)
        return false;

    if (slot.indexSelected >= slot.actions.size())
        return false;

    ContextAction &ctxAction = slot.actions[slot.indexSelected];
    if (ctxAction.spell && !ctxAction.item) {
        ctxAction.availability = leader->powerMenuStatus(*ctxAction.spell, *selectedObject);
    }
    if (!ctxAction.availability.available()) {
        _feedback.reject(ctxAction.availability.reason, _services);
        return true;
    }

    // Every entry runs inside the menu's wrapper. Out of combat mode it first
    // drops the leader's pending round entries and, for a hostile creature or
    // a bash, posts the combat message. When the entry is done and combat
    // mode is still off, it enters combat mode for a hostile creature and
    // clears the leader's actions, before the entry's order arrives.
    const bool hostileCreature = _selectedHostile && selectedObject->type() == ObjectType::Creature;
    const bool bash = ctxAction.type == ActionType::AttackObject && selectedObject->type() != ObjectType::Creature;
    const bool inCombatMode = leader->clientCombatMode();
    if (!inCombatMode) {
        _game.combat().removeAllScheduled(*leader);
        if (hostileCreature || bash) {
            _game.party().setCombatMessage(*leader, _game.party().idleCombatMessage());
        }
    } else if (_game.isAutoPaused()) {
        // In combat mode an entry taken during an autopause shows its own
        // pause reason.
        _game.setPaused(true, PauseReason::CombatOrder);
    }
    _feedback.accept(_services);

    // The first time an action is taken, its tutorial window may take it
    // instead, and gives it back when dismissed.
    auto intercepted = [&](int tutorial, uint32_t param = 0) {
        return _game.requestTutorialWindow(tutorial, leader->id(), selectedObject->id(), param);
    };
    std::function<void()> order;
    // A leader that cannot be commanded takes no skill use or power; its
    // attacks are refused as they are taken up.
    auto queue = [&](std::shared_ptr<Action> action) {
        order = [leader, action]() {
            if (!leader->isCommandable()) {
                return;
            }
            action->setUserAction(true);
            leader->addAction(action);
        };
    };
    switch (ctxAction.type) {
    case ActionType::AttackObject:
        if (bash ? _game.prepareMenuBash(*leader, selectedObject) : _game.prepareMenuAttack(*leader, selectedObject)) {
            order = [this, leader, selectedObject]() { _game.sendAttack(*leader, selectedObject); };
        }
        break;
    case ActionType::UseFeat:
        if (intercepted(kCombatFeatTutorial, static_cast<uint32_t>(ctxAction.feat))) {
            break;
        }
        order = [this, leader, selectedObject, feat = ctxAction.feat]() { _game.sendAttack(*leader, selectedObject, feat); };
        break;
    case ActionType::UseSkill:
        if (ctxAction.item) {
            // Setting a mine on a door or placeable.
            if (!area->playerCanSetMines()) {
                _game.showMessagePopup(_services.resource.strings.getText(kMineLimitStrRef));
                return true;
            }
            // The first time, the tutorial window takes the action instead.
            if (_game.requestTutorialWindow(kSetMineTutorial, leader->id(), ctxAction.item->id())) {
                break;
            }
        }
        // Security on a door first clears the leader's orders, as the
        // player's controls clear them, and delays the next autopause.
        if (ctxAction.skill == SkillType::Security && selectedObject->type() == ObjectType::Door) {
            _game.combat().clearAllOrders(*leader);
            _game.delayNextEnemyAutoPause();
        }
        // Security and mine work clear the leader's actions unless the leader
        // is busy, then follow them.
        queue(_game.newAction<UseSkillAction>(ctxAction.skill, selectedObject, ctxAction.subSkill, ctxAction.item));
        order = [leader, queued = std::move(order)]() {
            leader->clearOrdersUnlessBusy();
            queued();
        };
        break;
    case ActionType::DoorSaber: {
        // The door saber first clears the leader's orders, as the player's
        // controls clear them, and is taken up only by a door still locked.
        _game.combat().clearAllOrders(*leader);
        auto door = std::static_pointer_cast<Door>(selectedObject);
        queue(_game.newAction<DoorSaberAction>(door));
        order = [door, queued = std::move(order)]() {
            if (door->isLocked()) {
                queued();
            }
        };
        break;
    }
    case ActionType::CastSpellAtObject: {
        if (ctxAction.equipmentPower) {
            // A droid's item power enters combat mode as a power does, then
            // goes out as the use of the item at the target.
            leader->setClientCombatMode(true);
            order = [this, leader, item = ctxAction.item, selectedObject]() {
                _game.useMenuItem(*leader, item, item->firstUseProperty(), selectedObject,
                                  selectedObject->position());
            };
            break;
        }
        if (ctxAction.item) {
            // Grenades, and TSL's rockets, have their own tutorial window.
            const int itemType = ctxAction.item->itemType();
            const bool grenade = itemType == Item::kGrenadeItemType ||
                                 (_game.isTSL() && itemType == Item::kRocketItemType);
            if (grenade && intercepted(kGrenadeTutorial, ctxAction.item->id())) {
                break;
            }
        } else {
            // A power from the target menu puts the leader into combat mode
            // whether or not it is cast. A caster without a Jedi class casts
            // nothing.
            leader->setClientCombatMode(true);
            if (!_game.canMenuCast(*leader, *ctxAction.spell)) {
                order = [this, leader]() { _game.refuseMenuCast(*leader); };
                break;
            }
            if (auto tutorial = _game.forcePowerTutorial(*ctxAction.spell);
                tutorial && intercepted(*tutorial, static_cast<uint32_t>(ctxAction.spell->type)))
                break;
        }
        // An item goes out as its use at the target, a power as its cast.
        if (auto item = ctxAction.item) {
            order = [this, leader, item, spell = ctxAction.spell, selectedObject]() {
                _game.useMenuItem(*leader, item, item->spellProperty(spell->type), selectedObject,
                                  menuItemLocation(*leader, *selectedObject));
            };
        } else {
            order = [this, leader, spell = ctxAction.spell, selectedObject]() {
                _game.sendMenuCast(*leader, spell, selectedObject);
            };
        }
        debug(str(boost::format("Spell selected: actor=%u spell=%d target=%u") % leader->id() % static_cast<int>(ctxAction.spell->type) % selectedObject->id()), LogChannel::Combat);
        break;
    }
    default:
        break;
    }
    if (!leader->clientCombatMode()) {
        if (!inCombatMode && hostileCreature) {
            leader->setClientCombatMode(true);
        }
        _game.combat().clearActions(*leader);
    }
    if (order) {
        order();
    }
    return true;
}

bool SelectionOverlay::handleMouseWheel(const input::MouseWheelEvent &event) {
    if (_selectedActionSlot == -1 || _selectedActionSlot >= _actionSlots.size())
        return false;

    ActionSlot &slot = _actionSlots[_selectedActionSlot];
    if (slot.actions.empty())
        return false;

    // A wheel notch over a slot scrolls it like its arrows.
    cycleActionSlot(slot, event.y > 0);
    _game.requestAutoPause(AutoPauseReason::ActionMenu);
    _game.requestTutorialWindow(kActionMenuTutorial);
    return true;
}

void SelectionOverlay::update(float dt) {
    _feedback.update(dt);
    // TODO: update on selection change only

    _hilightedObject.reset();
    _hilightedHostile = false;

    _selectedObject.reset();
    _selectedHostile = false;

    std::shared_ptr<Module> module(_game.module());
    std::shared_ptr<Area> area(module->area());

    auto camera = _game.getActiveCamera();
    glm::mat4 projection(camera->cameraSceneNode()->camera()->projection());
    glm::mat4 view(camera->cameraSceneNode()->camera()->view());

    // The hover reticle is sized by the distance from the leader, and hides
    // near the edges of the arrow margin.
    auto hilightedObject = area->hilightedObject();
    if (hilightedObject) {
        const auto leader = _game.party().getLeader();
        _hilightedScreenCoords = area->getSelectableScreenCoords(hilightedObject, projection, view);
        const GraphicsOptions &opts = _game.options().graphics;
        const float x = opts.width * _hilightedScreenCoords.x;
        const float y = opts.height * (1.0f - _hilightedScreenCoords.y);
        const float edge = kHoverReticleEdge * layoutScale();
        const auto &margin = _arrowMargin->extent();
        const bool inside = x >= margin.left + edge && x <= margin.left + margin.width - edge &&
                            y >= margin.top + edge && y <= margin.top + margin.height - edge;

        if (_hilightedScreenCoords.z < 1.0f && inside) {
            _hilightedObject = hilightedObject;
            _hilightedReticleSize = hoverReticleSize(
                hilightedObject->type() == ObjectType::Creature, _game.isTSL(),
                glm::distance2(leader->position(), hilightedObject->position()));
            _hilightedHostile = isHoverHostile(*hilightedObject, *leader, *module, _game);
        }
    }

    auto selectedObject = area->selectedObject();
    if (selectedObject) {
        _selectedScreenCoords = area->getSelectableScreenCoords(selectedObject, projection, view);

        if (_selectedScreenCoords.z < 1.0f) {
            _selectedObject = selectedObject;

            for (int i = 0; i < kNumActionSlots; ++i) {
                _actionSlots[i].actions.clear();
            }
            std::vector<ContextAction> actions(module->getContextActions(selectedObject));
            _hasActions = !actions.empty();
            if (_hasActions) {
                for (auto &action : actions) {
                    switch (action.type) {
                    case ActionType::AttackObject:
                    case ActionType::UseFeat:
                    case ActionType::DoorSaber:
                        _actionSlots[0].actions.push_back(action);
                        break;
                    case ActionType::UseSkill:
                        if (action.item) {
                            _actionSlots[2].actions.push_back(action);
                        } else if (action.skill == SkillType::Demolitions && action.subSkill == 0) {
                            _actionSlots[0].actions.push_back(action);
                        } else {
                            _actionSlots[1].actions.push_back(action);
                        }
                        break;
                    case ActionType::CastSpellAtObject: {
                        // Items other than a droid's item powers, and powers
                        // against a mine, take the third column.
                        if ((action.item && !action.equipmentPower) || selectedObject->type() == ObjectType::Trigger) {
                            _actionSlots[2].actions.push_back(action);
                        } else {
                            _actionSlots[1].actions.push_back(action);
                        }
                    }
                    default:
                        break;
                    }
                }
            }
            for (int i = 0; i < kNumActionSlots; ++i) {
                if (_actionSlots[i].indexSelected >= _actionSlots[i].actions.size()) {
                    _actionSlots[i].indexSelected = 0;
                }
            }

            auto selectedCreature = std::dynamic_pointer_cast<Creature>(selectedObject);
            if (selectedCreature) {
                _selectedHostile = module->isHostileToPartyLeader(*selectedCreature);
            }
        }
    }
}

void SelectionOverlay::render() {
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this]() {
        const float scale = layoutScale();
        if (_hilightedObject) {
            const float size = _hilightedReticleSize * scale;
            renderReticle(_hilightedHostile ? *_hostileReticle : *_friendlyReticle, _hilightedScreenCoords,
                          size, size, kHoverReticleAlpha);
        }
        if (_selectedObject) {
            auto &texture = _selectedHostile ? *_hostileReticle2 : *_friendlyReticle2;
            renderReticle(texture, _selectedScreenCoords, texture.width() * scale, texture.height() * scale, 1.0f);
            renderActionBar();
            renderTitleBar();
            renderHealthBar();
        }
    });
}

void SelectionOverlay::renderReticle(Texture &texture, const glm::vec3 &screenCoords, float width, float height, float alpha) {
    _services.graphics.context.bindTexture(texture);

    const GraphicsOptions &opts = _game.options().graphics;

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3((opts.width * screenCoords.x) - width / 2, (opts.height * (1.0f - screenCoords.y)) - height / 2, 0.0f));
    transform = glm::scale(transform, glm::vec3(width, height, 1.0f));

    _services.graphics.uniforms.setLocals([transform, alpha](auto &locals) {
        locals.reset();
        locals.model = transform;
        locals.color.a = alpha;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void SelectionOverlay::renderTitleBar() {
    // Object-local feedback has priority over the menu error. The error is
    // UI-owned: changing it must not alter the object's script feedback text.
    auto title = !_selectedObject->feedbackText().empty()
                     ? _selectedObject->feedbackText()
                 : _feedback.active() ? _feedback.text(_game)
                                      : _selectedObject->name();
    // TSL shows the title with its actions hidden.
    if (_game.isTSL()) {
        title = _game.substituteLogTokens(std::move(title));
    }
    if (title.empty()) {
        return;
    }

    const GraphicsOptions &opts = _game.options().graphics;
    float scale = layoutScale();
    float titleScale = scale * kObjectTitleScale;
    float fontScale = titleScale * opts.guiTextScale * (_game.isTSL() ? 1.0f : kK1CombatTextScale);
    float barWidth = kTitleBarWidth * titleScale;
    float barHeight = _font->height() * fontScale + kTitleBarPadding * titleScale;
    float reticleHeight = _reticleHeight * scale;
    float offsetToReticle = kOffsetToReticle * scale;
    float healthBarHeight = kHealthBarHeight * titleScale;
    float actionHeight = kActionHeight * scale;
    float actionMargin = kActionBarMargin * scale;
    {
        float x = opts.width * _selectedScreenCoords.x - barWidth / 2;
        float y = opts.height * (1.0f - _selectedScreenCoords.y) - reticleHeight / 2.0f - barHeight - offsetToReticle - healthBarHeight - scale;

        if (_hasActions) {
            y -= actionHeight + 2 * actionMargin;
        }
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
        transform = glm::scale(transform, glm::vec3(barWidth, barHeight, 1.0f));

        _services.graphics.uniforms.setLocals([this, transform](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
            locals.color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            locals.color.a = 0.5f;
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
        _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
    }
    {
        float x = opts.width * _selectedScreenCoords.x;
        float y = opts.height * (1.0f - _selectedScreenCoords.y) - (reticleHeight + barHeight) / 2 - offsetToReticle - healthBarHeight - scale;
        if (_hasActions) {
            y -= actionHeight + 2 * actionMargin;
        }
        glm::vec3 position(x, y, 0.0f);
        _font->render(title, position, getColorFromSelectedObject(), TextGravity::CenterCenter, fontScale);
    }
}

void SelectionOverlay::renderHealthBar() {
    const GraphicsOptions &opts = _game.options().graphics;
    float scale = layoutScale();
    float titleScale = scale * kObjectTitleScale;
    float barWidth = kTitleBarWidth * titleScale;
    float healthBarHeight = kHealthBarHeight * titleScale;
    float x = opts.width * _selectedScreenCoords.x - barWidth / 2;
    float y = opts.height * (1.0f - _selectedScreenCoords.y) - _reticleHeight * scale / 2.0f - healthBarHeight - kOffsetToReticle * scale;
    float w = glm::clamp(_selectedObject->currentHitPoints() / static_cast<float>(_selectedObject->maxHitPoints()), 0.0f, 1.0f) * barWidth;

    if (_hasActions) {
        y -= (kActionHeight + 2 * kActionBarMargin) * scale;
    }
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
    transform = glm::scale(transform, glm::vec3(w, healthBarHeight, 1.0f));

    _services.graphics.uniforms.setLocals([this, transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
        locals.color = glm::vec4(getColorFromSelectedObject(), 1.0f);
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void SelectionOverlay::renderActionBar() {
    if (!_hasActions)
        return;

    for (int i = 0; i < kNumActionSlots; ++i) {
        renderActionFrame(i);
        renderActionArrows(i);
        renderActionIcon(i);
    }
}

void SelectionOverlay::renderActionFrame(int index) {
    std::shared_ptr<Texture> frameTexture;
    if (index == _selectedActionSlot) {
        frameTexture = _hilightedScroll;
    } else if (_selectedHostile) {
        frameTexture = _hostileScroll;
    } else {
        frameTexture = _friendlyScroll;
    }
    _services.graphics.context.bindTexture(*frameTexture);

    float frameX, frameY;
    getActionScreenCoords(index, frameX, frameY);
    float scale = layoutScale();

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(frameX, frameY, 0.0f));
    transform = glm::scale(transform, glm::vec3(kActionWidth * scale, kActionHeight * scale, 1.0f));

    _services.graphics.uniforms.setLocals([this, transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void SelectionOverlay::renderActionArrows(int index) {
    if (_actionSlots[index].actions.size() < 2)
        return;

    renderActionArrow(index, true);
    renderActionArrow(index, false);
}

void SelectionOverlay::renderActionArrow(int index, bool previous) {
    bool hilighted = index == _selectedActionSlot &&
                     _hilightedActionBand == (previous ? ActionBand::Previous : ActionBand::Next);
    auto texture = hilighted ? _hilightedActionArrow : _actionArrow;
    _services.graphics.context.bindTexture(*texture);

    float frameX, frameY;
    getActionScreenCoords(index, frameX, frameY);
    float scale = layoutScale();

    glm::mat4 transform(1.0f);
    transform = glm::translate(
        transform,
        glm::vec3(frameX, previous ? frameY : frameY + (kActionArrowHeight + kActionWidth) * scale, 0.0f));
    transform = glm::scale(transform, glm::vec3(kActionWidth * scale, kActionArrowHeight * scale, 1.0f));

    _services.graphics.uniforms.setLocals([transform, previous](auto &locals) {
        locals.reset();
        locals.model = transform;
        if (!previous) {
            locals.uv = glm::mat3x4(
                glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        }
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

bool SelectionOverlay::getActionScreenCoords(int index, float &x, float &y) const {
    if (!_selectedObject)
        return false;

    const GraphicsOptions &opts = _game.options().graphics;
    float scale = layoutScale();
    x = opts.width * _selectedScreenCoords.x + ((static_cast<float>(index - 1) - 0.5f) * kActionWidth + (index - 1) * kActionBarMargin) * scale;
    y = opts.height * (1.0f - _selectedScreenCoords.y) - (_reticleHeight / 2.0f + kActionHeight + kOffsetToReticle + kActionBarMargin) * scale;

    return true;
}

void SelectionOverlay::renderActionIcon(int index) {
    const ActionSlot &slot = _actionSlots[index];
    if (slot.indexSelected >= slot.actions.size())
        return;

    const ContextAction &action = slot.actions[slot.indexSelected];

    float frameX, frameY;
    getActionScreenCoords(index, frameX, frameY);

    const GraphicsOptions &opts = _game.options().graphics;
    float scale = layoutScale();
    float y = opts.height * (1.0f - _selectedScreenCoords.y) - ((_reticleHeight + kActionHeight + kActionWidth) / 2.0f + kOffsetToReticle + kActionBarMargin) * scale;

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(frameX, y, 0.0f));
    transform = glm::scale(transform, glm::vec3(kActionWidth * scale, kActionWidth * scale, 1.0f));

    renderContextActionIcon(action, transform, _services);
}

float SelectionOverlay::layoutScale() const {
    const auto &opts = _game.options().graphics;
    return std::min(opts.width / 800.0f, opts.height / 600.0f) * opts.guiScale;
}

glm::vec3 SelectionOverlay::getColorFromSelectedObject() const {
    static glm::vec3 red(1.0f, 0.0f, 0.0f);

    auto guiColorBase = _game.isTSL() ? kTSLGUIColorBase : kGUIColorBase;

    return (_selectedObject && _selectedHostile) ? red : guiColorBase;
}

} // namespace game

} // namespace reone
