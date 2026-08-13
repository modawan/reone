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

#include "reone/game/gui.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/guis.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/di/services.h"
#include "reone/scene/render/pass.h"

#include <algorithm>
#include <cmath>

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

GameGUI::GameGUI(Game &game,
                 ServicesView &services) :
    _game(game),
    _services(services) {

    if (game.isTSL()) {
        _baseColor = kTSLGUIColorBase;
        _hilightColor = kTSLGUIColorHilight;
        _disabledColor = kTSLGUIColorDisabled;
    } else {
        _baseColor = kGUIColorBase;
        _hilightColor = kGUIColorHilight;
        _disabledColor = kGUIColorDisabled;
    }
}

void GameGUI::init() {
    if (_resRef.empty()) {
        throw std::logic_error("GUI resRef must not be empty");
    }
    _gui = _services.gui.guis.get(_resRef, std::bind(&GameGUI::preload, this, std::placeholders::_1));
    if (!_gui) {
        throw ResourceNotFoundException(str(boost::format("GUI not found: %s") % _resRef));
    }
    _gui->setEventListener(*this);

    onGUILoaded();
}

void GameGUI::preload(IGUI &gui) {
    // Every game GUI defaults to scaled mode. It preserves the authored
    // aspect and fits the layout on the limiting screen axis.
    //
    // Subclasses that need edge anchoring override this after the base call.
    gui.setScaling(IGUI::ScalingMode::Scaled);
    if (_game.isTSL()) {
        gui.setResolution(800, 600);
        // TSL's GUI fills are authored as color masks. Their BORDER.COLOR
        // values carry the screen palette; drawing the textures white turns
        // title bars and panels into opaque gray rectangles.
        gui.setTintBorderFills(true);
    } else {
        gui.setDefaultHilightColor(kGUIColorHilight);
    }
}

bool GameGUI::handle(const input::Event &event) {
    if (!_gui) {
        return false;
    }
    return _gui->handle(event);
}

void GameGUI::update(float dt) {
    if (_gui) {
        _gui->update(dt);
    }
}

void GameGUI::render() {
    if (_gui) {
        _gui->render();
    }
}

void GameGUI::loadBackground(BackgroundType type) {
    std::string resRef;

    if (_game.isTSL()) {
        switch (type) {
        case BackgroundType::Menu:
        case BackgroundType::Load:
            // TSL's pause-menu plate is transparent outside its authored
            // frame. The retail menus put blackfill behind it so the paused
            // world cannot show through at wider aspect ratios.
            resRef = "blackfill";
            break;
        case BackgroundType::Computer0:
        case BackgroundType::Computer1:
            // The terminal bezel is the computer_p panel's own fill; using it
            // as the backdrop too drew a cropped second copy behind the
            // panel. The retail terminal sits on a void fill.
            resRef = "black";
            break;
        default:
            return;
        }
    } else {
        auto &options = _game.options().graphics;
        if ((options.width == 1600 && options.height == 1200) ||
            (options.width == 1280 && options.height == 960) ||
            (options.width == 1024 && options.height == 768) ||
            (options.width == 800 && options.height == 600)) {

            resRef = str(boost::format("%dx%d") % options.width % options.height);
        } else {
            resRef = "1600x1200";
        }
        switch (type) {
        case BackgroundType::Menu:
            resRef += "back";
            break;
        case BackgroundType::Load:
            resRef += "load";
            break;
        case BackgroundType::Computer0:
            resRef += "comp0";
            break;
        case BackgroundType::Computer1:
            resRef += "comp1";
            break;
        default:
            return;
        }
    }

    if (_gui) {
        _gui->setBackground(_services.resource.textures.get(resRef, TextureUsage::MainTex));
    }
}

void GameGUI::useBakedItemSlotArt(ListBox &listBox) {
    if (_game.isTSL()) {
        return;
    }
    auto &root = _gui->rootControl();
    if (!root.border().fill) {
        return;
    }

    // Pixel bounds measured in K1's authored 640x480 canvas. The panel art
    // stores one slot as separate icon and nameplate silhouettes, so the two
    // can be repainted independently of the baked strip - whose period is
    // fractional in this canvas and so never registers with the row pitch.
    // Everything else is derived from the proto item.
    static constexpr int kIconInkLeft = 5;
    static constexpr int kInkTop = 3;
    static constexpr int kInkOvershoot = 1;
    static constexpr int kNameplateInkLeft = 8;
    static constexpr int kNameplateInkTrim = 9;
    static constexpr int kIconOutlineInset = 5;
    static constexpr int kNameplateOutlineInset = 2;

    const auto &authoredCanvas = root.authoredExtent();
    const auto &slot = listBox.protoItem().authoredExtent();
    glm::vec2 canvas(authoredCanvas.width, authoredCanvas.height);
    if (canvas.x <= 0.0f || canvas.y <= 0.0f) {
        return;
    }
    auto panel = root.border().fill;

    Control::Extent iconInk {
        slot.left + kIconInkLeft,
        slot.top + kInkTop,
        slot.height - kIconInkLeft,
        slot.height + kInkOvershoot};
    Control::Extent nameplateInk {
        slot.left + slot.height + kNameplateInkLeft,
        slot.top + kInkTop,
        slot.width - slot.height - kNameplateInkTrim,
        slot.height + kInkOvershoot};

    listBox.setBackgroundRenderer(
        [panel, canvas, iconInk, nameplateInk](const ListBox &list, const glm::ivec2 &offset, IRenderPass &pass) {
            auto viewport = list.itemsViewport();
            if (viewport.width <= 0 || viewport.height <= 0 || list.layoutScale() <= 0.0f) {
                return;
            }
            auto blit = [&](const Control::Extent &source, const Control::Extent &destination) {
                if (source.width <= 0 || source.height <= 0 ||
                    destination.width <= 0 || destination.height <= 0) {
                    return;
                }
                glm::mat3x4 uv(
                    glm::vec4(source.width / canvas.x, 0.0f, 0.0f, 0.0f),
                    glm::vec4(0.0f, source.height / canvas.y, 0.0f, 0.0f),
                    glm::vec4(source.left / canvas.x,
                              1.0f - (source.top + source.height) / canvas.y,
                              0.0f,
                              0.0f));
                pass.drawImage(
                    *panel,
                    {destination.left + offset.x, destination.top + offset.y},
                    {destination.width, destination.height},
                    glm::vec4(1.0f),
                    uv);
            };

            // Cover the baked strip with a single texel lifted from the gap
            // between two authored slots, so only the rows placed below
            // survive. A zero-scale UV makes every fragment sample that texel.
            const auto &slot = list.protoItem().authoredExtent();
            glm::vec4 gap(
                (slot.left + slot.height / 2 + 0.5f) / canvas.x,
                1.0f - (slot.top + slot.height + list.padding() / 2 + 0.5f) / canvas.y,
                0.0f,
                0.0f);
            pass.drawImage(
                *panel,
                {viewport.left + offset.x, viewport.top + offset.y},
                {viewport.width, viewport.height},
                glm::vec4(1.0f),
                glm::mat3x4(glm::vec4(0.0f), glm::vec4(0.0f), gap));

            int iconInset = static_cast<int>(std::lround(kIconOutlineInset * list.layoutScale()));
            int plateInset = static_cast<int>(std::lround(kNameplateOutlineInset * list.layoutScale()));
            for (int i = 0; i < list.visibleItemCount(); ++i) {
                auto row = list.visibleItemExtent(i);
                int iconSize = std::max(row.height - 2 * iconInset, 0);
                blit(iconInk, {row.left + iconInset, row.top + iconInset, iconSize, iconSize});
                blit(nameplateInk,
                     {row.left + row.height + plateInset,
                      row.top + plateInset,
                      std::max(row.width - row.height - 2 * plateInset, 0),
                      std::max(row.height - 2 * plateInset, 0)});
            }
        });
}

void GameGUI::centerRootInCanvas(int canvasWidth, int canvasHeight) {
    auto &root = _gui->rootControl();
    auto authored = root.authoredExtent();
    authored.left = (canvasWidth - authored.width) / 2;
    authored.top = (canvasHeight - authored.height) / 2;
    root.setAuthoredExtent(std::move(authored));
    _gui->refreshLayout();
}

std::shared_ptr<Texture> GameGUI::itemFrameTexture(int stackSize) const {
    // The wider plates exist to carry the stack count. Picking one for a
    // stack that does not need it puts a three-digit plate behind a single
    // digit.
    static const char *kTSLFrames[] {"uibit_eqp_itm1", "uibit_eqp_itm2", "uibit_eqp_itm3"};
    static const char *kK1Frames[] {"lbl_hex_3", "lbl_hex_6", "lbl_hex_7"};
    int tier = stackSize >= 100 ? 2 : (stackSize > 1 ? 1 : 0);
    auto resRef = _game.isTSL() ? kTSLFrames[tier] : kK1Frames[tier];
    return _services.resource.textures.get(resRef, TextureUsage::GUI);
}

void GameGUI::tintK2InGameHeader() {
    if (!_game.isTSL()) {
        return;
    }

    for (auto &tag : {"LBL_BAR1", "LBL_BAR2", "LBL_BAR6"}) {
        auto control = _gui->findControl(tag);
        if (control) {
            control->setTintBorderFill(true);
        }
    }
}

void GameGUI::enableK2ButtonBodyFill(Control &control) {
    if (!_game.isTSL()) {
        return;
    }

    auto edge = _services.resource.textures.get("uibit_brdr_16wet", TextureUsage::GUI);
    auto corner = _services.resource.textures.get("uibit_brdr_16wct", TextureUsage::GUI);

    auto border = control.border();
    border.edge = edge;
    border.corner = corner;
    control.setBorder(std::move(border));

    auto hilight = control.hilight();
    hilight.edge = std::move(edge);
    hilight.corner = std::move(corner);
    control.setHilight(std::move(hilight));
}

void GameGUI::enableK2ButtonBodyFill(const std::shared_ptr<Control> &control) {
    if (!control) {
        return;
    }

    enableK2ButtonBodyFill(*control);
}

void GameGUI::fillK2SectionStrip(const std::shared_ptr<Control> &topBar, const std::shared_ptr<Control> &bottomBar) {
    static constexpr int kLineThickness = 2;

    if (!_game.isTSL() || !topBar || !bottomBar) {
        return;
    }

    topBar->setBorderFill("uibit_brdr_16we");
    topBar->setTintBorderFill(true);

    bottomBar->setBorderFill("uibit_brdr_16we");
    bottomBar->setBorderFillTransform(Control::Border::FillTransform::Rotate180);
    bottomBar->setTintBorderFill(true);

    auto &upper = topBar->extent();
    auto &lower = bottomBar->extent();
    int lowerLineTop = lower.top + kLineThickness;
    bottomBar->setExtentTop(lowerLineTop - lower.height);

    int top = upper.top;
    int height = lowerLineTop - top;
    if (height <= 0) {
        return;
    }

    auto fill = std::shared_ptr<Control>(_gui->newControl(ControlType::Label, topBar->tag() + "_SECTION_FILL"));
    fill->setExtent({upper.left, top, upper.width, height});
    fill->setBorderFill("uibit_fill_2wt");
    fill->setBorderColor(topBar->border().color);
    fill->setTintBorderFill(true);
    _gui->addControlToFront(std::move(fill), IGUI::ControlCoordinates::Authored);
}

void GameGUI::useK2ShellTitle(const std::shared_ptr<Control> &control) {
    if (!_game.isTSL() || !control) {
        return;
    }

    _k2InGameTitleControl = control;
    control->setVisible(false);
}

void GameGUI::updateK2FilterButton(const std::shared_ptr<Control> &button, bool selected) {
    if (!button) {
        return;
    }

    button->setSelected(selected);
    button->setTextColor(selected ? glm::vec3 {1.0f} : _baseColor);
}

std::string GameGUI::guiResRef(const std::string &base) const {
    return _game.isTSL() ? base + "_p" : base;
}

void GameGUI::onClick(const std::string &control) {
    auto clip = _services.game.guiSounds.getOnClick();
    _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
}

void GameGUI::onSelectionChanged(const std::string &control, bool selected) {
    if (selected) {
        auto clip = _services.game.guiSounds.getOnEnter();
        _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
    }
}

} // namespace game

} // namespace reone
