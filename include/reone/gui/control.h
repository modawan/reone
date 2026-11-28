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

#include <array>
#include <optional>

#include "reone/graphics/texture.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"

#include "reone/resource/parser/gff/gui.h"

#include "controlrender.h"
#include "types.h"

namespace reone {

namespace resource {

class Gff;
class IFonts;

struct ResourceServices;

} // namespace resource

namespace graphics {

struct GraphicsServices;

class Font;
class Texture;

} // namespace graphics

namespace scene {

class IRenderPass;
class ISceneGraphs;

} // namespace scene

namespace gui {

class IGUI;

class Control : boost::noncopyable {
public:
    static constexpr int kStretchLeft = 1;
    static constexpr int kStretchTop = 2;
    static constexpr int kStretchWidth = 4;
    static constexpr int kStretchHeight = 8;
    static constexpr int kStretchAll = kStretchLeft | kStretchTop | kStretchWidth | kStretchHeight;

    /**
     * A text's alignment as a layout gives it: one horizontal bit, left,
     * centre or right, and one vertical, top, middle or bottom.
     */
    enum class TextAlign {
        LeftTop = 9,
        CenterTop = 10,
        RightTop = 12,
        LeftCenter = 17,
        CenterCenter = 18,
        RightCenter = 20,
        LeftBottom = 33,
        CenterBottom = 34,
        RightBottom = 36
    };

    static constexpr int kTextAlignCenter = 0x02;
    static constexpr int kTextAlignRight = 0x04;
    static constexpr int kTextAlignHorizontal = 0x07;
    static constexpr int kTextAlignMiddle = 0x10;
    static constexpr int kTextAlignBottom = 0x20;
    static constexpr int kTextAlignVertical = 0x38;

    /** A direction the arrow keys move the keyboard in, from one control to another. */
    enum class MoveDirection {
        Up,
        Left,
        Down,
        Right
    };

    struct Extent {
        int left {0};
        int top {0};
        int width {0};
        int height {0};

        Extent() = default;
        Extent(int left, int top, int width, int height);

        bool contains(int x, int y) const;
        void getCenter(int &x, int &y) const;
    };

    struct Border {
        enum class FillTransform {
            None,
            /** A quarter turn clockwise. */
            Rotate90,
            Rotate180,
            /** A quarter turn anticlockwise. */
            Rotate270
        };

        /** How the fill covers the area inside the frame, as FILLSTYLE authors it. */
        enum class FillStyle {
            /** Whole copies of the image, each stretched by a share of what is left over. */
            Tile,
            /** The image at its own size in the middle, squeezed only along an axis it overflows. */
            Center,
            /** The image stretched over the whole area. */
            Stretch,
            /**
             * The art stretched over the control's whole extent, frame
             * included, in a GUI that draws this style (FillRules::extentFills);
             * nothing in one that does not.
             */
            Extent,
            /** A style the layout names that is none of the others: the fill is not drawn. */
            None
        };

        /** The corners' art; none when no art is named or the named art is missing (cornerMissing). */
        std::shared_ptr<graphics::Texture> corner;
        /**
         * The corners name art the resources do not hold. Such a corner has
         * no size of its own and is drawn as a plain quad in the border's
         * colour.
         */
        bool cornerMissing {false};
        /** The edges' art; none when no art is named or the named art is missing (edgeMissing). */
        std::shared_ptr<graphics::Texture> edge;
        /**
         * The edges name art the resources do not hold. Such an edge has no
         * size of its own and is drawn as a plain quad in the border's colour.
         */
        bool edgeMissing {false};
        /** The fill's art; none when no art is named or the named art is missing (fillMissing). */
        std::shared_ptr<graphics::Texture> fill;
        /**
         * The fill names art the resources do not hold. Such a fill has no
         * size, so only the stretched styles draw it, as a plain quad in the
         * fill's colour.
         */
        bool fillMissing {false};
        glm::vec3 color {1.0f};
        /** The opacity every piece of the border is drawn with. */
        float alpha {1.0f};
        int dimension {0};
        /** How far the inner area reaches out over the frame on the left and right, at most the dimension. */
        int innerOffset {0};
        /** How far the inner area reaches out over the frame above and below; with none, innerOffset. */
        int innerOffsetY {0};
        FillTransform fillTransform {FillTransform::None};
        FillStyle fillStyle {FillStyle::Stretch};
        /** While set, the corners and edges are drawn at the GUI's pulsing alpha instead of alpha. */
        bool pulseFrame {false};
        /** While set, the fill is drawn at the GUI's pulsing alpha instead of alpha. */
        bool pulseFill {false};

        /** Whether the corners name art, held or missing; a border without has no frame. */
        bool hasCorner() const { return corner || cornerMissing; }
        /** Whether the edges name art, held or missing. */
        bool hasEdge() const { return edge || edgeMissing; }
        /** Whether the fill names art, held or missing. */
        bool hasFill() const { return fill || fillMissing; }
        /** Takes the fill away: no art is named. */
        void clearFill() {
            fill.reset();
            fillMissing = false;
        }
    };

    struct Text {
        std::string text;
        /**
         * The font the layout names, or the layout's default when it names
         * none; the text shows its small or large variant (fontName).
         */
        std::string baseFont {resource::generated::GUI_TEXT().FONT};
        std::shared_ptr<graphics::Font> font;
        glm::vec3 color {1.0f};
        /** The colour while the control is selected; unset, the hilight's. */
        std::optional<glm::vec3> selectedColor;
        TextAlign align {TextAlign::CenterCenter};
        float opacity {1.0f};
        /** The text is drawn at the GUI's pulsing alpha. */
        bool pulsing {false};
        /**
         * While the control is selected, the text is drawn at the GUI's pulsing
         * alpha; deselecting the control stops the text's pulse.
         */
        bool hilightPulsing {false};
    };

    /**
     * The font a text shows for the font a layout names: its small variant,
     * ending in 'a', or its large one, ending in 'b'. The console font has
     * no variants.
     */
    static std::string fontName(const std::string &baseFont, bool useSmall);
    /** The variant of \p baseFont (fontName), or the base font itself when the install has no usable variant. */
    static std::shared_ptr<graphics::Font> findFont(resource::IFonts &fonts, const std::string &baseFont, bool useSmall);

    static ControlType getType(const resource::generated::GUI_BASECONTROL &gui);
    static std::string getTag(const resource::generated::GUI_BASECONTROL &gui);
    static std::string getParent(const resource::generated::GUI_BASECONTROL &gui);

    virtual ~Control() = default;

    virtual void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem = false);
    virtual void update(float dt);
    virtual void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass);

    void updateTransform();
    void updateTextLines();

    /**
     * Stretches this control in both directions.
     *
     * @param mask bitmask, specifying which components to stretch
     */
    virtual void stretch(float x, float y, int mask = kStretchAll);

    bool isVisible() const { return _visible; }
    bool isSelectable() const { return _selectable; }
    /** Whether pointing at the control selects it; one that isn't is selected only by its screen. */
    bool isSelectedOnHover() const { return _selectedOnHover; }
    bool isSelected() const { return _selected; }
    bool isDisabled() const { return _disabled; }
    /** Whether the control takes the keyboard when the pointer comes onto it. */
    bool takesKeyboardOnHover() const { return _takesKeyboardOnHover; }
    bool isHilightOverBorder() const { return _hilightOverBorder; }

    int id() const { return _id; }
    /** The kind of control this is. */
    ControlType type() const { return _type; }
    /** The ID of the control the arrow key of \p direction moves the keyboard to from this one. */
    int moveTo(MoveDirection direction) const { return _moveTo[static_cast<size_t>(direction)]; }
    int padding() const { return _padding; }
    /** The GUI the control belongs to. */
    IGUI &gui() const { return _gui; }
    Border &border() const { return *_border; }
    const Extent &authoredExtent() const { return _authoredExtent; }
    void setAuthoredExtent(Extent extent) { _authoredExtent = std::move(extent); }

    /** The combined layout and text factor used to render this control's text. */
    float scale() const { return _scale; }
    /** The scale the control's frames and their art are drawn at (setPresentationScale). */
    float frameScale() const { return _frameLayoutScale; }
    void setScale(float scale) {
        _scale = scale;
        updateTextLines();
    }
    /** Applies text and frame scale without changing the control's rectangle. */
    void setPresentationScale(float layoutScale);
    /**
     * Applies independent frame and text layout scales without changing the
     * control's rectangle. A GUI whose frames keep their own size
     * (FillRules::unscaledArt) draws them at the border scale whatever
     * \p frameLayoutScale is.
     */
    virtual void setPresentationScale(float frameLayoutScale, float textLayoutScale);
    const Extent &extent() const { return _extent; }
    /** The area the borders are drawn over: the whole extent, unless the control lays them out apart from its text. */
    virtual Extent borderExtent() const { return _extent; }
    /** The area the text is laid out in: the whole extent, unless the control lays it out apart from its borders. */
    virtual Extent textExtent() const { return _extent; }
    /**
     * The area inside the frames of \p area, an area of this control: the
     * text's, or a list's rows'. A control takes the area inside its border
     * (see borderInnerExtent); a button and an image button take what their
     * border's and their hilight's share (innerExtentWithHilight).
     */
    virtual Extent innerExtent(const Extent &area) const;
    /**
     * The area inside \p border over \p area. The border's measure is its
     * dimension or, with none, the height of its corner art when it names
     * corner and edge art. With a measure, both axes are inset by it and it is
     * the step; without one, a border naming corner and edge art is inset by
     * its corner art's width across and height down, the edge art's width
     * being the step, and any other border is not inset. Missing art measures
     * 0. An axis that leaves less room than half the step collapses onto its
     * centre. The area then reaches out by the inner offset across, at most
     * the measure, and by all of the vertical inner offset, or with none the
     * inner offset, up and down.
     */
    Extent borderInnerExtent(const Border &border, const Extent &area) const;
    /**
     * The measure of the control's border down (see borderInnerExtent): its
     * dimension or, with none, the height its corners inset the area inside
     * it by; 0 for a control without a border.
     */
    int borderDimension() const;
    /** The part two areas share; an empty area where they do not meet. */
    static Extent intersection(const Extent &a, const Extent &b);
    const Border &hilight() const { return *_hilight; }
    /** Whether the control has a border. */
    bool hasBorder() const { return static_cast<bool>(_border); }
    /** Whether the control has a hilight border. */
    bool hasHilight() const { return static_cast<bool>(_hilight); }
    /** The colour the control's frames show now (getBorderColor); white for a control without a border. */
    glm::vec3 shownFrameColor() const { return _border ? getBorderColor() : glm::vec3(1.0f); }
    /** The opacity of the control's own border; full without one. */
    float frameAlpha() const { return _border ? _border->alpha : 1.0f; }
    const std::string &borderFillResRef() const { return _borderFillResRef; }
    const std::string &hilightFillResRef() const { return _hilightFillResRef; }
    const std::string &tag() const { return _tag; }
    const Text &text() const { return _text; }
    const std::vector<std::string> &textLines() const { return _textLines; }
    /**
     * The height of \p lineCount lines of the control's text measured as one
     * block: the font's block height at the control's text scale; 0 for a
     * control without a font.
     */
    int textBlockHeight(int lineCount) const;
    /**
     * The control's text line unit: how far apart its lines are drawn, at
     * its text scale, rounded; 0 for a control without a font.
     */
    int textLineUnit() const;
    /**
     * How far apart the control's text lines are drawn, at its text scale,
     * unrounded; 0 for a control without a font.
     */
    float textLineAdvance() const;
    const std::string &sceneName() const { return _sceneName; }
    /** The text the tooltip shows while the pointer rests on the control; empty for none. */
    const std::string &tooltip() const { return _tooltip; }

    void setId(int id) { _id = id; }
    void setTag(std::string tag) { _tag = std::move(tag); }
    void setBorder(Border border);
    /** Sets the border's fill to the art named \p resRef, a fill even when that art is missing; an empty name takes the fill away. */
    void setBorderFill(std::string resRef);
    /** Sets the border's fill to \p texture; none takes the fill away. */
    void setBorderFill(std::shared_ptr<graphics::Texture> texture);
    /** Sets how the border's fill covers the area inside its frame; a control without a border gets one with no pieces. */
    void setBorderFillStyle(Border::FillStyle style);
    void setBorderFillTransform(Border::FillTransform transform);
    /** Sets the border's colour; a control without a border gets one with no pieces, in that colour. */
    void setBorderColor(glm::vec3 color);
    /** Sets the opacity every piece of the border is drawn with. */
    void setBorderAlpha(float alpha);
    void setBorderColorOverride(glm::vec3 color);
    void setDisabled(bool disabled);
    /** Sets whether the control takes the keyboard when the pointer comes onto it. */
    void setTakesKeyboardOnHover(bool takes) { _takesKeyboardOnHover = takes; }
    /** Sets the ID of the control the arrow key of \p direction moves the keyboard to from this one. */
    void setMoveTo(MoveDirection direction, int id) { _moveTo[static_cast<size_t>(direction)] = id; }
    /** Puts back the links the layout gave the control, undoing setMoveTo. */
    void restoreLayoutMoves() { _moveTo = _layoutMoveTo; }
    virtual void setExtent(Extent extent);
    virtual void setExtentHeight(int height);
    void setExtentTop(int top);
    virtual void setSelected(bool selected);
    void setSelectable(bool selectable);
    /** Sets whether pointing at the control selects it. */
    void setSelectedOnHover(bool selectedOnHover) { _selectedOnHover = selectedOnHover; }
    void setHeight(int height);
    void setHilight(Border hilight);
    /** The hilight and the text take \p color while the control is selected. */
    void setHilightColor(glm::vec3 color);
    /** Sets the hilight's fill to the art named \p resRef, a fill even when that art is missing; an empty name takes the fill away. */
    void setHilightFill(std::string resRef);
    /** Sets the hilight's fill to \p texture; none takes the fill away. */
    void setHilightFill(std::shared_ptr<graphics::Texture> texture);
    void setHilightFillTransform(Border::FillTransform transform);
    void setHilightOverBorder(bool enabled) { _hilightOverBorder = enabled; }
    void setPadding(int padding);
    void setSceneName(std::string name);
    /** Sets the text the tooltip shows while the pointer rests on the control; empty for none. */
    void setTooltip(std::string text) { _tooltip = std::move(text); }
    void setText(Text text);
    void setTextAlignment(TextAlign align);
    void setTextColor(glm::vec3 color);
    /**
     * The text alone takes \p color while the control is selected, or with
     * none the hilight's colour.
     */
    void setSelectedTextColor(std::optional<glm::vec3> color);
    /** Sets whether the text is drawn at the GUI's pulsing alpha. */
    void setTextPulsing(bool pulsing) { _text.pulsing = pulsing; }
    /** The hilight's frame and fill pulse, as chosen; a control without a hilight is left alone. */
    void setHilightPulsing(bool frame, bool fill);
    /** The border's frame and fill pulse, as chosen; a control without a border is left alone. */
    void setBorderPulsing(bool frame, bool fill);
    /** Sets the alpha the text is drawn with. */
    void setTextOpacity(float opacity) { _text.opacity = opacity; }
    void setTextMessage(std::string text);
    /** Sets the font the text is named in; it shows the variant the GUI's font size picks. */
    void setTextFont(std::string baseFont);
    /** Shows the text in the variant of its font the GUI's font size now picks. */
    virtual void resetFont();
    void setSharpenBorderFillAlpha(bool sharpen) { _sharpenBorderFillAlpha = sharpen; }
    void setUseBorderColorOverride(bool use);
    bool usesBorderColorOverride() const { return _useBorderColorOverride; }
    const glm::vec3 &borderColorOverride() const { return _borderColorOverride; }
    void setVisible(bool visible);

    void setTextLines(std::vector<std::string> lines) {
        _textLines = std::move(lines);
    }

    // Childen

    void addChildToFront(Control &child) {
        _children.insert(_children.begin(), child);
    }

    void addChildToBack(Control &child) {
        _children.push_back(child);
    }

    std::vector<std::reference_wrapper<Control>> &children() {
        return _children;
    }

    // END Children

    // User input

    virtual bool handleGamepadButtonDown(const input::GamepadButtonEvent &event);
    virtual bool handleGamepadButtonUp(const input::GamepadButtonEvent &event);
    virtual bool handleGamepadAxisMotion(const input::GamepadAxisMotionEvent &event);
    virtual bool handleKeyDown(const input::KeyCode key);
    virtual bool handleKeyUp(const input::KeyCode key);
    virtual bool handleMouseMotion(int x, int y);
    virtual bool handleMouseWheel(int x, int y);
    virtual bool handleClick(int x, int y, int clicks = 1);
    /**
     * A press of the left button over this control, which keeps the mouse
     * until the release.
     *
     * @return whether the control takes the press for itself
     */
    virtual bool handleMouseDown(int x, int y) { return false; }
    /**
     * The mouse moved while this control keeps it.
     *
     * @return whether the control took the move, so that the pointer moves onto nothing else
     */
    virtual bool handleMouseDrag(int x, int y) { return false; }
    /**
     * The left button was released after a press on this control. A release
     * over the control clicks it.
     *
     * @param over whether the pointer is over the control
     * @param clicks the number of clicks the release ends
     * @return whether the release clicks the control
     */
    virtual bool handleMouseUp(int x, int y, bool over, int clicks) { return over; }

    // END User input

    // Event listeners

    void setOnClick(std::function<void()> fn) { _onClick = std::move(fn); }
    void setOnSelectionChanged(std::function<void(bool)> fn) { _onSelectedChanged = std::move(fn); }
    void setOnMouseWheel(std::function<void(int, int)> fn) { _onMouseWheel = std::move(fn); }

    // END Event listeners

protected:
    IGUI &_gui;
    ControlType _type;

    int _id {-1};
    std::string _tag;
    std::string _borderFillResRef;
    std::string _hilightFillResRef;
    Extent _authoredExtent;
    int _authoredBorderDimension {0};
    int _authoredHilightDimension {0};
    /** How many screen pixels each authored pixel of the frame and fill covers. */
    float _frameLayoutScale {1.0f};
    /** The hilight's horizontal inner offset as authored. */
    int _authoredHilightInnerOffset {0};
    /** The hilight's vertical inner offset as authored. */
    int _authoredHilightInnerOffsetY {0};
    /** The border's horizontal inner offset as authored. */
    int _authoredInnerOffset {0};
    /** The border's vertical inner offset as authored; with none, the horizontal one applies. */
    int _authoredInnerOffsetY {0};
    float _scale {1.0f};
    Extent _extent;
    std::shared_ptr<Border> _border;
    std::shared_ptr<Border> _hilight;
    Text _text;
    std::string _sceneName;
    /** The text the tooltip shows for the control; empty for none. */
    std::string _tooltip;
    int _padding {0};
    glm::mat4 _transform {1.0f};
    bool _visible {true};
    bool _disabled {false};
    bool _selected {false};
    bool _hilightOverBorder {false};
    bool _selectable {false};
    /** Whether this control is a list's prototype, drawn once for each row. */
    bool _isProtoItem {false};
    /** Pointing at the control selects it. */
    bool _selectedOnHover {true};
    /** The control takes the keyboard when the pointer comes onto it. */
    bool _takesKeyboardOnHover {false};
    /** By MoveDirection, the IDs of the controls the arrow keys move the keyboard to. */
    std::array<int, 4> _moveTo {-1, -1, -1, -1};
    /** The links as the layout gave them. */
    std::array<int, 4> _layoutMoveTo {-1, -1, -1, -1};
    bool _sharpenBorderFillAlpha {false};
    glm::vec3 _borderColorOverride {1.0f};
    bool _useBorderColorOverride {false};
    std::vector<std::string> _textLines;

    std::vector<std::reference_wrapper<Control>> _children;

    // Services

    graphics::GraphicsServices &_graphicsSvc;
    resource::ResourceServices &_resourceSvc;
    scene::ISceneGraphs &_sceneGraphs;

    // END Services

    // Event listeners

    std::function<void()> _onClick;
    std::function<void(bool)> _onSelectedChanged;
    std::function<void(int, int)> _onMouseWheel;

    // END Event listeners

    Control(
        IGUI &gui,
        ControlType type,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        _gui(gui),
        _type(type),
        _sceneGraphs(sceneGraphs),
        _graphicsSvc(graphicsSvc),
        _resourceSvc(resourceSvc) {
    }

    /**
     * Draws \p border over the control's extent moved by \p offset, \p size
     * across: the corners and the edges, each edge in its segments and the
     * art of each piece turned to its side, then the fill over them.
     *
     * @param part the parts of the border drawn
     * @param pulsing whether the whole border is drawn at the GUI's pulsing
     *                alpha, whatever the border's own pulse
     * @param color the colour every piece, fill included, is drawn in; with
     *        none, the control's border colour
     */
    void renderBorder(const Border &border,
                      const glm::ivec2 &offset,
                      const glm::ivec2 &size,
                      scene::IRenderPass &pass,
                      BorderRenderPart part = BorderRenderPart::All,
                      bool pulsing = false,
                      const std::optional<glm::vec3> &color = std::nullopt);

    /**
     * The size of frame or fill art at the frame's scale: the whole image,
     * however many frames it is cut into; 0 for missing art.
     */
    glm::ivec2 frameArtSize(const std::shared_ptr<graphics::Texture> &art) const;
    /**
     * An authored frame size at the frame's scale, rounded; one above 0 is
     * never scaled down to 0, which would take the art's own size.
     */
    int scaledFrameSize(int authored) const;
    /** The part the inner extents of the border and the hilight share over \p area. */
    Extent innerExtentWithHilight(const Extent &area) const;
    /**
     * The control's border and hilight have taken the presentation scale;
     * frames of its own take it now, before its text is laid out again.
     */
    virtual void scaleOwnFrames() {}

    void renderScene(const glm::ivec2 &screenSize,
                     const glm::ivec2 &offset,
                     scene::IRenderPass &pass);

    void renderText(const std::vector<std::string> &lines,
                    const glm::ivec2 &offset,
                    const glm::ivec2 &size,
                    scene::IRenderPass &pass);

    virtual const glm::vec3 &getBorderColor() const;

    /** The border drawn while the control is not hilighted, if any. */
    virtual const std::shared_ptr<Border> &shownBorder() const { return _border; }
    /** The border drawn while the control is hilighted, if any. */
    virtual const std::shared_ptr<Border> &shownHilight() const { return _hilight; }

    /** Creates a border from its GUI resource fields. */
    std::shared_ptr<Border> createBorder(const resource::generated::GUI_BORDER &gui) const;
    /** The texture coordinates that turn art drawn over a rect by \p quarters quarter turns anticlockwise. */
    static glm::mat3x4 quarterTurnUV(int quarters);
    /** The texture coordinates that draw art upside down over a rect. */
    static glm::mat3x4 upsideDownUV();
    /** The texture coordinates that draw art left to right reversed over a rect. */
    static glm::mat3x4 leftToRightUV();

    /** Whether a layout's art name names art: an empty name and "0" name none. */
    static bool namesArt(const std::string &resRef) { return !resRef.empty() && resRef != "0"; }
    /** Whether a layout's border names any art: a corner, an edge or a fill. */
    static bool namesArt(const resource::generated::GUI_BORDER &border) {
        return namesArt(border.CORNER) || namesArt(border.EDGE) || namesArt(border.FILL);
    }

private:
    /**
     * Visits the rects, relative to the corner of a control \p size across,
     * that \p border's fill covers, by its fill style, with \p visit. Art is
     * measured at the frame's scale, which by the GUI's fill rules may be
     * the border scale alone; missing art has no size. Private, since it is
     * defined in control.cpp alone.
     */
    template <typename Visit>
    void forEachFillRect(const Border &border, const glm::ivec2 &size, Visit &&visit) const;

    void loadExtent(const resource::generated::GUI_EXTENT &gui);
    void loadBorder(const resource::generated::GUI_BORDER &gui);
    void loadText(const resource::generated::GUI_TEXT &gui);
    void loadHilight(const resource::generated::GUI_BORDER &gui);

    /** How a border's frame lies over a control of some size. */
    struct FrameLayout {
        /** The area inside the frame, relative to the control's corner. */
        Extent area;
        /**
         * How many edge segments run along the top and bottom (x) and along
         * the left and right (y); 0 where the frame takes the whole axis.
         */
        glm::ivec2 segments {0};
        /** The step on both axes, which the segment counts and lengths are measured by. */
        int step {0};
    };

    /**
     * How \p border's frame lies over a control \p size across. A border that
     * names no corner art has no frame. Otherwise the corner and the step are
     * the dimension; with no dimension, the corner art's height when edge art
     * is named and that height is not 0, else the corner is the corner art's
     * size and the step the edge art's width, or without edge art the corner
     * art's width. Along each axis the room between the corners holds as
     * many segments as whole steps across, or one where it holds half a step
     * or there is no step; with a segment the frame takes the corner's length
     * on both sides, without it takes the whole axis, half on either side,
     * the far side the larger.
     */
    FrameLayout frameLayout(const Border &border, const glm::ivec2 &size) const;

    /** How a border's frame is measured on each axis. */
    struct FrameMeasure {
        /** The length the corners take. */
        glm::ivec2 corner {0};
        /**
         * The step the edges are laid out in on both axes: an edge is its art
         * turned to its side, so it runs the art's own length on either.
         */
        int step {0};
    };

    /**
     * The measure of \p border's frame, as it is drawn (frameLayout): the
     * dimension on both axes when the border has one or names no corner art;
     * otherwise the corner art's height with edge art when that height is not
     * 0, else the corner art's size, the step being the edge art's width, or
     * without edge art the corner art's.
     */
    FrameMeasure frameMeasure(const Border &border) const;
    /**
     * The measure of the area inside \p border's frame (borderInnerExtent):
     * the frame's, but none for a border with no dimension and no edge art.
     */
    FrameMeasure innerMeasure(const Border &border) const;

    /** Places \p lineCount lines of the text in \p area, the area inside the border. */
    void getTextPosition(glm::ivec2 &position, int lineCount, const Extent &area, graphics::TextGravity &gravity) const;
};

} // namespace gui

} // namespace reone
