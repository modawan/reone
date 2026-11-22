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

namespace reone {

namespace input {

enum class GamepadButton {
    Invalid = -1,
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Misc1,
    RightPaddle1,
    LeftPaddle1,
    RightPaddle2,
    LeftPaddle2,
    Touchpad,
    Misc2,
    Misc3,
    Misc4,
    Misc5,
    Misc6,
};

enum class GamepadAxis {
    Invalid = -1,
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

struct GamepadButtonEvent {
    uint32_t which;
    bool pressed;
    GamepadButton button;

    GamepadButtonEvent(uint32_t which,
                       bool pressed,
                       GamepadButton button) :
        which(which),
        pressed(pressed),
        button(button) {
    }
};

struct GamepadAxisMotionEvent {
    uint32_t which;
    float value;
    GamepadAxis axis;

    GamepadAxisMotionEvent(uint32_t which,
                           int16_t rawValue,
                           GamepadAxis axis) :
        which(which),
        value(rawValue < 0
                  ? static_cast<float>(rawValue) / 32768.0f
                  : static_cast<float>(rawValue) / 32767.0f),
        axis(axis) {
    }
};

enum class KeyCode {
    Unknown = 0,

    Return = '\r',
    Escape = '\x1B',
    Backspace = '\b',
    Tab = '\t',
    Space = ' ',
    Exclaim = '!',
    DoubleQuote = '"',
    Hash = '#',
    Percent = '%',
    Dollar = '$',
    Ampersand = '&',
    Quote = '\'',
    LeftParenthesis = '(',
    RightParenthesis = ')',
    Asterisk = '*',
    Plus = '+',
    Comma = ',',
    Minus = '-',
    Period = '.',
    Slash = '/',
    Key0 = '0',
    Key1 = '1',
    Key2 = '2',
    Key3 = '3',
    Key4 = '4',
    Key5 = '5',
    Key6 = '6',
    Key7 = '7',
    Key8 = '8',
    Key9 = '9',
    Colon = ':',
    Semicolon = ';',
    Less = '<',
    Equals = '=',
    Greater = '>',
    Question = '?',
    At = '@',

    LeftBracket = '[',
    Backslash = '\\',
    RightBracket = ']',
    Caret = '^',
    Underscore = '_',
    Backquote = '`',
    A = 'a',
    B = 'b',
    C = 'c',
    D = 'd',
    E = 'e',
    F = 'f',
    G = 'g',
    H = 'h',
    I = 'i',
    J = 'j',
    K = 'k',
    L = 'l',
    M = 'm',
    N = 'n',
    O = 'o',
    P = 'p',
    Q = 'q',
    R = 'r',
    S = 's',
    T = 't',
    U = 'u',
    V = 'v',
    W = 'w',
    X = 'x',
    Y = 'y',
    Z = 'z',

    CapsLock = 57 | (1 << 30),

    F1 = 58 | (1 << 30),
    F2 = 59 | (1 << 30),
    F3 = 60 | (1 << 30),
    F4 = 61 | (1 << 30),
    F5 = 62 | (1 << 30),
    F6 = 63 | (1 << 30),
    F7 = 64 | (1 << 30),
    F8 = 65 | (1 << 30),
    F9 = 66 | (1 << 30),
    F10 = 67 | (1 << 30),
    F11 = 68 | (1 << 30),
    F12 = 69 | (1 << 30),

    PrintScreen = 70 | (1 << 30),
    ScrollLock = 71 | (1 << 30),
    Pause = 72 | (1 << 30),
    Insert = 73 | (1 << 30),
    Home = 74 | (1 << 30),
    PageUp = 75 | (1 << 30),
    Delete = '\x7F',
    End = 77 | (1 << 30),
    PageDown = 78 | (1 << 30),
    KeypadEnter = 88 | (1 << 30),
    Right = 79 | (1 << 30),
    Left = 80 | (1 << 30),
    Down = 81 | (1 << 30),
    Up = 82 | (1 << 30),

    LeftControl = 224 | (1 << 30),
    LeftShift = 225 | (1 << 30),
    LeftAlt = 226 | (1 << 30),
    LeftGUI = 227 | (1 << 30),
    RightControl = 228 | (1 << 30),
    RightShift = 229 | (1 << 30),
    RightAlt = 230 | (1 << 30),
    RightGUI = 231 | (1 << 30)
};

struct KeyModifiers {
    static constexpr int leftShift = 1 << 0;
    static constexpr int rightShift = 1 << 1;
    static constexpr int leftControl = 1 << 6;
    static constexpr int rightControl = 1 << 7;
    static constexpr int leftAlt = 1 << 8;
    static constexpr int rightAlt = 1 << 9;
    static constexpr int leftGUI = 1 << 10;
    static constexpr int rightGUI = 1 << 11;
    static constexpr int capsLock = 1 << 13;

    static constexpr int control = leftControl | rightControl;
    static constexpr int shift = leftShift | rightShift;
    static constexpr int alt = leftAlt | rightAlt;
    static constexpr int gui = leftGUI | rightGUI;
};

struct KeyEvent {
    bool pressed;
    KeyCode code;
    uint16_t mod;
    bool repeat;
    /**
     * The character a key pressed types with the modifiers held, as a
     * Unicode code point, or 0 when it types none or the modifiers make a
     * shortcut.
     */
    uint32_t character;

    KeyEvent(bool pressed,
             KeyCode code,
             uint16_t mod,
             bool repeat,
             uint32_t character = 0) :
        pressed(pressed),
        code(code),
        mod(mod),
        repeat(repeat),
        character(character) {
    }
};

struct MouseMotionEvent {
    int32_t x;
    int32_t y;
    float xrel;
    float yrel;

    MouseMotionEvent(
        int32_t x,
        int32_t y,
        float xrel,
        float yrel) :
        x(x),
        y(y),
        xrel(xrel),
        yrel(yrel) {
    }
};

enum class MouseButton {
    Left = 1,
    Middle,
    Right,
    X1,
    X2
};

struct MouseButtonEvent {
    MouseButton button;
    bool pressed;
    uint8_t clicks;
    int32_t x;
    int32_t y;

    MouseButtonEvent(MouseButton button,
                     bool pressed,
                     uint8_t clicks,
                     int32_t x,
                     int32_t y) :
        button(button),
        pressed(pressed),
        clicks(clicks),
        x(x),
        y(y) {
    }
};

/** One character of text the system translated from the keyboard. */
struct TextInputEvent {
    /** The character's Unicode code point. */
    uint32_t codePoint;

    TextInputEvent(uint32_t codePoint) :
        codePoint(codePoint) {
    }
};

enum class MouseWheelDirection {
    Normal,
    Flipped
};

struct MouseWheelEvent {
    float x;
    float y;
    MouseWheelDirection direction;

    MouseWheelEvent(float x,
                    float y,
                    MouseWheelDirection direction) :
        x(x),
        y(y),
        direction(direction) {
    }

    /**
     * The notches the wheel itself turned up, or down when negative, with
     * the system's reversed ("natural") scrolling undone; may be a fraction.
     * For what the wheel moves physically, as a camera's zoom; content that
     * scrolls takes y as it is, the system's setting included.
     */
    float physicalNotchesY() const { return direction == MouseWheelDirection::Flipped ? -y : y; }
};

/**
 * Gathers a wheel's scrolling, which precise wheels and touch pads send in
 * fractions, into whole steps for content that scrolls: x and y as the
 * system reports them, so its reversed ("natural") scrolling applies as in
 * other applications. SDL accumulates whole ticks itself only from 3.2.12.
 */
class WheelSteps {
public:
    struct Steps {
        int x {0}; /**< steps right, or left when negative */
        int y {0}; /**< steps up, or down when negative */
    };

    /** Adds the event's scrolling and takes the whole steps gathered, keeping the rest for later events. */
    Steps take(const MouseWheelEvent &event) {
        _restX += event.x;
        _restY += event.y;
        Steps steps;
        steps.x = static_cast<int>(_restX);
        steps.y = static_cast<int>(_restY);
        _restX -= static_cast<float>(steps.x);
        _restY -= static_cast<float>(steps.y);
        return steps;
    }

private:
    float _restX {0.0f};
    float _restY {0.0f};
};

enum class EventType {
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAxisMotion,
    KeyDown,
    KeyUp,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    /** A character of text the system translated from the keyboard (TextInputEvent). */
    TextInput
};

struct Event {
    EventType type;
    union {
        GamepadButtonEvent gamepadButton;
        GamepadAxisMotionEvent gamepadAxisMotion;
        KeyEvent key;
        MouseMotionEvent motion;
        MouseButtonEvent button;
        MouseWheelEvent wheel;
        TextInputEvent text;
    };

    Event(EventType type) :
        type(type) {
    }

    inline static Event newGamepadButtonDown(GamepadButtonEvent gamepadButton) {
        Event event {EventType::GamepadButtonDown};
        event.gamepadButton = std::move(gamepadButton);
        return event;
    }

    inline static Event newGamepadButtonUp(GamepadButtonEvent gamepadButton) {
        Event event {EventType::GamepadButtonUp};
        event.gamepadButton = std::move(gamepadButton);
        return event;
    }

    inline static Event newGamepadAxisMotion(GamepadAxisMotionEvent gamepadAxisMotion) {
        Event event {EventType::GamepadAxisMotion};
        event.gamepadAxisMotion = std::move(gamepadAxisMotion);
        return event;
    }

    inline static Event newKeyDown(KeyEvent key) {
        Event event {EventType::KeyDown};
        event.key = std::move(key);
        return event;
    }

    inline static Event newKeyUp(KeyEvent key) {
        Event event {EventType::KeyUp};
        event.key = std::move(key);
        return event;
    }

    inline static Event newMouseMotion(MouseMotionEvent motion) {
        Event event {EventType::MouseMotion};
        event.motion = std::move(motion);
        return event;
    }

    inline static Event newMouseButtonDown(MouseButtonEvent button) {
        Event event {EventType::MouseButtonDown};
        event.button = std::move(button);
        return event;
    }

    inline static Event newMouseButtonUp(MouseButtonEvent button) {
        Event event {EventType::MouseButtonUp};
        event.button = std::move(button);
        return event;
    }

    inline static Event newMouseWheel(MouseWheelEvent wheel) {
        Event event {EventType::MouseWheel};
        event.wheel = std::move(wheel);
        return event;
    }

    /**
     * @param text the character the system translated from the keyboard
     * @return an event of type TextInput carrying it
     */
    inline static Event newTextInput(TextInputEvent text) {
        Event event {EventType::TextInput};
        event.text = std::move(text);
        return event;
    }
};

} // namespace input

} // namespace reone
