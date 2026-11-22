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
#include <functional>
#include <unordered_set>

#include "reone/graphics/di/services.h"
#include "reone/graphics/options.h"
#include "reone/input/event.h"
#include "reone/resource/di/services.h"
#include "reone/resource/parser/gff/gui.h"

#include "control.h"

namespace reone {

namespace scene {

class ISceneGraphs;

} // namespace scene

namespace gui {

class Tooltip;

constexpr int kDefaultResolutionX = 640;
constexpr int kDefaultResolutionY = 480;

class IGUIEventListener {
public:
    virtual ~IGUIEventListener() = default;

    virtual void onClick(const std::string &control) = 0;
    virtual void onSelectionChanged(const std::string &control, bool selected) = 0;
    /**
     * The pointer came onto \p control or, for a list, reached one of its rows
     * while the list does not hold the keyboard, or moved its selection to
     * another of its rows.
     */
    virtual void onEntered(const std::string &control) = 0;
};

/**
 * The time every GUI is drawn by: the art that cycles through frames, and
 * the opacities of the pulses. The hilight pulse, which pulsing borders,
 * images and texts are drawn with, falls from 1 to 0.4 over 0.75 s, rises
 * back over the next 0.75 s, and repeats; the scroll-arrow pulse, which every
 * list's scroll arrows are drawn with, does the same down to 0.2. Controls
 * outside a GUI's tree, as a list's rows and scroll bars, keep the same time.
 */
class GUIClock {
public:
    /** Advances the clock by dt seconds: the art's time, then both pulses, the arrows' first. */
    void update(float dt) {
        _textureTime += dt;
        _arrows.update(dt);
        _hilights.update(dt);
    }

    /**
     * The seconds the clock has run for: the frame cycling art shows
     * (graphics::getCycleFrame), and the time held presses repeat by.
     */
    double textureTime() const { return _textureTime; }

    /** The hilight pulse's opacity now, 0.4 to 1. */
    float value() const { return _hilights.value; }
    /** The scroll-arrow pulse's opacity now, 0.2 to 1. */
    float arrowValue() const { return _arrows.value; }

private:
    /** Seconds a pulse takes to fall, or to rise. */
    static constexpr float kHalfPeriod = 0.75f;

    struct Pulse {
        /** The opacity at the bottom of the pulse. */
        float low;
        /** The opacity now. */
        float value {1.0f};
        /** Seconds left before the pulse turns. */
        float timeLeft {kHalfPeriod};
        /** Whether the opacity is rising; it starts falling from 1. */
        bool rising {false};

        void update(float dt) {
            timeLeft -= dt;
            while (timeLeft <= 0.0f) {
                rising = !rising;
                timeLeft += kHalfPeriod;
            }
            const float progress = (kHalfPeriod - timeLeft) / kHalfPeriod;
            value = rising ? low + (1.0f - low) * progress : 1.0f - (1.0f - low) * progress;
        }
    };

    double _textureTime {0.0};
    Pulse _hilights {0.4f};
    Pulse _arrows {0.2f};
};

class IGUI {
public:
    enum class ScalingMode {
        Center,
        CenterHorizontal,
        PositionRelativeToCenter,
        /** Uniformly scales a control around the authored and screen centres. */
        ScaledRelativeToCenter,
        /** Uniformly fits the authored layout, centred horizontally and anchored to the top. */
        ScaledTopCenter,
        /** Uniformly fits the authored layout in the screen. The game-GUI default. */
        Scaled
    };

    enum class ControlCoordinates {
        Authored,
        Screen
    };

    virtual ~IGUI() = default;

    virtual void load(const resource::Gff &gui) = 0;

    virtual bool handle(const input::Event &event) = 0;
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    /** Runs \p block in normal blending with the render pass the GUI's controls draw with. */
    virtual void withRenderPass(const std::function<void(scene::IRenderPass &)> &block) = 0;

    /** The GUI stops being shown: the control under the pointer is left, and a tooltip shown goes. */
    virtual void clearSelection() = 0;

    /**
     * The control holding the keyboard, if any: the one a screen gave it to,
     * or the last control the pointer came onto that takes the keyboard on
     * hover.
     */
    virtual std::optional<std::reference_wrapper<Control>> activeControl() const = 0;
    /**
     * Gives the keyboard to \p control, silently and without selecting it;
     * the control holding it before loses it, and its hilight on a GUI whose
     * hilight follows the keyboard, unless it is a list. A list taking it then selects as
     * ListBox::takeKeyboard does. Nothing changes when \p control holds the
     * keyboard already.
     */
    virtual void setActiveControl(Control &control) = 0;
    /**
     * Gives the keyboard to \p control as setActiveControl does and selects
     * it, without the sound of entering it. Nothing changes when \p control
     * holds the keyboard already.
     */
    virtual void selectActiveControl(Control &control) = 0;
    /**
     * No control holds the keyboard; the one holding it loses its hilight
     * on a GUI whose hilight follows the keyboard, unless it is a list.
     */
    virtual void clearActiveControl() = 0;
    /**
     * Makes the hilight of a control that takes the keyboard on hover, other
     * than a list, follow the keyboard: such a control takes the keyboard as
     * the pointer comes onto it, sounding unless it is hilighted; it stays
     * hilighted after the pointer leaves it, until another control takes the
     * keyboard or the pointer comes onto another such control; and coming
     * back onto it is silent. A list keeps its hilight on the pointer and
     * takes the keyboard by its rows.
     */
    virtual void setHilightFollowsKeyboard(bool follows) = 0;
    /**
     * Whether the arrow keys walk as KotOR's do, passing over a hidden control
     * as well as a disabled one (moveTarget's visibleOnly). Off, as in TSL.
     */
    virtual void setArrowWalkVisibleOnly(bool visibleOnly) = 0;
    /**
     * Whether a held arrow key wrapped one of the GUI's lists round: the
     * key's repeats then move none of its lists until a key is pressed again
     * (ListBox::selectAdjacentItem).
     */
    virtual bool keyRepeatStopped() const = 0;
    virtual void setKeyRepeatStopped(bool stopped) = 0;
    /**
     * The control the arrow key of \p direction moves the keyboard to from
     * \p control: the one its link names, or past a disabled one the next
     * along the same direction. A link to a control the keys don't move
     * through (a label, a panel, a bar) ends the walk there, and that control
     * takes the keyboard even when disabled. None when the links lead to no
     * control or back to \p control.
     *
     * @param visibleOnly KotOR's walk: it also passes over a hidden control,
     *        and a control it ends on takes the keyboard only when enabled
     *        and shown. TSL's passes over a disabled control only.
     */
    virtual std::optional<std::reference_wrapper<Control>> moveTarget(const Control &control, Control::MoveDirection direction, bool visibleOnly = false) const = 0;
    /**
     * The GUI comes up with the pointer at \p pointer, in screen coordinates.
     * The control hovered before loses its hilight, unless it holds the
     * keyboard on a GUI whose hilight follows it. The control under the
     * pointer is hovered: it is hilighted, sounding as it is entered unless it
     * was, but takes no keyboard; one that takes the keyboard on hover on such a GUI takes the
     * hilight from the control holding the keyboard, which keeps it (a list
     * draws its selected row without its hilight). The tooltip is left as it
     * is: the pointer comes onto a control for it only by moving.
     */
    virtual void hoverOnOpening(glm::ivec2 pointer) = 0;

    virtual Control &rootControl() = 0;

    virtual const glm::ivec2 &rootOffset() const = 0;
    virtual const glm::ivec2 &controlOffset() const = 0;

    /**
     * The uniform factor the authored layout is drawn at, or 1 for a mode
     * that does not scale.
     *
     * Anything drawn into a control's rect from outside the control tree -
     * the map inside its minimap frame, its markers - is in the same pixel
     * space as the layout and has to follow it.
     */
    virtual float scale() const = 0;

    /** Independent presentation dials owned by the live graphics options. */
    virtual float textScale() const = 0;
    virtual float textLayoutScale(float horizontalScale, float verticalScale) const = 0;
    virtual float borderScale() const = 0;
    virtual float listScale() const = 0;
    /** Whether an image button's text pulses while the button is selected, as a button's does. */
    virtual bool imageButtonTextPulses() const = 0;
    /** The opacity pulsing borders and texts are drawn with now. */
    virtual float pulsingAlpha() const = 0;
    /** The opacity a list's scroll arrows are drawn with now. */
    virtual float arrowPulsingAlpha() const = 0;
    /**
     * The seconds the GUIs' clock has run for, the same for every control:
     * the frame cycling art shows, and the time held presses repeat by. It
     * runs through pauses and at any game speed, and slows only with the
     * world under TSL's gameover panel.
     */
    virtual double textureTime() const = 0;
    /** How the GUI's controls draw their border fills. */
    virtual const FillRules &fillRules() const = 0;

    /** Whether scene-backed controls may render their 3D content. */
    virtual bool sceneRenderEnabled() const = 0;

    virtual void setEventListener(IGUIEventListener &listener) = 0;
    virtual void setResolution(int x, int y) = 0;
    virtual void setScaling(ScalingMode scaling) = 0;
    virtual void setTextScale(float scale) = 0;
    /** Sets whether the image buttons loaded afterwards pulse their text while selected. */
    virtual void setImageButtonTextPulses(bool pulses) = 0;
    /** Sets how the GUI's controls draw their border fills; set before the GUI loads. */
    virtual void setFillRules(FillRules rules) = 0;
    virtual void setControlScaling(const std::string &tag, ScalingMode scaling) = 0;
    /** Every control loaded afterwards draws its text in \p color while selected. */
    virtual void setSelectedTextColor(glm::vec3 color) = 0;
    virtual void setBackground(std::shared_ptr<graphics::Texture> texture) = 0;

    /**
     * Reapplies the layout after authored geometry or the resolution changed.
     * Controls added in screen coordinates keep the place their owner gave
     * them.
     */
    virtual void refreshLayout() = 0;

    virtual std::unique_ptr<Control> newControl(ControlType type, std::string tag) = 0;
    virtual void addControlToFront(std::shared_ptr<Control> control, ControlCoordinates coordinates) = 0;
    virtual void addControlToBack(std::shared_ptr<Control> control, ControlCoordinates coordinates) = 0;

    virtual std::shared_ptr<Control> findControl(const std::string &tag) const = 0;

    /** Whether a visible, enabled control that takes clicks lies under the screen point. */
    virtual bool hasSelectableControlAt(int x, int y) const = 0;
    /** An ID no control of the GUI has, for a control the GUI's layout lacks: one above the highest. */
    virtual int unusedControlId() const = 0;
    /** The control of the layout ID, none for a negative ID or one no control has; of two with one ID, the first loaded. */
    virtual std::optional<std::reference_wrapper<Control>> findControlById(int id) const = 0;
};

class GUI : public IGUI, boost::noncopyable {
public:
    /** @param tooltip the interface's tooltip, which the pointer and the mouse buttons on this GUI drive */
    GUI(
        graphics::GraphicsOptions &options,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc,
        const GUIClock &clock,
        Tooltip &tooltip) :
        _options(options),
        _sceneGraphs(sceneGraphs),
        _graphicsSvc(graphicsSvc),
        _resourceSvc(resourceSvc),
        _clock(clock),
        _tooltip(tooltip) {
    }

    /** The tooltip forgets the GUI's controls. */
    ~GUI() override;

    /** Derived from the current resolution, which can change at runtime. */
    glm::ivec2 screenCenter() const {
        return {_options.width / 2, _options.height / 2};
    }

    void load(const resource::Gff &gui) override;

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    void withRenderPass(const std::function<void(scene::IRenderPass &)> &block) override;

    void clearSelection() override;

    std::optional<std::reference_wrapper<Control>> activeControl() const override {
        return _activeControl;
    }

    void setActiveControl(Control &control) override;

    void selectActiveControl(Control &control) override;

    void clearActiveControl() override {
        releaseKeyboard();
    }

    void setHilightFollowsKeyboard(bool follows) override {
        _hilightFollowsKeyboard = follows;
    }
    void setArrowWalkVisibleOnly(bool visibleOnly) override { _arrowWalkVisibleOnly = visibleOnly; }

    bool keyRepeatStopped() const override { return _keyRepeatStopped; }
    void setKeyRepeatStopped(bool stopped) override { _keyRepeatStopped = stopped; }

    std::optional<std::reference_wrapper<Control>> moveTarget(const Control &control, Control::MoveDirection direction, bool visibleOnly = false) const override;

    void hoverOnOpening(glm::ivec2 pointer) override;

    Control &rootControl() override {
        return *_rootControl;
    }

    const glm::ivec2 &rootOffset() const override {
        return _rootOffset;
    }

    const glm::ivec2 &controlOffset() const override {
        return _controlOffset;
    }

    float scale() const override {
        switch (_scaling) {
        case ScalingMode::Scaled:
        case ScalingMode::ScaledTopCenter:
        case ScalingMode::PositionRelativeToCenter:
        case ScalingMode::ScaledRelativeToCenter:
            return scaledFactor();
        default:
            return 1.0f;
        }
    }

    void setEventListener(IGUIEventListener &listener) override {
        _eventListener = &listener;
    }

    void setResolution(int x, int y) override {
        _resolutionX = x;
        _resolutionY = y;
    }

    void setScaling(ScalingMode scaling) override {
        _scaling = scaling;
    }

    void setTextScale(float scale) override {
        _textScale = scale;
    }

    void setImageButtonTextPulses(bool pulses) override {
        _imageButtonTextPulses = pulses;
    }

    void setFillRules(FillRules rules) override {
        _fillRules = rules;
    }

    float textScale() const override {
        return _textScale.value_or(_options.guiTextScale);
    }

    float textLayoutScale(float horizontalScale, float verticalScale) const override {
        // Glyphs always follow the limiting resolution axis. Text may move
        // with viewport-relative controls, but it must never inherit their
        // non-uniform geometry or fall back to its authored pixel size.
        return std::min(horizontalScale, verticalScale);
    }

    float borderScale() const override {
        return _options.guiBorderScale;
    }

    float listScale() const override {
        return _options.guiListScale;
    }

    bool imageButtonTextPulses() const override {
        return _imageButtonTextPulses;
    }

    float pulsingAlpha() const override {
        return _clock.value();
    }

    float arrowPulsingAlpha() const override {
        return _clock.arrowValue();
    }

    double textureTime() const override {
        return _clock.textureTime();
    }

    const FillRules &fillRules() const override {
        return _fillRules;
    }

    bool sceneRenderEnabled() const override {
        return _options.sceneRender;
    }

    void setControlScaling(const std::string &tag, ScalingMode scaling) override {
        _scalingByControlTag[tag] = scaling;
    }

    void setSelectedTextColor(glm::vec3 color) override {
        _selectedTextColor = color;
    }

    void setBackground(std::shared_ptr<graphics::Texture> texture) override;

    void refreshLayout() override {
        applyLayout();
    }

    std::unique_ptr<Control> newControl(ControlType type, std::string tag) override;

    void addControlToFront(std::shared_ptr<Control> control, ControlCoordinates coordinates) override;
    void addControlToBack(std::shared_ptr<Control> control, ControlCoordinates coordinates) override;

    std::shared_ptr<Control> findControl(const std::string &tag) const override;
    int unusedControlId() const override;
    std::optional<std::reference_wrapper<Control>> findControlById(int id) const override;

    bool hasSelectableControlAt(int x, int y) const override;

private:
    graphics::GraphicsOptions &_options;

    IGUIEventListener *_eventListener {nullptr};

    int _resolutionX {kDefaultResolutionX};
    int _resolutionY {kDefaultResolutionY};
    ScalingMode _scaling {ScalingMode::Center};
    std::optional<float> _textScale;
    bool _imageButtonTextPulses {false};
    /** How the controls draw their borders and fills. */
    FillRules _fillRules {};
    glm::ivec2 _rootOffset {0};
    glm::ivec2 _controlOffset {0};
    std::shared_ptr<graphics::Texture> _background;
    std::unordered_map<std::string, ScalingMode> _scalingByControlTag;
    /** Where the pointer last moved to, in screen coordinates. */
    glm::ivec2 _pointerPosition {0};

    // Controls

    std::vector<std::shared_ptr<Control>> _controls;
    /** The controls added in screen coordinates, which their owners place. */
    std::unordered_set<std::shared_ptr<Control>> _screenControls;
    /** The controls by tag; of two with one tag, the first loaded. */
    std::unordered_map<std::string, std::shared_ptr<Control>> _tagToControl;
    std::unordered_map<std::string, std::vector<std::reference_wrapper<Control>>> _controlTagToChildren;

    std::optional<std::reference_wrapper<Control>> _rootControl;
    std::optional<std::reference_wrapper<Control>> _selection;
    /** The control holding the keyboard. */
    std::optional<std::reference_wrapper<Control>> _activeControl;
    /** The list the pointer is on has been entered since the pointer came onto it. */
    bool _listEntered {false};
    /** Whether the hilight of a control that takes the keyboard on hover, other than a list, follows the keyboard. */
    bool _hilightFollowsKeyboard {false};
    /** The wheel's turns gathered into whole steps for the control under the pointer. */
    input::WheelSteps _wheelSteps;
    /** Whether a held arrow key wrapped one of the lists round (keyRepeatStopped). */
    bool _keyRepeatStopped {false};
    /** Whether the arrow keys pass over a hidden control too (setArrowWalkVisibleOnly). */
    bool _arrowWalkVisibleOnly {false};
    /** The control the left button was pressed on, which keeps the mouse until the release. */
    std::optional<std::reference_wrapper<Control>> _mouseCapture;

    // END Controls

    // Services

    scene::ISceneGraphs &_sceneGraphs;
    graphics::GraphicsServices &_graphicsSvc;
    resource::ResourceServices &_resourceSvc;
    const GUIClock &_clock;
    Tooltip &_tooltip;

    // END Services

    // GUI Colors

    std::optional<glm::vec3> _selectedTextColor;

    // END GUI Colors

    void loadControl(const resource::generated::GUI_CONTROLS &gui);

    void onClick(const std::string &control) {
        if (_eventListener) {
            _eventListener->onClick(control);
        }
    }

    void onSelectionChanged(const std::string &control, bool selected) {
        if (_eventListener) {
            _eventListener->onSelectionChanged(control, selected);
        }
    }

    void onEntered(const std::string &control) {
        if (_eventListener) {
            _eventListener->onEntered(control);
        }
    }

    void positionRelativeToCenter(Control &control);
    void scaleRelativeToCenter(Control &control);
    void applyControlLayout(Control &control);
    void applyLayout();
    ScalingMode controlScaling(const Control &control) const;
    glm::ivec2 renderOffset(const Control &control) const;
    glm::ivec2 controlCoordinates(const Control &control, int screenX, int screenY) const;
    float scaledFactor() const;
    void updateSelection(int screenX, int screenY);
    /**
     * The pointer is on \p control, or on none, when the hilight follows the
     * keyboard: a control that takes it on hover, other than a list, takes
     * the keyboard, on every move; the control the pointer comes onto is
     * hilighted; the control it leaves keeps its hilight while that follows
     * the keyboard, unless the pointer came onto another such control.
     */
    void followPointer(const std::optional<std::reference_wrapper<Control>> &control);
    /**
     * Whether the pointer stays on the control it was on: \p control is that
     * same control, compared by address since layout IDs repeat, or both
     * are none.
     */
    bool isHovered(const std::optional<std::reference_wrapper<Control>> &control) const;
    /** The pointer is on \p control, or on none: no list row is entered yet and the wheel's turns gather afresh. */
    void setHovered(const std::optional<std::reference_wrapper<Control>> &control);
    /**
     * The pointer comes onto \p control: unless it is already hilighted, it
     * is hilighted and entered, a list only by its rows.
     */
    void enterHovered(Control &control);
    /** Whether the hilight of \p control follows the keyboard. */
    bool hilightFollowsKeyboard(const Control &control) const;
    /** \p control is hilighted, which it is told of. */
    void hilight(Control &control);
    /** \p control loses its hilight, which it is told of, if it has it. */
    void unhilight(Control &control);
    /**
     * No control holds the keyboard; the one holding it loses its hilight
     * on a GUI whose hilight follows the keyboard, unless it is a list, which
     * draws its selected row without its hilight.
     */
    void releaseKeyboard();
    /**
     * The keyboard moves along \p control's link of \p direction: the
     * control reached sounds as it is entered, takes the keyboard and is
     * selected. Nothing changes where the links lead to no control.
     */
    void moveKeyboard(const Control &control, Control::MoveDirection direction);
    /** A list the pointer acted on is entered when the pointer moved its selection to another row. */
    void enterListSelection(Control &control);
    /**
     * A list the pointer moves on at \p coords, control-space, is entered when
     * the pointer moved its selection to another row, or once when it reaches
     * a row while the list does not hold the keyboard, which a list taking
     * the keyboard on hover then takes.
     */
    void enterList(Control &control, const glm::ivec2 &coords);
    /** Whether \p control holds the keyboard. */
    bool holdsKeyboard(const Control &control) const;

    void renderBackground(scene::IRenderPass &pass);

    std::optional<std::reference_wrapper<Control>> findControlAt(int x, int y,
                                                                 const std::function<bool(const Control &)> &test) const;

    // User input

    virtual bool handleGamepadButtonDown(const input::GamepadButtonEvent &event);
    virtual bool handleGamepadButtonUp(const input::GamepadButtonEvent &event);
    virtual bool handleGamepadAxisMotion(const input::GamepadAxisMotionEvent &event);
    /**
     * A key goes to the control holding the keyboard. A list moves its
     * selection a row on Up and Down, sounding as it moves unless Up selects
     * its first row from none, and acts on its selected row on Return or
     * keypad Enter as the click that activates the row does, with its sound.
     * A button takes Return or keypad Enter as a click, with the click's
     * sound, and a chart as the choice of its focused icon; a slider steps
     * on the arrows along it; a chart moves its focus on the arrows,
     * sounding as it steps to the side; and an arrow the control does not
     * take moves the keyboard along the control's link of its direction.
     *
     * @return whether the key was one of these and a control holds the keyboard
     */
    virtual bool handleKeyDown(const input::KeyEvent &key);
    virtual bool handleKeyUp(input::KeyCode key);

    // END User input
};

} // namespace gui

} // namespace reone
