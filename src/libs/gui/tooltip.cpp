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

#include "reone/gui/tooltip.h"

#include <cmath>

#include "reone/graphics/font.h"
#include "reone/graphics/options.h"
#include "reone/graphics/textutil.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"
#include "reone/system/logutil.h"

namespace reone {

namespace gui {

/** The label of the tooltip panel that shows the tooltip. */
static constexpr char kTooltipTag[] = "tooltip";
// The measures below are in the panel's layout pixels, scaled with it.
/** How far right of and below the pointer the tooltip is placed. */
static constexpr int kTooltipPointerOffset = 15;
/** How close to the screen's right and bottom edges the tooltip may come. */
static constexpr int kTooltipScreenMargin = 2;
/** The room the tooltip leaves around its text, across both sides. */
static constexpr int kTooltipPadding = 8;
/** The step by which the tooltip narrows to fit its text. */
static constexpr int kTooltipWidthStep = 10;

void Tooltip::setPanel(std::shared_ptr<IGUI> panel) {
    _label.reset();
    _panel = std::move(panel);
    _shown.reset();
    if (!_panel) {
        return;
    }
    _label = _panel->findControl(kTooltipTag);
    if (!_label) {
        warn("Tooltip panel has no tooltip label", LogChannel::GUI);
        _panel.reset();
        return;
    }
    _layoutExtent = _label->extent();
}

void Tooltip::beginPointerMove() {
    _move = PointerMove();
}

void Tooltip::hover(const IGUI &gui, const std::optional<std::reference_wrapper<Control>> &control) {
    _move.followed = true;
    if (control && !_move.target) {
        _move.target = Target {gui, control->get()};
    }
}

void Tooltip::endPointerMove() {
    if (!_move.followed) {
        resetTimer();
        return;
    }
    const Control *before {_hovered ? &_hovered->control.get() : nullptr};
    const Control *after {_move.target ? &_move.target->control.get() : nullptr};
    if (after == before) {
        return;
    }
    if (_shown) {
        remove();
    }
    setTemporarilyDisabled(false);
    _hovered = _move.target;
    resetTimer();
}

void Tooltip::closePanel(const IGUI &gui) {
    remove();
    if (_hovered && &_hovered->gui.get() == &gui) {
        _hovered.reset();
    }
}

void Tooltip::press() {
    if (_shown) {
        remove();
    }
    setTemporarilyDisabled(true);
}

void Tooltip::turnWheel(bool overList) {
    if (_shown) {
        remove();
    }
    if (overList && _disabled) {
        setTemporarilyDisabled(false);
    }
}

void Tooltip::forget(const IGUI &gui) {
    if (_hovered && &_hovered->gui.get() == &gui) {
        _hovered.reset();
    }
    if (_move.target && &_move.target->gui.get() == &gui) {
        _move.target.reset();
    }
    if (_shown && &_shown->gui.get() == &gui) {
        remove();
    }
}

void Tooltip::update(float dt) {
    if (!_timer) {
        return;
    }
    *_timer += dt;
    if (*_timer <= kDefaultDelay) {
        return;
    }
    _timer.reset();
    if (_hovered && _enabled && !_disabled && _panel) {
        const auto &text = _hovered->control.get().tooltip();
        if (!text.empty()) {
            display(*_hovered, text);
        }
    }
}

void Tooltip::render() {
    if (!_shown) {
        return;
    }
    // The text follows its control's, sized again from the place it was
    // shown at.
    const auto &text = _shown->control.get().tooltip();
    if (text != _shownText) {
        fit(text);
    }
    _panel->withRenderPass([this](scene::IRenderPass &pass) {
        _label->render({_graphicsOpt.width, _graphicsOpt.height}, {0, 0}, pass);
    });
}

void Tooltip::resetTimer() {
    if (_enabled && !_disabled) {
        _timer = 0.0f;
    }
}

void Tooltip::remove() {
    _shown.reset();
    resetTimer();
}

void Tooltip::setTemporarilyDisabled(bool disabled) {
    if (disabled) {
        _timer.reset();
    } else {
        resetTimer();
    }
    _disabled = disabled;
}

// The tooltip is as narrow as it can be, in steps, without taking more lines
// than at the label's own width. Its text is measured at the label's text
// scale.
void Tooltip::display(const Target &target, const std::string &text) {
    if (!_label->text().font) {
        return;
    }
    _shown = target;
    _shownPointer = _pointer;
    fit(text);
}

void Tooltip::fit(const std::string &text) {
    auto &label = *_label;
    _shownText = text;
    label.setTextMessage(text);
    if (!label.text().font) {
        return;
    }
    auto &font = *label.text().font;
    const float scale {label.scale()};
    const float layoutScale {_panel->scale()};
    auto px = [layoutScale](int length) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(length) * layoutScale))); };
    const int widthStep {px(kTooltipWidthStep)};
    const int padding {px(kTooltipPadding)};
    const int pointerOffset {px(kTooltipPointerOffset)};
    const int screenMargin {px(kTooltipScreenMargin)};
    auto heightAt = [&](int width) {
        return font.blockHeight(static_cast<int>(graphics::breakText(text, font, width, scale).size()), scale);
    };
    const int fullWidth {label.innerExtent(_layoutExtent).width};
    const int fullHeight {heightAt(fullWidth)};
    int width {fullWidth - widthStep};
    while (width > 0 && heightAt(width) == fullHeight) {
        width -= widthStep;
    }
    if (width <= 0) {
        width = fullWidth;
    }
    Control::Extent extent;
    extent.width = width + widthStep + padding;
    // Text in a font under 16 pixels high takes a pixel more.
    const int fontPixelHeight {static_cast<int>(font.height() * scale)};
    extent.height = fullHeight + (fontPixelHeight < 16 ? 1 : 0) + padding;
    extent.left = _shownPointer.x + pointerOffset;
    if (extent.left + extent.width >= _graphicsOpt.width) {
        extent.left = _graphicsOpt.width - extent.width - screenMargin;
    }
    extent.top = _shownPointer.y + pointerOffset;
    if (extent.top + extent.height >= _graphicsOpt.height) {
        extent.top = _graphicsOpt.height - extent.height - screenMargin;
    }
    label.setExtent(std::move(extent));
}

} // namespace gui

} // namespace reone
