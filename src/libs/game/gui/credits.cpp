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

#include "reone/game/gui/credits.h"

#include <algorithm>
#include <cmath>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/textutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;

namespace reone {

namespace game {

// The text sits inside the screen, this far from every edge. Its size follows
// the screen height, as authored for a 480-pixel-high screen.
static constexpr int kTextInset = 44;
static constexpr float kAuthoredScreenHeight = 480.0f;
// Each row but the last fades in over a second, holds for two, fades out over
// a second and leaves a second of nothing before the next.
static constexpr float kFadeSeconds = 1.0f;
static constexpr float kShownSeconds = 3.0f;
static constexpr float kHiddenSeconds = 2.0f;
// The whole height of the last row's text scrolls past in this many seconds.
static constexpr float kScrollSeconds = 305.0f;

CreditsGUI::CreditsGUI(Game &game, ServicesView &services, bool transparentBackground, std::string music) :
    GameGUI(game, services),
    _transparentBackground(transparentBackground),
    _musicResRef(std::move(music)) {
    _resRef = "credits";
}

CreditsGUI::~CreditsGUI() {
    // Credits without music have none to stop.
    if (_music) {
        _music->stop();
    }
}

void CreditsGUI::onGUILoaded() {
    // The text takes the colour of the credits list's rows.
    _textColor = findControl<gui::ListBox>("LB_CREDITS")->protoItem().text().color;
    _font = _services.resource.fonts.get("fnt_credits");
    auto table = getRequiredTwoDA(_services.resource.twoDas, "credits");
    for (int row = 0; row < table->getRowCount(); ++row) {
        _strRefs.push_back(table->getInt(row, "name"));
    }
    // The music plays as a sound effect, at the area-wide ambient priority
    // group's volume.
    if (!_musicResRef.empty()) {
        _music = _services.audio.mixer.play(_services.resource.audioClips.get(_musicResRef), audio::AudioType::Sound,
                                            _game.areaMusic().ambientGroupGain());
    }
    showNextText();
}

// The last row scrolls; every other row is shown on its own. The scrolling
// text starts and ends with a screenful of blank lines, so it comes in from
// below and leaves at the top.
void CreditsGUI::showNextText() {
    const auto region = textRegion();
    auto text = _services.resource.strings.getText(_strRefs[_nextRow]);
    if (_nextRow >= static_cast<int>(_strRefs.size()) - 1) {
        _fading = false;
        _alpha = 1.0f;
        const std::string padding(static_cast<size_t>(region[3] / lineHeight() + 1), '\n');
        _lines = breakText(padding + text + padding, *_font, region[2], textScale());
        const int height = static_cast<int>(_lines.size()) * lineHeight();
        _scrollEnd = std::max(0, height - region[3]);
        _scrollStep = kScrollSeconds / static_cast<float>(height);
    } else {
        _lines = breakText(text, *_font, region[2], textScale());
    }
    ++_nextRow;
}

bool CreditsGUI::handle(const input::Event &event) {
    if ((event.type == input::EventType::MouseButtonDown && event.button.button == input::MouseButton::Left) ||
        (event.type == input::EventType::KeyDown &&
         (event.key.code == input::KeyCode::Return || event.key.code == input::KeyCode::Escape))) {
        _finished = true;
    }
    return true;
}

void CreditsGUI::update(float dt) {
    if (_finished) {
        return;
    }
    if (_fading) {
        updateFading(dt);
    } else {
        updateScrolling(dt);
    }
}

void CreditsGUI::updateFading(float dt) {
    _fadeTime += dt;
    if (_fadeTime < kFadeSeconds) {
        _alpha = _fadingIn ? _fadeTime : kFadeSeconds - _fadeTime;
    } else if (_fadingIn) {
        _alpha = 1.0f;
        if (_fadeTime >= kShownSeconds) {
            _fadeTime = std::fmod(_fadeTime, kShownSeconds);
            _fadingIn = false;
        }
    } else {
        _alpha = 0.0f;
        if (_fadeTime >= kHiddenSeconds) {
            _fadeTime = std::fmod(_fadeTime, kHiddenSeconds);
            _fadingIn = true;
            showNextText();
        }
    }
}

void CreditsGUI::updateScrolling(float dt) {
    if (_scrollOffset >= _scrollEnd) {
        if (!_music || !_music->isPlaying()) {
            _finished = true;
        }
        return;
    }
    _scrollTime += dt;
    while (_scrollTime >= _scrollStep && _scrollOffset < _scrollEnd) {
        ++_scrollOffset;
        _scrollTime -= _scrollStep;
    }
}

void CreditsGUI::render() {
    auto &graphics = _services.graphics;
    const auto &options = _game.options().graphics;
    graphics.context.withBlendMode(BlendMode::Normal, [&]() {
        if (!_transparentBackground) {
            graphics.uniforms.setLocals([&](auto &locals) {
                locals.reset();
                locals.model = glm::scale(glm::mat4(1.0f), glm::vec3(options.width, options.height, 1.0f));
                locals.color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            });
            graphics.context.useProgram(graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
            graphics.meshRegistry.get(MeshName::quad).draw(graphics.statistic);
        }
        const auto region = textRegion();
        const int height = lineHeight();
        // Shown rows sit in the middle of the text area; the scrolling text
        // hangs from its top.
        int top = _fading
                      ? region[1] + (region[3] - static_cast<int>(_lines.size()) * height) / 2
                      : region[1] - _scrollOffset;
        const glm::ivec4 scissorBounds {region[0], options.height - (region[1] + region[3]), region[2], region[3]};
        graphics.context.withScissorTestNoClear(scissorBounds, [&]() {
            for (const auto &line : _lines) {
                if (top + height > region[1] && top < region[1] + region[3]) {
                    glm::vec3 position {static_cast<float>(region[0] + region[2] / 2), static_cast<float>(top), 0.0f};
                    _font->render(line, position, glm::vec4(_textColor, _alpha), TextGravity::CenterBottom, textScale());
                }
                top += height;
            }
        });
    });
}

float CreditsGUI::textScale() const {
    return static_cast<float>(_game.options().graphics.height) / kAuthoredScreenHeight;
}

int CreditsGUI::lineHeight() const {
    return static_cast<int>(_font->height() * textScale());
}

glm::ivec4 CreditsGUI::textRegion() const {
    const auto &options = _game.options().graphics;
    return glm::ivec4(kTextInset, kTextInset, options.width - 2 * kTextInset, options.height - 2 * kTextInset);
}

} // namespace game

} // namespace reone
