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

#pragma once

#include <algorithm>

#include "../control.h"

namespace reone {

namespace gui {

class ScrollBar : public Control {
public:
    /** How long an arrow or the track must be held before it acts again, in seconds. */
    static constexpr float kPressRepeatDelay = 0.5f;
    /** How often a held arrow or track acts again after the delay, in seconds. */
    static constexpr float kPressRepeatInterval = 0.1f;
    /** The most time one update counts while the bar is held, so a stall acts a few times at most, in seconds. */
    static constexpr float kLongestHoldStep = 0.5f;

    /**
     * What the bar shows of the list it scrolls. The values are in the
     * list's unit: rows, or pixels for a block of text taller than the list.
     */
    struct ScrollState {
        int maxValue {1};     /**< how far the list scrolls */
        int curValue {0};     /**< how far it is scrolled */
        int visibleValue {1}; /**< how much of it shows at once */
        /**
         * The maximum already counts the last position, so a bar showing
         * one at a time never adds one to it.
         */
        bool extendedRange {false};
    };

    /** The parts of the bar a press can land on. */
    enum class Part {
        None,
        FirstArrow,
        LastArrow,
        Thumb,
        TrackBeforeThumb,
        TrackAfterThumb
    };

    /**
     * The list a scroll bar scrolls: what the bar shows of it, and how a
     * press on the bar moves it.
     */
    class IScrollable {
    public:
        virtual ~IScrollable() = default;

        /** The values the bar shows of the list as it is scrolled now. */
        virtual ScrollState getScrollState() const = 0;
        /** The positions the list scrolls through. */
        virtual int getScrollPositions() const = 0;
        /** The position, from 0, the list is scrolled to. */
        virtual int getScrollPosition() const = 0;
        /** Scrolls the list to the position, one of its positions. */
        virtual void scrollToPosition(int position) = 0;
        /** Scrolls the list a page back or on. */
        virtual void scrollPage(bool next) = 0;

        /**
         * Scrolls the list \p positions on, or back for a negative count,
         * staying within its positions.
         *
         * @return whether the list moved
         */
        bool scrollBy(int positions) {
            const int last = getScrollPositions() - 1;
            const int position = std::clamp(getScrollPosition() + positions, 0, std::max(last, 0));
            if (position == getScrollPosition()) {
                return false;
            }
            scrollToPosition(position);
            return true;
        }
    };

    ScrollBar(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::ScrollBar,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {
    }

    /** \p control as a scroll bar; null when it is none or of another type. */
    static ScrollBar *of(const std::shared_ptr<Control> &control);

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;

    /**
     * Shows the list's scroll values. A visible value below 1 counts as 1,
     * unless it and the maximum are both 0.
     */
    void setScrollState(ScrollState state);

    /** Whether the arrows are drawn at the GUI's scroll-arrow pulse, as a list's are. Off unless set. */
    void setArrowsPulsing(bool pulsing) { _arrowsPulsing = pulsing; }

    /**
     * The left button is pressed on the bar at the point, in the list's
     * coordinates, and the bar scrolls \p list until the release. A press on
     * the thumb starts dragging it. A press on an arrow scrolls the list a
     * position back or on, as the wheel does, and one on the track before or
     * after the thumb a page back or on; either acts at once and again while
     * it is held there (hold).
     */
    void press(int x, int y, IScrollable &list);
    /**
     * The left button is pressed at the point, in the list's coordinates:
     * the bar takes the press (press) when it is shown and the point is on it.
     *
     * @return whether the bar took the press
     */
    bool pressIfOn(int x, int y, IScrollable &list);
    /**
     * The pointer moves to the point while the left button is held. Dragging
     * the thumb scrolls the list to the position it was pressed at, moved by
     * the positions beyond the first in the share of the thumb's travel the
     * pointer has moved along the bar, rounded and kept within the list
     * (draggedValue). A thumb with no travel leaves the list where it is.
     */
    void drag(int x, int y, IScrollable &list);
    /**
     * The pointer moves while the left button is held: a held bar drags (drag).
     *
     * @return whether the bar is held
     */
    bool dragIfHeld(int x, int y, IScrollable &list);
    /**
     * Time passes on the GUIs' clock (IGUI::textureTime), whatever the
     * game's speed. While the bar is held the time counts only while the
     * pointer is on the arrow or the track pressed: it acts again once it
     * has been held half a second, then every tenth of a second, at any
     * frame rate. An update counts at most kLongestHoldStep, so a stall is
     * made up by a few acts at most.
     */
    void hold(IScrollable &list);
    /**
     * The left button is released: nothing on the bar is pressed.
     *
     * @return whether the bar was held
     */
    bool release();

private:
    struct Direction {
        std::shared_ptr<graphics::Texture> image;
    };

    struct Thumb {
        std::shared_ptr<graphics::Texture> image;
    };

    /** Where the bar's pieces lie, in the bar's own coordinates. */
    struct Layout {
        bool vertical {false};
        /** The arrows' length along the bar. */
        int arrowLength {0};
        Extent firstArrow;
        Extent lastArrow;
        /** The track between the arrows. */
        Extent track;
        /** The track's inner extent, which holds the thumb. */
        Extent inner;
        Extent thumb;
    };

    Direction _dir;
    Thumb _thumb;
    ScrollState _state;
    /** The bar shows only the arrows that have rows to scroll to: no track and no thumb. */
    bool _arrowsOnly {false};
    bool _arrowsPulsing {false};
    Part _pressedPart {Part::None};
    /** The left button was pressed on the bar and is not yet released. */
    bool _held {false};
    /** The pointer's last position while the left button is held. */
    glm::ivec2 _pointer {0};
    /** Where the thumb was pressed along the bar, and the list's position then. */
    int _thumbPressAlong {0};
    int _thumbPressPosition {0};
    /** Time left before a held arrow or track acts again, in seconds. */
    float _repeatTimer {0.0f};
    /** The GUIs' clock when the bar was pressed or last held. */
    double _holdClock {0.0};

    /** Where the pieces lie for the scroll values shown now. */
    Layout layout() const;
    /**
     * The part of the bar at the point, in the list's coordinates, alike
     * along a vertical and a horizontal bar: the thumb, its edges included;
     * an arrow, inside the bar's sides and from just past the bar's end to
     * the arrow's far edge; the track either side of the thumb, strictly
     * between the arrow and the thumb and strictly inside the track's inner
     * sides.
     */
    Part partAt(int x, int y) const;
    /** The part pressed, or held pressed, acts on the list. */
    void act(Part part, IScrollable &list);
    /** Draws the track's frame and the thumb. */
    void renderTrack(const glm::ivec2 &offset, const Layout &layout, scene::IRenderPass &pass);
};

} // namespace gui

} // namespace reone
