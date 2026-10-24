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

#include "reone/game/gui/hud.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

#include "reone/audio/mixer.h"
#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/combat.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spell.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/visual.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"
#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/label.h"
#include "reone/gui/guis.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"
#include "reone/system/smallvector.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

/**
 * TSL enlarges parts of its interface by half for each full 1024 pixels of
 * screen width past 1022.
 */
static float interfaceScalar(bool tsl, int screenWidth) {
    return tsl && screenWidth > 1022 ? 1.0f + ((screenWidth - 1022) / 1024) * 0.5f : 1.0f;
}

// Effect stacks
//
// Each portrait shows one icon for every beneficial effect on its member and
// one for every harmful effect, stacked over the portrait. The rules below lay
// the icons out to the pixel as the game does.

/** The leader's portrait stacks at most nine icons of a kind; another member's at most five. */
static constexpr int kLeaderEffectStackMax = 9;
static constexpr int kMemberEffectStackMax = 5;
/** A stack of up to four icons is centred on the portrait's effect icon. */
static constexpr int kCentredEffectStackMax = 4;
/** A longer harmful stack starts this far above the bottom of the portrait. */
static constexpr int kLeaderEffectStackBottomMargin = 16;
static constexpr int kMemberEffectStackBottomMargin = 8;

/** The distance between the icons of a stack, before scaling. */
static int effectStackSpacing(int count, bool leader) {
    if (!leader) {
        return 5;
    }
    switch (count) {
    case 6:
        return 8;
    case 7:
        return 7;
    case 8:
        return 6;
    case 9:
        return 5;
    default:
        return 10;
    }
}

/** In a stack of seven or eight icons, the gaps after the first and the third icon are a pixel narrower. */
static bool isEffectStackGapNarrowed(int count, int index) {
    return (count == 7 || count == 8) && (index == 0 || index == 2);
}

/**
 * Draw \p label once for each icon of a stack of \p effects, clipped to the
 * portrait. Beneficial icons are drawn from the top down, harmful ones from the
 * bottom up.
 */
static void renderEffectStack(
    Label &label,
    const Control::Extent &portrait,
    int effects,
    bool leader,
    bool beneficial,
    float scalar,
    const glm::ivec2 &screenSize,
    const glm::ivec2 &controlOffset,
    scene::IRenderPass &pass,
    graphics::IContext &context) {

    const int count = std::min(effects, leader ? kLeaderEffectStackMax : kMemberEffectStackMax);
    if (count == 0) {
        return;
    }
    const int spacing = static_cast<int>(effectStackSpacing(count, leader) * scalar);
    const int direction = beneficial ? 1 : -1;
    int top;
    if (count <= kCentredEffectStackMax) {
        top = label.extent().top - direction * (count - 1) * (spacing / 2);
    } else if (beneficial) {
        top = portrait.top + 1;
    } else {
        const int bottomMargin = leader ? kLeaderEffectStackBottomMargin : kMemberEffectStackBottomMargin;
        top = portrait.top + portrait.height - bottomMargin - 1;
    }

    glm::ivec4 scissorBounds {
        controlOffset.x + portrait.left,
        screenSize.y - (controlOffset.y + portrait.top + portrait.height),
        portrait.width,
        portrait.height};

    bool wasVisible = label.isVisible();
    label.setVisible(true);
    context.withScissorTestNoClear(scissorBounds, [&]() {
        for (int index = 0; index < count; ++index) {
            glm::ivec2 offset = controlOffset;
            offset.y += top - label.extent().top;
            label.render(screenSize, offset, pass);
            top += direction * spacing;
            if (isEffectStackGapNarrowed(count, index)) {
                top -= direction;
            }
        }
    });
    label.setVisible(wasVisible);
}

// END Effect stacks

// Pause indicator

/** Pause indicator text for a pause reason; `press` shows the unpause prompt. */
struct PauseReasonText {
    int k1StrRef;
    int k2StrRef;
    bool press;
};

/** The indicator's text for \p reason; the reasons it leaves out show no indicator. */
static std::optional<PauseReasonText> pauseReasonText(PauseReason reason) {
    switch (reason) {
    case PauseReason::Player:
        return PauseReasonText {1508, 1508, true};
    case PauseReason::EnemySighted:
        return PauseReasonText {48212, 48732, false};
    case PauseReason::EndOfCombatRound:
        return PauseReasonText {42432, 42432, true};
    case PauseReason::ActionMenu:
        return PauseReasonText {42482, 42482, true};
    case PauseReason::NewTargetSelected:
        return PauseReasonText {42481, 42481, true};
    case PauseReason::PartyKilled:
        return PauseReasonText {42397, 42397, true};
    case PauseReason::MineSighted:
        return PauseReasonText {49118, 48735, false};
    case PauseReason::CombatOrder:
        return PauseReasonText {48423, 48734, true};
    default:
        return std::nullopt;
    }
}

/** TSL's unpause prompt under the reason. */
static constexpr int kK2PausePressStrRef = 136311;
/** The prompt sits this far below the reason, and the panel ends this far below its last line, before scaling. */
static constexpr int kPausePromptGap = 2;
static constexpr int kPausePanelBottomPadding = 5;
/** The panel's distance below the menu bar and from the right edge of the screen: KotOR's, and TSL's before scaling. */
static constexpr int kK1PausePanelMargin = 3;
static constexpr float kK2PausePanelTop = 11.0f;
static constexpr float kK2PausePanelRight = 6.0f;

// END Pause indicator

static std::string g_attackIcon("i_attack");

/** The action queue's icons for round entries that show no command of their own. */
static const std::string kEquipIcon {"i_equip"};
static const std::string kUnequipIcon {"i_unequip"};
static const std::string kDefensiveStanceIcon {"i_stancedef"};
static const std::string kSwitchWeaponsIcon {"i_swtchwpn"};
/** The number of icons in the action queue. */
static constexpr size_t kActionQueueSize = 4;
/** The alpha of the queue's first icon while no round action is under way. */
static constexpr float kIdleActionQueueOpacity = 0.25f;

/**
 * A cast shows its spell's icon. The use of an item shows the item's icon
 * while the item is still in the game, and nothing once it is gone.
 */
static std::shared_ptr<graphics::Texture> castIcon(const Spell &spell, const std::optional<std::shared_ptr<Item>> &item) {
    if (!item) {
        return spell.icon;
    }
    return *item && (*item)->isRuntimeLive() ? (*item)->icon() : nullptr;
}

/** Authored-canvas gap between the minimap frame and the TSL bark bubble. */
static constexpr int kBarkBubbleMapGap = 8;

/**
 * TSL's box of the action bar's announcements and prompts: its place on the
 * authored canvas, its frame in the interface's green, its fill tinted like
 * the rest of the HUD, and its text in the highlight colour.
 */
static constexpr char kNoticeBoxTag[] = "LBL_ACTIONNOTICE";
static const gui::Control::Extent kNoticeBoxExtent {4, 398, 252, 96};
static constexpr char kNoticeBoxCorner[] = "uibit_brdr_16bct";
static constexpr char kNoticeBoxEdge[] = "uibit_brdr_16bet";
static constexpr char kNoticeBoxFill[] = "uibit_fill_2bt";
static constexpr int kNoticeBoxBorderDimension = 16;
static const glm::vec3 kNoticeBoxBorderColor {0.05f, 0.35f, 0.27f};
static const glm::vec3 kNoticeBoxTextColor {0.7f, 0.7f, 0.6f};
static constexpr char kNoticeBoxFont[] = "dialogfont16x16";

/** The combat line shown while the leader is held helpless. */
static constexpr int kLeaderDebilitatedCombatMessage = 47915;
/** The visuals the player character shows on a shift to the dark or light side (TSL). */
static constexpr int kDarkSideVisual = 9014;
static constexpr int kLightSideVisual = 9015;

static void tintHUDMenuButton(const std::shared_ptr<Button> &button, const glm::vec3 &baseColor) {
    if (!button) {
        return;
    }
    button->setBorderColor(baseColor);
    button->setTintBorderFill(true);
}

void HUD::preload(IGUI &gui) {
    gui.setResolution(800, 600);
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);

    static constexpr const char *kCentredCombatControls[] = {
        "BTN_CLEARALL", "BTN_CLEARONE", "BTN_CLEARONE2",
        "LBL_CMBTMODEMSG", "LBL_CMBTMSGBG", "LBL_COMBATBG1", "LBL_COMBATBG2", "LBL_COMBATBG3",
        "LBL_QUEUE0", "LBL_QUEUE1", "LBL_QUEUE2", "LBL_QUEUE3"};
    for (auto tag : kCentredCombatControls) {
        gui.setControlScaling(tag, GUI::ScalingMode::ScaledRelativeToCenter);
    }
}

void HUD::onGUILoaded() {
    bindControls();

    if (!_game.isTSL()) {
        // The GUI control tree outlives this wrapper in the GUI cache. Derive
        // from the live layout inputs so rebuilding a HUD cannot feed the
        // previous presentation scale back into itself.
        const float combatTextScale = _gui->scale() * _gui->textScale() * kK1CombatTextScale;
        for (Control *control : {
                 static_cast<Control *>(_controls.LBL_CMBTMODEMSG.get()),
                 static_cast<Control *>(_controls.BTN_CLEARALL.get()),
                 static_cast<Control *>(_controls.BTN_CLEARONE.get()),
                 static_cast<Control *>(_controls.BTN_CLEARONE2.get())}) {
            if (control) {
                control->setScale(combatTextScale);
            }
        }
        if (auto actionDescription = findControl<gui::Label>("LBL_ACTIONDESC")) {
            actionDescription->setScale(combatTextScale);
        }
    }

    // The menu strip and the action queue are icon artwork, magnified on any
    // modern screen, so they get their alpha edge reconstructed.
    for (auto &icon : {_controls.BTN_EQU, _controls.BTN_INV, _controls.BTN_CHAR,
                       _controls.BTN_ABI, _controls.BTN_MSG, _controls.BTN_JOU,
                       _controls.BTN_MAP, _controls.BTN_OPT}) {
        icon->setSharpenBorderFillAlpha(true);
    }
    for (auto &icon : {_controls.LBL_QUEUE0, _controls.LBL_QUEUE1,
                       _controls.LBL_QUEUE2, _controls.LBL_QUEUE3}) {
        icon->setSharpenBorderFillAlpha(true);
    }

    _actionBar.addDescription(
        findControl<gui::Label>("LBL_ACTIONDESC"),
        findControl<gui::Label>("LBL_ACTIONDESCBG"));
    if (_game.isTSL()) {
        loadNoticeBox();
    }

    // TSL adds a sixth slot, the forms.
    const int actionSlots = _game.isTSL() ? 6 : 5;
    for (int i = 0; i < actionSlots; ++i) {
        auto action = findControl<gui::Button>(str(boost::format("BTN_ACTION%d") % i));
        auto icon = findControl<gui::Button>(str(boost::format("LBL_ACTION%d") % i));
        auto up = findControl<gui::Button>(str(boost::format("BTN_ACTIONUP%d") % i));
        auto down = findControl<gui::Button>(str(boost::format("BTN_ACTIONDOWN%d") % i));

        if (action && icon && up && down) {
            _actionBar.addSlot(action, icon, up, down);
        }
    }

    _controls.BTN_CLEARALL->setVisible(false);
    _controls.BTN_TARGET0->setVisible(false);
    _controls.BTN_TARGET1->setVisible(false);
    _controls.BTN_TARGET2->setVisible(false);
    _controls.BTN_TARGETDOWN0->setVisible(false);
    _controls.BTN_TARGETDOWN1->setVisible(false);
    _controls.BTN_TARGETDOWN2->setVisible(false);
    _controls.BTN_TARGETUP0->setVisible(false);
    _controls.BTN_TARGETUP1->setVisible(false);
    _controls.BTN_TARGETUP2->setVisible(false);
    _controls.LBL_ARROW_MARGIN->setVisible(false);
    _controls.LBL_CASH->setVisible(false);
    _controls.LBL_CMBTEFCTINC1->setVisible(false);
    _controls.LBL_CMBTEFCTINC2->setVisible(false);
    _controls.LBL_CMBTEFCTINC3->setVisible(false);
    _controls.LBL_CMBTEFCTRED1->setVisible(false);
    _controls.LBL_CMBTEFCTRED2->setVisible(false);
    _controls.LBL_CMBTEFCTRED3->setVisible(false);
    _controls.LBL_CMBTMODEMSG->setVisible(false);
    _controls.LBL_CMBTMSGBG->setVisible(false);
    _controls.LBL_COMBATBG3->setVisible(false);
    _controls.LBL_DARKSHIFT->setVisible(false);
    _controls.LBL_DEBILATATED1->setVisible(false);
    _controls.LBL_DEBILATATED2->setVisible(false);
    _controls.LBL_DEBILATATED3->setVisible(false);
    _controls.LBL_DISABLE1->setVisible(false);
    _controls.LBL_DISABLE2->setVisible(false);
    _controls.LBL_DISABLE3->setVisible(false);
    _controls.LBL_JOURNAL->setVisible(false);
    _controls.LBL_HEALTHBG->setVisible(false);
    _controls.LBL_ITEMRCVD->setVisible(false);
    _controls.LBL_ITEMLOST->setVisible(false);
    _controls.LBL_LIGHTSHIFT->setVisible(false);
    _controls.LBL_MAP->setVisible(false);
    _controls.LBL_MOULDING1->setVisible(false);
    _controls.LBL_MOULDING3->setVisible(false);
    _controls.LBL_NAME->setVisible(false);
    _controls.LBL_NAMEBG->setVisible(false);
    _controls.LBL_PLOTXP->setVisible(false);
    _controls.LBL_STEALTHXP->setVisible(false);
    _controls.LBL_ARROW->setVisible(false);
    _controls.BTN_TARGET0->setVisible(false);

    if (_game.isTSL()) {
        // The weapon swap queues through the leader's combat round.
        _controls.BTN_SWAPWEAPONS->setOnClick([this]() {
            if (auto leader = _game.party().getLeader()) {
                leader->requestSwitchWeapons(false);
            }
        });
        tintHUDMenuButton(_controls.BTN_EQU, _baseColor);
        tintHUDMenuButton(_controls.BTN_INV, _baseColor);
        tintHUDMenuButton(_controls.BTN_CHAR, _baseColor);
        tintHUDMenuButton(_controls.BTN_ABI, _baseColor);
        tintHUDMenuButton(_controls.BTN_MSG, _baseColor);
        tintHUDMenuButton(_controls.BTN_JOU, _baseColor);
        tintHUDMenuButton(_controls.BTN_MAP, _baseColor);
        tintHUDMenuButton(_controls.BTN_OPT, _baseColor);
        // The bar backings are colour masks like the rest of the TSL HUD.
        for (auto &bar : {_controls.PB_VIT1, _controls.PB_VIT2, _controls.PB_VIT3,
                          _controls.PB_FORCE1, _controls.PB_FORCE2, _controls.PB_FORCE3}) {
            bar->setTintBorderFill(true);
        }
    } else {
        _controls.LBL_COMBATBG1->setVisible(false);
        _controls.LBL_COMBATBG2->setVisible(false);
        _controls.LBL_MOULDING2->setVisible(false);
    }

    _controls.BTN_EQU->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Equipment);
    });
    _controls.BTN_INV->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Inventory);
    });
    _controls.BTN_CHAR->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Character);
    });
    _controls.BTN_ABI->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Abilities);
    });
    _controls.BTN_MSG->setOnClick([this]() {
        if (_game.isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Party);
        } else {
            _game.openInGameMenu(InGameMenuTab::Messages);
        }
    });
    _controls.BTN_JOU->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Journal);
    });
    _controls.BTN_MAP->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Map);
    });
    _controls.BTN_OPT->setOnClick([this]() {
        _game.openInGameMenu(InGameMenuTab::Options);
    });
    // The pause toggle stays as authored; its state follows play every frame.
    _controls.TB_PAUSE->setOnClick([this]() {
        _game.pressPauseButton();
    });
    _controls.TB_SOLO->setOnClick([this]() {
        _game.showSoloModeQuery(false);
    });
    _controls.TB_STEALTH->setOnClick([this]() {
        _game.requestStealth();
    });
    // Clear All leaves combat mode and clears the leader's actions, which one
    // that cannot be commanded keeps, its pending round entries and its targets.
    // The button shows only while the leader is in combat mode.
    // It also takes the rejection message off the action bar.
    _controls.BTN_CLEARALL->setOnClick([this]() {
        const auto leader = _game.party().getLeader();
        leader->setClientCombatMode(false);
        _game.combat().clearAllOrders(*leader);
        _actionBar.clearRejection();
    });
    // Both clear-one buttons clear one action.
    _controls.BTN_CLEARONE->setOnClick([this]() { _game.clearOneAction(); });
    _controls.BTN_CLEARONE2->setOnClick([this]() { _game.clearOneAction(); });
    // While the action bar waits for the party member to use an item on, a
    // portrait picks its member, and pointing at one names it.
    const std::array<std::shared_ptr<gui::Button>, 3> portraitButtons {
        _controls.BTN_CHAR1, _controls.BTN_CHAR2, _controls.BTN_CHAR3};
    for (int portrait = 0; portrait < 3; ++portrait) {
        portraitButtons[portrait]->setOnSelectionChanged([this, portrait](bool selected) {
            _actionBar.pointAtPartyItemTarget(selected ? portraitMemberIndex(portrait) : std::nullopt);
        });
    }
    _controls.BTN_CHAR1->setOnClick([this]() {
        if (_actionBar.isChoosingPartyItemTarget()) {
            _actionBar.usePartyItemOn(0);
            return;
        }
        // The controlled character's portrait opens the character sheet
        // while a level-up is due, the equipment otherwise.
        const bool levelUp = _game.party().getLeader()->isLevelUpPending();
        _game.openInGameMenu(levelUp ? InGameMenuTab::Character : InGameMenuTab::Equipment);
    });
    // Otherwise a member who is down cannot take control.
    auto selectMember = [this](int portrait) {
        auto index = portraitMemberIndex(portrait);
        if (!index) {
            return;
        }
        if (_actionBar.isChoosingPartyItemTarget()) {
            _actionBar.usePartyItemOn(*index);
            return;
        }
        auto member = _game.party().getMember(*index);
        if (member->isDead() || member->isTemporarilyDead()) {
            return;
        }
        _game.party().setPartyLeaderByIndex(*index);
    };
    _controls.BTN_CHAR2->setOnClick([selectMember]() { selectMember(1); });
    _controls.BTN_CHAR3->setOnClick([selectMember]() { selectMember(2); });

    _select.init(_controls.LBL_ARROW_MARGIN);

    _barkBubble = std::make_unique<BarkBubble>(_game, _services);
    if (_game.isTSL()) {
        // TSL authors the bark bubble over the minimap frame. Keep it below;
        // both GUIs share the 800x600 authored canvas.
        const auto &mapBorder = _controls.LBL_MAPBORDER->authoredExtent();
        _barkBubble->setAuthoredTop(mapBorder.top + mapBorder.height + kBarkBubbleMapGap);
    }
    _barkBubble->init();

    _statusSummary = std::make_unique<StatusSummary>(_game, _services, _game.statusSummary());
    _statusSummary->init();
    _statusSummary->setOnStatusFlash([this](StatusSummaryCategory category) { flashStatus(category); });

    _areaTransition = std::make_unique<AreaTransition>(_game, _services);
    _areaTransition->init();

    loadPausePanel();
}

void HUD::loadNoticeBox() {
    auto notice = std::static_pointer_cast<gui::Button>(std::shared_ptr<Control>(_gui->newControl(ControlType::Button, kNoticeBoxTag)));
    notice->setAuthoredExtent(kNoticeBoxExtent);
    gui::Control::Border border;
    border.corner = _services.resource.textures.get(kNoticeBoxCorner, TextureUsage::GUI);
    border.edge = _services.resource.textures.get(kNoticeBoxEdge, TextureUsage::GUI);
    border.fill = _services.resource.textures.get(kNoticeBoxFill, TextureUsage::GUI);
    border.color = kNoticeBoxBorderColor;
    border.dimension = kNoticeBoxBorderDimension;
    notice->setBorder(border);
    notice->setHilight(border);
    notice->setTintBorderFill(true);
    notice->setTextFont(_services.resource.fonts.get(kNoticeBoxFont));
    notice->setTextAlignment(Control::TextAlign::LeftTop);
    notice->setTextColor(kNoticeBoxTextColor);
    _actionBar.addNotice(notice);
    _gui->addControlToFront(notice, IGUI::ControlCoordinates::Authored);
}

std::optional<int> HUD::portraitMemberIndex(int portrait) const {
    const Party &party = _game.party();
    int index = portrait;
    if (party.getSize() == 2 && portrait > 0) {
        if (portrait == 1) {
            return std::nullopt;
        }
        index = 1;
    }
    if (!party.getMember(index)) {
        return std::nullopt;
    }
    return index;
}

void HUD::loadPausePanel() {
    _pauseGUI = _services.gui.guis.get(guiResRef("pause"), [this](IGUI &gui) {
        GameGUI::preload(gui);
        gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
    });
    if (!_pauseGUI) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef("pause"));
    }
    _pauseGUI->setEventListener(*this);
    auto find = [this](const std::string &tag) { return _pauseGUI->findControl(tag); };
    _pauseControls.BTN_UNPAUSE = std::static_pointer_cast<Button>(find("BTN_UNPAUSE"));
    _pauseControls.LBL_PAUSEREASON = std::static_pointer_cast<Label>(find("LBL_PAUSEREASON"));
    _pauseControls.LBL_PRESS = std::static_pointer_cast<Label>(find("LBL_PRESS"));
    // The prompt's authored text takes custom tokens.
    _pauseControls.LBL_PRESS->setTextMessage(_game.substituteCustomTokens(_pauseControls.LBL_PRESS->text().text));
    // A click anywhere on the indicator resumes play.
    _pauseControls.BTN_UNPAUSE->setOnClick([this]() {
        _game.togglePlayerPause();
    });
    _pauseLayoutReason.reset();
}

bool HUD::isPausePanelShown() const {
    return _game.isPaused() && pauseReasonText(_game.pauseReason()).has_value();
}

void HUD::updatePausePanel(float dt) {
    if (!isPausePanelShown()) {
        if (_pauseLayoutReason) {
            _pauseGUI->clearSelection();
            _pauseLayoutReason.reset();
        }
        return;
    }
    const PauseReason reason = _game.pauseReason();
    if (_pauseLayoutReason != reason) {
        layoutPausePanel(reason);
    }
    _pauseGUI->update(dt);
}

void HUD::layoutPausePanel(PauseReason reason) {
    _pauseLayoutReason = reason;
    const auto text = *pauseReasonText(reason);
    const bool tsl = _game.isTSL();
    auto &root = _pauseGUI->rootControl();
    auto &reasonLabel = *_pauseControls.LBL_PAUSEREASON;
    auto &pressLabel = *_pauseControls.LBL_PRESS;
    auto &unpause = *_pauseControls.BTN_UNPAUSE;

    const float scale = _pauseGUI->scale();
    auto scaled = [scale](const Control::Extent &authored) {
        return Control::Extent {
            static_cast<int>(std::lround(authored.left * scale)),
            static_cast<int>(std::lround(authored.top * scale)),
            static_cast<int>(std::lround(authored.width * scale)),
            static_cast<int>(std::lround(authored.height * scale))};
    };
    auto textHeight = [](const Label &label) {
        if (!label.text().font) {
            return 0;
        }
        const int lineHeight = std::max(1, static_cast<int>(std::lround(label.text().font->height() * label.scale())));
        return std::max(1, static_cast<int>(label.textLines().size())) * lineHeight;
    };
    for (Control *control : {static_cast<Control *>(&root), static_cast<Control *>(&reasonLabel),
                             static_cast<Control *>(&pressLabel), static_cast<Control *>(&unpause)}) {
        control->setPresentationScale(scale);
    }

    // Panel-relative layout.
    Control::Extent reasonExtent = scaled(reasonLabel.authoredExtent());
    reasonLabel.setExtent(reasonExtent);
    // TSL resolves the reason's tokens a second time.
    std::string reasonText = _game.getInterfaceText(tsl ? text.k2StrRef : text.k1StrRef);
    if (tsl) {
        reasonText = _game.substituteCustomTokens(std::move(reasonText));
    }
    reasonLabel.setTextMessage(reasonText);
    reasonExtent.height = textHeight(reasonLabel);
    Control::Extent last = reasonExtent;
    Control::Extent pressExtent = scaled(pressLabel.authoredExtent());
    pressLabel.setVisible(text.press);
    if (text.press) {
        pressLabel.setExtent(pressExtent);
        if (tsl) {
            pressLabel.setTextMessage(_game.getInterfaceText(kK2PausePressStrRef));
        }
        pressExtent.top = reasonExtent.top + reasonExtent.height + static_cast<int>(std::lround(kPausePromptGap * scale));
        pressExtent.height = textHeight(pressLabel);
        last = pressExtent;
    }
    Control::Extent panelExtent = scaled(root.authoredExtent());
    panelExtent.height = last.top + last.height + static_cast<int>(std::lround(kPausePanelBottomPadding * scale));
    Control::Extent unpauseExtent {reasonExtent.left, reasonExtent.top, reasonExtent.width, panelExtent.height};

    // Top-right, just under the menu bar.
    const int screenWidth = _game.options().graphics.width;
    const auto &menuButton = _controls.BTN_EQU->extent();
    panelExtent.top = menuButton.top + menuButton.height - 1;
    if (tsl) {
        const float scalar = interfaceScalar(true, screenWidth);
        panelExtent.top += static_cast<int>(kK2PausePanelTop * scalar);
        panelExtent.left = static_cast<int>(static_cast<float>(screenWidth - panelExtent.width) - scalar * kK2PausePanelRight);
    } else {
        panelExtent.top += kK1PausePanelMargin;
        panelExtent.left = screenWidth - panelExtent.width - kK1PausePanelMargin;
    }

    auto place = [&panelExtent](Control::Extent extent) {
        extent.left += panelExtent.left;
        extent.top += panelExtent.top;
        return extent;
    };
    root.setExtent(panelExtent);
    reasonLabel.setExtent(place(reasonExtent));
    if (text.press) {
        pressLabel.setExtent(place(pressExtent));
    }
    unpause.setExtent(place(unpauseExtent));
}

std::optional<std::reference_wrapper<gui::Label>> HUD::statusFlashLabel(StatusSummaryCategory category) const {
    // A category flashes the indicator the HUD's layout has for it, if any.
    auto present = [](const std::shared_ptr<gui::Label> &label) -> std::optional<std::reference_wrapper<gui::Label>> {
        if (!label) {
            return std::nullopt;
        }
        return *label;
    };
    switch (category) {
    case StatusSummaryCategory::Journal:
        return present(_controls.LBL_JOURNAL);
    case StatusSummaryCategory::Credits:
        return present(_controls.LBL_CASH);
    case StatusSummaryCategory::PlotXP:
        return present(_controls.LBL_PLOTXP);
    case StatusSummaryCategory::StealthXP:
        return present(_controls.LBL_STEALTHXP);
    case StatusSummaryCategory::DarkSideShift:
        return present(_controls.LBL_DARKSHIFT);
    case StatusSummaryCategory::LightSideShift:
        return present(_controls.LBL_LIGHTSHIFT);
    case StatusSummaryCategory::ItemsReceived:
        return present(_controls.LBL_ITEMRCVD);
    case StatusSummaryCategory::ItemsLost:
        return present(_controls.LBL_ITEMLOST);
    default:
        return std::nullopt;
    }
}

void HUD::presentAlignmentShift(const StatusSummaryBatch &batch) {
    if (!_game.isTSL()) {
        return;
    }
    const int dark = batch.entry(StatusSummaryCategory::DarkSideShift).amount;
    const int light = batch.entry(StatusSummaryCategory::LightSideShift).amount;
    if (dark == light) {
        return;
    }
    _services.audio.mixer.play(_services.game.guiSounds.getAlignmentShift(light > dark), AudioType::Sound);
    auto leader = _game.party().getLeader();
    if (!leader->isPlayerCreated()) {
        return;
    }
    auto visual = std::make_shared<VisualEffect>(light > dark ? kLightSideVisual : kDarkSideVisual, false, _services);
    auto instance = visual->saveFacingInstance();
    instance.effect = visual;
    instance.creatorId = leader->id();
    _game.bindEffectCreator(instance);
    instance.setDuration(DurationType::Instant, 0.0f);
    leader->applyEffect(std::move(instance));
}

void HUD::flashStatus(StatusSummaryCategory category) {
    auto shared = [](StatusSummaryCategory flashed) -> std::optional<StatusSummaryCategory> {
        switch (flashed) {
        case StatusSummaryCategory::PlotXP:
            return StatusSummaryCategory::StealthXP;
        case StatusSummaryCategory::StealthXP:
            return StatusSummaryCategory::PlotXP;
        case StatusSummaryCategory::DarkSideShift:
            return StatusSummaryCategory::LightSideShift;
        case StatusSummaryCategory::LightSideShift:
            return StatusSummaryCategory::DarkSideShift;
        default:
            return std::nullopt;
        }
    };
    if (auto other = shared(category)) {
        _statusFlashes[static_cast<size_t>(*other)].reset();
        if (auto label = statusFlashLabel(*other)) {
            label->get().setVisible(false);
        }
    }
    // The net shift and the influence and Force rows have no indicator to flash.
    auto label = statusFlashLabel(category);
    if (!label) {
        return;
    }
    _statusFlashes[static_cast<size_t>(category)].activate();
    label->get().setVisible(true);
}

void HUD::resetStatusSummaryPresentation() {
    for (size_t i = 0; i < _statusFlashes.size(); ++i) {
        _statusFlashes[i].reset();
        if (auto label = statusFlashLabel(static_cast<StatusSummaryCategory>(i))) {
            label->get().setVisible(false);
        }
    }
    if (_statusSummary) {
        _statusSummary->reset();
    }
}

void HUD::onClick(const std::string &control) {
    // The pause, solo and stealth toggles click like checkboxes.
    if (control == "TB_PAUSE" || control == "TB_SOLO" || control == "TB_STEALTH") {
        auto clip = _services.game.guiSounds.getCheckboxCheck();
        _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
        return;
    }
    // An action slot sounds its own acceptance or rejection, with no click before it.
    if (!_actionBar.ownsActionControl(control)) {
        GameGUI::onClick(control);
    }
}

bool HUD::handle(const input::Event &event) {
    if (_statusSummary && _statusSummary->isVisible()) {
        return _statusSummary->handle(event);
    }
    // The pause indicator is not modal: it takes only what lands on it. It is
    // laid out by the first update after play pauses.
    if (isPausePanelShown() && _pauseLayoutReason) {
        if (_pauseGUI->handle(event)) {
            return true;
        }
        if (event.type == input::EventType::MouseButtonDown &&
            _pauseControls.BTN_UNPAUSE->extent().contains(event.button.x, event.button.y)) {
            return true;
        }
    }
    if (_select.handle(event)) {
        return true;
    }
    return _gui->handle(event);
}

void HUD::update(float dt) {
    _gui->update(dt);
    // The toggle shows any pause, not only the player's.
    _controls.TB_PAUSE->setOn(_game.isPaused());
    // Solo mode shows with companions; stealth when the leader can use it.
    _controls.TB_SOLO->setVisible(_game.party().companionCount() > 0);
    _controls.TB_SOLO->setOn(_game.party().isSoloMode());
    auto stealthLeader = _game.party().getLeader();
    _controls.TB_STEALTH->setVisible(stealthLeader && stealthLeader->isStealthCapable());
    _controls.TB_STEALTH->setOn(stealthLeader && stealthLeader->isStealthed());
    updatePausePanel(dt);

    // Module/script work is updated before the HUD. Snapshotting here gives
    // all status events in that batch one deterministic coalescing boundary,
    // without a user-visible timer or synchronous popup from the mutation.
    // A pending level-up chime plays when its batch is presented, or at once
    // when the batch has nothing to show.
    auto &summary = _game.statusSummary();
    if (_statusSummary && !_game.feedbackOptions().statusSummary) {
        // With the summary option off, the pending rows only flash their
        // indicators, a pending level-up chime plays, and the batch is
        // dropped.
        const auto &pending = summary.pending();
        for (auto category : pending.activeCategories()) {
            if (pending.flashes(category, _game.isTSL())) {
                flashStatus(category);
            }
        }
        presentAlignmentShift(pending);
        if (pending.levelUpSound) {
            _services.audio.mixer.play(_services.game.guiSounds.getOnLevelUpNotify(), AudioType::Sound);
        }
        const bool reported = !pending.empty();
        summary.discardPending();
        if (reported) {
            _game.finishStatusSummaryCycle();
        }
    } else if (_statusSummary && _statusSummary->presentPending()) {
        // The summary takes all input, releases included; steering stops.
        _game.stopMovement();
        auto clip = _services.resource.audioClips.get("gui_quest");
        _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
        presentAlignmentShift(*summary.displayed());
        if (summary.displayed()->levelUpSound) {
            _services.audio.mixer.play(_services.game.guiSounds.getOnLevelUpNotify(), AudioType::Sound);
        }
    } else if (summary.takeLevelUpSound()) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnLevelUpNotify(), AudioType::Sound);
    }

    Party &party = _game.party();
    std::vector<Label *> charLabels {
        _controls.LBL_CHAR1.get(),
        _controls.LBL_CHAR2.get(),
        _controls.LBL_CHAR3.get()};
    std::vector<Label *> backLabels {
        _controls.LBL_BACK1.get(),
        _controls.LBL_BACK2.get(),
        _controls.LBL_BACK3.get()};
    std::vector<Label *> lvlUpBgLabels {
        _controls.LBL_LVLUPBG1.get(),
        _controls.LBL_LVLUPBG2.get(),
        _controls.LBL_LVLUPBG3.get()};
    std::vector<Label *> levevlUpLabels {
        _controls.LBL_LEVELUP1.get(),
        _controls.LBL_LEVELUP2.get(),
        _controls.LBL_LEVELUP3.get()};
    std::array<Label *, 3> debilitatedLabels {
        _controls.LBL_DEBILATATED1.get(),
        _controls.LBL_DEBILATATED2.get(),
        _controls.LBL_DEBILATATED3.get()};
    std::array<ProgressBar *, 3> vitalityBars {
        _controls.PB_VIT1.get(),
        _controls.PB_VIT2.get(),
        _controls.PB_VIT3.get()};
    std::array<ProgressBar *, 3> forceBars {
        _controls.PB_FORCE1.get(),
        _controls.PB_FORCE2.get(),
        _controls.PB_FORCE3.get()};

    for (int i = 0; i < 3; ++i) {
        Label &LBL_CHAR = *charLabels[i];
        Label &LBL_BACK = *backLabels[i];
        Label &LBL_LEVELUP = *levevlUpLabels[i];

        Label *LBL_LVLUPBG;
        if (!_game.isTSL()) {
            LBL_LVLUPBG = lvlUpBgLabels[i];
        }

        auto memberIndex = portraitMemberIndex(i);
        std::shared_ptr<Creature> member(memberIndex ? party.getMember(*memberIndex) : nullptr);
        if (member) {
            LBL_CHAR.setVisible(true);
            LBL_CHAR.setBorderFill(member->portrait());
            LBL_BACK.setVisible(true);
            // A fallen member shows no mark; otherwise a pending level-up
            // shows ahead of being held helpless.
            const bool down = member->isDead() || member->isTemporarilyDead();
            const bool levelUp = !down && member->isLevelUpPending();
            const bool debilitated = !down && !levelUp && member->isDebilitated();
            // A member standing able to level up raises the level-up chime
            // once, until it levels up again.
            if (levelUp && member->isLevelUpChimeArmed()) {
                summary.submitLevelUpSound();
                member->setLevelUpChimeArmed(false);
            }
            LBL_LEVELUP.setVisible(levelUp);
            if (!_game.isTSL()) {
                LBL_LVLUPBG->setVisible(levelUp);
            }
            debilitatedLabels[i]->setVisible(debilitated);
            // A helpless leader says so on the combat line. Any member able
            // to act, the leader included, returns it to the idle line, so
            // it stays only while the whole party is helpless or fallen.
            if (*memberIndex == 0 && debilitated) {
                setCombatMessage(kLeaderDebilitatedCombatMessage);
            }
            if (!member->isDebilitated() && _combatMessageStrref == kLeaderDebilitatedCombatMessage) {
                setCombatMessage(party.idleCombatMessage());
            }
            int maxHitPoints = member->maxHitPoints();
            vitalityBars[i]->setVisible(true);
            vitalityBars[i]->setValue(maxHitPoints > 0
                ? std::clamp(100 * member->currentHitPoints() / maxHitPoints, 0, 100)
                : 0);

            int forcePoints = member->maxForcePoints();
            forceBars[i]->setVisible(forcePoints > 0);
            forceBars[i]->setValue(forcePoints > 0
                ? std::clamp(100 * member->currentForce() / forcePoints, 0, 100)
                : 0);
        } else {
            LBL_CHAR.setVisible(false);
            LBL_BACK.setVisible(false);
            LBL_LEVELUP.setVisible(false);
            if (!_game.isTSL()) {
                LBL_LVLUPBG->setVisible(false);
            }
            debilitatedLabels[i]->setVisible(false);
            vitalityBars[i]->setVisible(false);
            forceBars[i]->setVisible(false);
        }
    }

    // With no one under control there is no combat mode to show.
    if (auto leader = party.getLeader(); leader && leader->clientCombatMode()) {
        toggleCombat(true);
        refreshActionQueueItems();
    } else {
        toggleCombat(false);
    }

    updateCombatMessage(dt);
    for (size_t i = 0; i < _statusFlashes.size(); ++i) {
        auto label = statusFlashLabel(static_cast<StatusSummaryCategory>(i));
        if (!label) {
            continue;
        }
        _statusFlashes[i].update(dt);
        label->get().setVisible(_statusFlashes[i].visible());
    }

    _select.update(dt);
    _actionBar.update(dt);
    updateTransitionPresentation();
    _barkBubble->update(dt);
    if (_statusSummary && _statusSummary->isVisible()) {
        _statusSummary->update(dt);
    }

    // Hide minimap when there is no image to display
    _controls.LBL_MAPBORDER->setVisible(_game.map().isLoaded());

    if (_capturePresentation) {
        _controls.LBL_CHAR1->setVisible(true);
        _controls.LBL_BACK1->setVisible(true);
        _controls.PB_HEALTH->setVisible(true);
        _controls.PB_HEALTH->setValue(65);
        _controls.PB_VIT1->setVisible(true);
        _controls.PB_VIT1->setValue(65);
        _controls.PB_FORCE1->setVisible(true);
        _controls.PB_FORCE1->setValue(45);
        if (_captureCombatPresentation) {
            toggleCombat(true);
            for (auto &queue : {_controls.LBL_QUEUE0, _controls.LBL_QUEUE1,
                                _controls.LBL_QUEUE2, _controls.LBL_QUEUE3}) {
                queue->setBorderFill(g_attackIcon);
            }
        }
    }
}

void HUD::showCapturePresentation(bool combat) {
    _capturePresentation = true;
    _captureCombatPresentation = combat;
    _gui->rootControl().setVisible(true);
}

void HUD::showTransitionCapturePresentation(const std::string &destination) {
    if (!_areaTransition) {
        throw std::runtime_error("Area-transition GUI is unavailable");
    }
    _captureTransitionPresentation = true;
    _areaTransition->show(destination);
}

void HUD::clearCapturePresentation() {
    _capturePresentation = false;
    _captureCombatPresentation = false;
    _captureTransitionPresentation = false;
    if (_barkBubble) {
        _barkBubble->setBarkText("", 0.0f);
    }
    if (_areaTransition) {
        _areaTransition->hide();
    }
}

void HUD::updateTransitionPresentation() {
    if (!_areaTransition) {
        return;
    }
    if (_captureTransitionPresentation) {
        return;
    }
    auto candidate = currentTransitionCandidate();
    if (candidate) {
        _areaTransition->show(candidate->destination);
    } else {
        _areaTransition->hide();
    }
}

std::optional<TransitionPortal> HUD::currentTransitionCandidate() const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return std::nullopt;
    }
    auto leader = _game.party().getLeader();
    if (!leader) {
        return std::nullopt;
    }
    auto camera = _game.getActiveCamera();
    if (!camera) {
        return std::nullopt;
    }
    auto cameraSceneNode = camera->cameraSceneNode();
    if (!cameraSceneNode || !cameraSceneNode->camera()) {
        return std::nullopt;
    }
    auto &graphicsCamera = *cameraSceneNode->camera();

    TransitionView view;
    view.leaderPosition = leader->position();
    view.cameraViewProjection = graphicsCamera.projection() * graphicsCamera.view();

    return pickTransitionPortal(module->area()->transitionPresentationPortals(), view);
}

void HUD::render() {
    _gui->render();
    if (isPausePanelShown() && _pauseLayoutReason) {
        _pauseGUI->render();
    }

    renderEffectStacks();
    renderMinimap();


    _barkBubble->render();
    if (_areaTransition && _areaTransition->isVisible()) {
        _areaTransition->render();
    }
    _select.render();
    _actionBar.render(_gui->scale());
    _game.floatingText().render();
}

void HUD::renderModal() {
    if (_statusSummary && _statusSummary->isVisible()) {
        _statusSummary->render();
    }
}

void HUD::renderMinimap() {
    const Control::Extent &extent = _controls.LBL_MAPVIEW->extent();

    glm::vec4 bounds;
    bounds[0] = static_cast<float>(_gui->controlOffset().x + extent.left);
    bounds[1] = static_cast<float>(_gui->controlOffset().y + extent.top);
    bounds[2] = static_cast<float>(extent.width);
    bounds[3] = static_cast<float>(extent.height);

    std::shared_ptr<Area> area(_game.module()->area());
    // The frame is a control and scales with the layout; the map drawn inside
    // it is not, so it has to be told.
    _game.map().render(Map::Mode::Minimap, bounds, _gui->scale());
}

// Combat-mode lines

/** The seconds a timed combat-mode line is shown. */
static constexpr float kCombatMessageDuration = 2.5f;
static const glm::vec3 kCombatMessageColor {0.7f, 0.7f, 0.6f};

/** The leader moves where the player sent it. */
static constexpr int kMovingCombatMessage = 42476;
/** Another line KotOR draws in the moving line's green. */
static constexpr int kGreenCombatMessage = 42478;
/** The leader cannot get around a creature in its way. */
static constexpr int kCannotGetAroundCombatMessage = 47859;
/** TSL draws every combat-mode line in one colour; KotOR colours it by line. */
static glm::vec3 combatMessageColor(bool tsl, int strref, int idle) {
    if (tsl) {
        return kCombatMessageColor;
    }
    if (strref == idle) {
        return {0.74f, 0.11f, 0.0f}; // red
    }
    switch (strref) {
    case kLeaderDebilitatedCombatMessage:
        return {0.98f, 0.45f, 0.0f}; // orange
    case kMovingCombatMessage:
    case kGreenCombatMessage:
        return {0.28f, 0.92f, 0.11f}; // green
    case kCannotGetAroundCombatMessage:
        return {0.95f, 0.0f, 0.85f}; // purple
    default:
        return {0.0f, 0.66f, 0.98f}; // blue
    }
}

void HUD::setCombatMessage(int strref) {
    if (_combatMessageRemaining > 0.0f) {
        return;
    }
    const int idle = _game.party().idleCombatMessage();
    // Lines about the leader's movement give way to an autopause.
    const bool movement = strref == kMovingCombatMessage ||
                          strref == Party::kMovingToAttackCombatMessage ||
                          strref == kCannotGetAroundCombatMessage;
    if (movement && _game.isAutoPaused()) {
        return;
    }
    const bool persistent = strref == idle ||
                            strref == kLeaderDebilitatedCombatMessage ||
                            strref == kMovingCombatMessage ||
                            strref == Party::kMovingToAttackCombatMessage;
    auto &label = *_controls.LBL_CMBTMODEMSG;
    label.setTextColor(combatMessageColor(_game.isTSL(), strref, idle));
    label.setTextMessage(_game.getInterfaceText(strref));
    label.setTextOpacity(1.0f);
    _combatMessageStrref = strref;
    _combatMessageDuration = persistent ? 0.0f : kCombatMessageDuration;
    _combatMessageRemaining = _combatMessageDuration;
}

// A timed line holds for half its time, fades over the rest, then yields to
// the controlled character's idle line.
void HUD::updateCombatMessage(float dt) {
    if (_combatMessageRemaining <= 0.0f) {
        return;
    }
    _combatMessageRemaining -= dt;
    if (_combatMessageRemaining > 0.0f) {
        _controls.LBL_CMBTMODEMSG->setTextOpacity(
            std::min(1.0f, 2.0f * _combatMessageRemaining / _combatMessageDuration));
        return;
    }
    _combatMessageRemaining = 0.0f;
    setCombatMessage(_game.party().idleCombatMessage());
}

// END Combat-mode lines

void HUD::toggleCombat(bool enabled) {
    _controls.BTN_CLEARALL->setVisible(enabled);
    _controls.BTN_CLEARONE->setVisible(enabled);
    _controls.BTN_CLEARONE2->setVisible(enabled);
    _controls.LBL_CMBTMODEMSG->setVisible(enabled);
    _controls.LBL_CMBTMSGBG->setVisible(enabled);
    _controls.LBL_QUEUE0->setVisible(enabled);
    _controls.LBL_QUEUE1->setVisible(enabled);
    _controls.LBL_QUEUE2->setVisible(enabled);
    _controls.LBL_QUEUE3->setVisible(enabled);

    if (_game.isTSL()) {
        // The first two TSL backings are persistent HUD art. The third is the
        // combat-sequence extension and follows combat visibility.
        _controls.LBL_COMBATBG3->setVisible(enabled);
    } else {
        _controls.LBL_COMBATBG1->setVisible(enabled);
        _controls.LBL_COMBATBG2->setVisible(enabled);
        _controls.LBL_COMBATBG3->setVisible(enabled);
    }
}

void HUD::refreshActionQueueItems() const {
    const auto leader = _game.party().getLeader();
    const std::array<std::reference_wrapper<Label>, kActionQueueSize> queueLabels {
        *_controls.LBL_QUEUE0,
        *_controls.LBL_QUEUE1,
        *_controls.LBL_QUEUE2,
        *_controls.LBL_QUEUE3};
    // The leader's round action under way, if any.
    const auto current = _game.combat().currentRoundAction(*leader);
    // An icon takes the next slot and no icon takes none. The first slot is
    // dimmed while nothing is under way.
    size_t shown = 0;
    auto show = [&](const std::shared_ptr<graphics::Texture> &icon) {
        if (!icon || shown == queueLabels.size()) {
            return;
        }
        Label &label = queueLabels[shown].get();
        label.setBorderFill(icon);
        label.setBorderFillOpacity(shown == 0 && !current ? kIdleActionQueueOpacity : 1.0f);
        label.setVisible(true);
        ++shown;
    };
    auto namedIcon = [this](const std::string &resRef) {
        return _services.resource.textures.get(resRef, graphics::TextureUsage::GUI);
    };
    // A cast shows its spell's icon or its item's, a feat its own.
    auto commandIcon = [this](Action &action) -> std::shared_ptr<graphics::Texture> {
        if (auto *cast = dyn_cast<CastSpellAction>(&action)) {
            return castIcon(*cast->spell(), cast->item());
        }
        if (auto *feat = dyn_cast<UseFeatAction>(&action)) {
            std::shared_ptr<Feat> row(_services.game.feats.get(feat->feat()));
            return row ? row->icon : nullptr;
        }
        return nullptr;
    };
    // The icon of a round entry. A stance or a weapon swap shows only on the
    // round dispatcher; a pause or a move shows nothing.
    auto roundActionIcon = [&](const Combat::PendingEntry &entry, bool dispatcher) -> std::shared_ptr<graphics::Texture> {
        switch (entry.type) {
        case SavedScheduledAction::kAttackType:
            return namedIcon(g_attackIcon);
        case SavedScheduledAction::kEquipType:
            return namedIcon(kEquipIcon);
        case SavedScheduledAction::kUnequipType:
            return namedIcon(kUnequipIcon);
        case SavedScheduledAction::kCastSpellType:
        case SavedScheduledAction::kItemCastSpellType:
        case SavedScheduledAction::kFeatAttackType:
            return entry.command ? commandIcon(entry.command->get().combatAction()) : nullptr;
        case SavedScheduledAction::kCombatStanceType:
            return dispatcher ? namedIcon(kDefensiveStanceIcon) : nullptr;
        case SavedScheduledAction::kSwitchWeaponsType:
            return dispatcher ? namedIcon(kSwitchWeaponsIcon) : nullptr;
        default:
            return nullptr;
        }
    };

    // Only attacks, casts, item uses and the round dispatcher show. The first
    // attack, cast or item use, or the dispatcher while a round action is
    // under way, shows the action under way: an attack as that action, a cast
    // or item use as itself. Without one an attack shows nothing. The
    // dispatcher then shows each entry still pending, or with none pending
    // and nothing shown yet, the stance or attack its round serves.
    SmallVector<Combat::PendingEntry, kActionQueueSize> pending;
    _game.combat().pendingScheduled(*leader, pending);
    bool currentShown = false;
    for (const auto &node : leader->actions().nodes) {
        if (shown == queueLabels.size()) {
            break;
        }
        if (!node->action || node->action->isCompleted() || node->action->isCancelled()) {
            continue;
        }
        const SavedActionType type = node->type();
        const bool dispatcher = type == SavedActionType::CombatDispatch;
        const bool cast = type == SavedActionType::CastSpell || type == SavedActionType::ItemCastSpell;
        if (((dispatcher && current) || cast || type == SavedActionType::AttackObject) && !currentShown) {
            currentShown = true;
            if (cast) {
                show(commandIcon(node->action->combatAction()));
            } else if (current) {
                show(roundActionIcon(*current, dispatcher));
            }
        }
        if (!dispatcher) {
            continue;
        }
        if (pending.empty()) {
            if (!currentShown) {
                show(namedIcon(leader->isInTotalDefense() ? kDefensiveStanceIcon : g_attackIcon));
            }
            continue;
        }
        for (const auto &entry : pending) {
            if (shown == queueLabels.size()) {
                break;
            }
            show(roundActionIcon(entry, true));
        }
    }
    for (size_t i = shown; i < queueLabels.size(); ++i) {
        queueLabels[i].get().setVisible(false);
    }
}

void HUD::renderEffectStacks() {
    Party &party = _game.party();
    const std::array<Label *, 3> portraits {
        _controls.LBL_BACK1.get(),
        _controls.LBL_BACK2.get(),
        _controls.LBL_BACK3.get()};
    const std::array<Label *, 3> beneficialIcons {
        _controls.LBL_CMBTEFCTINC1.get(),
        _controls.LBL_CMBTEFCTINC2.get(),
        _controls.LBL_CMBTEFCTINC3.get()};
    const std::array<Label *, 3> harmfulIcons {
        _controls.LBL_CMBTEFCTRED1.get(),
        _controls.LBL_CMBTEFCTRED2.get(),
        _controls.LBL_CMBTEFCTRED3.get()};

    auto &options = _game.options().graphics;
    glm::ivec2 screenSize {options.width, options.height};
    const glm::ivec2 &controlOffset = _gui->controlOffset();
    const float scalar = interfaceScalar(_game.isTSL(), screenSize.x);
    auto &context = _services.graphics.context;

    _gui->withRenderPass([&](scene::IRenderPass &pass) {
        for (int portraitIndex = 0; portraitIndex < 3; ++portraitIndex) {
            auto memberIndex = portraitMemberIndex(portraitIndex);
            if (!memberIndex) {
                continue;
            }
            auto member = party.getMember(*memberIndex);
            const bool leader = *memberIndex == 0;
            const auto &portrait = portraits[portraitIndex]->extent();
            auto counts = member->effectStackCounts();
            renderEffectStack(*beneficialIcons[portraitIndex], portrait, counts.positive, leader, true,
                              scalar, screenSize, controlOffset, pass, context);
            renderEffectStack(*harmfulIcons[portraitIndex], portrait, counts.negative, leader, false,
                              scalar, screenSize, controlOffset, pass, context);
        }
    });
}

} // namespace game

} // namespace reone
