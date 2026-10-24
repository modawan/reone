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

#include "../gui.h"

namespace reone {

namespace graphics {
class Font;
}

namespace game {

/**
 * The closing credits, drawn over everything else on an opaque black screen,
 * or over the world when the background is transparent. Every row of the
 * credits table but the last is shown in turn, fading in and out; the last
 * row then scrolls up from below the screen. The credits finish once the
 * scroll has ended and their music, if any, has stopped, or at once on a
 * click, accept or back.
 */
class CreditsGUI : public GameGUI {
public:
    /**
     * @param transparentBackground whether the world shows behind the text
     * @param music the music played under the credits; empty for none
     */
    CreditsGUI(Game &game, ServicesView &services, bool transparentBackground, std::string music);
    /** The credits' music stops with them. */
    ~CreditsGUI();

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    /** Whether the credits have run their course or were dismissed. */
    bool isFinished() const { return _finished; }

private:
    bool _transparentBackground;
    std::string _musicResRef;
    std::shared_ptr<audio::AudioSource> _music;
    std::shared_ptr<graphics::Font> _font;
    glm::vec3 _textColor {1.0f};
    /** The text of each row of the credits table. */
    std::vector<int> _strRefs;
    int _nextRow {0};

    /** The lines of the text shown now. */
    std::vector<std::string> _lines;
    /** Whether a row fades in and out, rather than the last one scrolling. */
    bool _fading {true};
    bool _fadingIn {true};
    float _fadeTime {0.0f};
    float _alpha {0.0f};

    /** How many pixels the last row has scrolled, and how many it scrolls in all. */
    int _scrollOffset {0};
    int _scrollEnd {0};
    /** The seconds each pixel of scrolling takes, and the time not yet scrolled. */
    float _scrollStep {0.0f};
    float _scrollTime {0.0f};

    bool _finished {false};

    void onGUILoaded() override;

    /** Break the next row's text into lines; the last row starts scrolling. */
    void showNextText();
    void updateFading(float dt);
    void updateScrolling(float dt);

    /** The text's scale for the screen height. */
    float textScale() const;
    int lineHeight() const;
    /** The area the text is drawn in: left, top, width and height in pixels. */
    glm::ivec4 textRegion() const;
};

} // namespace game

} // namespace reone
