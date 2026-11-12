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


struct Keybind {
    input::KeyCode keyCode;
    input::KeyCode keyCodeAlt;
    input::GamepadButton gamepadButton;
    input::GamepadAxis gamepadAxis;

    bool check(input::KeyCode keyCode) {
        return this->keyCode == keyCode || keyCodeAlt == keyCode;
    }
    bool check(input::GamepadButton button) {
        return gamepadButton == button;
    }
    bool check(input::GamepadAxis axis) {
        return gamepadAxis == axis;
    }
};

struct KeybindOptions {
    Keybind moveForward {input::KeyCode::W, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftY};
    Keybind moveBackward {input::KeyCode::S, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftY};
    Keybind moveLeft {input::KeyCode::Z, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftX};
    Keybind moveRight {input::KeyCode::C, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftX};
    Keybind flourish {input::KeyCode::X, input::KeyCode::Unknown, input::GamepadButton::North, input::GamepadAxis::Invalid};
    Keybind walk {input::KeyCode::B, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind cameraRotateLeft {input::KeyCode::A, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::RightX};
    Keybind cameraRotateRight {input::KeyCode::D, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::RightX};
    Keybind cycleTargetLeft {input::KeyCode::Q, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftTrigger};
    Keybind cycleTargetRight {input::KeyCode::E, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::RightTrigger};
    Keybind toggleStealth {input::KeyCode::G, input::KeyCode::Unknown, input::GamepadButton::Back, input::GamepadAxis::Invalid};
    Keybind clearOneAction {input::KeyCode::Y, input::KeyCode::Unknown, input::GamepadButton::North, input::GamepadAxis::Invalid};
    Keybind cancelCombat {input::KeyCode::F, input::KeyCode::Unknown, input::GamepadButton::East, input::GamepadAxis::Invalid};
    Keybind defaultAction {input::KeyCode::R, input::KeyCode::Unknown, input::GamepadButton::South, input::GamepadAxis::Invalid};
    Keybind pause {input::KeyCode::Space, input::KeyCode::Pause, input::GamepadButton::LeftShoulder, input::GamepadAxis::Invalid};
    Keybind changeCharacter {input::KeyCode::Tab, input::KeyCode::Unknown, input::GamepadButton::RightShoulder, input::GamepadAxis::Invalid};
    Keybind freeLook {input::KeyCode::CapsLock, input::KeyCode::Unknown, input::GamepadButton::LeftStick, input::GamepadAxis::Invalid};
    Keybind quickSave {input::KeyCode::F4, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind quickLoad {input::KeyCode::F5, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiNavigateScrollUp {input::KeyCode::Unknown, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::RightY};
    Keybind guiNavigateScrollDown {input::KeyCode::Unknown, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::RightY};
    Keybind guiNavigateUp {input::KeyCode::Up, input::KeyCode::Unknown, input::GamepadButton::DPadUp, input::GamepadAxis::LeftY};
    Keybind guiNavigateDown {input::KeyCode::Down, input::KeyCode::Unknown, input::GamepadButton::DPadDown, input::GamepadAxis::LeftY};
    Keybind guiNavigateLeft {input::KeyCode::Left, input::KeyCode::Unknown, input::GamepadButton::DPadLeft, input::GamepadAxis::LeftX};
    Keybind guiNavigateRight {input::KeyCode::Right, input::KeyCode::Unknown, input::GamepadButton::DPadRight, input::GamepadAxis::LeftX};
    Keybind guiNavigateExit {input::KeyCode::Escape, input::KeyCode::Unknown, input::GamepadButton::East, input::GamepadAxis::Invalid};
    Keybind guiNavigateConfirm {input::KeyCode::Return, input::KeyCode::KeypadEnter, input::GamepadButton::South, input::GamepadAxis::Invalid};
    Keybind guiNavigateAlt1 {input::KeyCode::Unknown, input::KeyCode::Unknown, input::GamepadButton::West, input::GamepadAxis::Invalid};
    Keybind guiNavigateAlt2 {input::KeyCode::Unknown, input::KeyCode::Unknown, input::GamepadButton::North, input::GamepadAxis::Invalid};
    Keybind guiMenuMessages {input::KeyCode::J, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuMap {input::KeyCode::M, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuQuest {input::KeyCode::L, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuSkillsFeats {input::KeyCode::K, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuOptions {input::KeyCode::O, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuCharacter {input::KeyCode::P, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuInventory {input::KeyCode::I, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiMenuEquipment {input::KeyCode::U, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiNextMenu {input::KeyCode::E, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind guiPreviousMenu {input::KeyCode::Q, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::Invalid};
    Keybind mgMoveFoward {input::KeyCode::W, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftY};
    Keybind mgMoveBackward {input::KeyCode::S, input::KeyCode::Unknown, input::GamepadButton::Invalid, input::GamepadAxis::LeftY};
    Keybind mgMoveLeft {input::KeyCode::A, input::KeyCode::Left, input::GamepadButton::Invalid, input::GamepadAxis::LeftX};
    Keybind mgMoveRight {input::KeyCode::D, input::KeyCode::Right, input::GamepadButton::Invalid, input::GamepadAxis::LeftX};
    Keybind mgAction {input::KeyCode::Space, input::KeyCode::Unknown, input::GamepadButton::South, input::GamepadAxis::Invalid};
    Keybind mgPause {input::KeyCode::Escape, input::KeyCode::Unknown, input::GamepadButton::LeftShoulder, input::GamepadAxis::Invalid};
};
   
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

    KeyEvent(bool pressed,
             KeyCode code,
             uint16_t mod,
             bool repeat) :
        pressed(pressed),
        code(code),
        mod(mod),
        repeat(repeat) {
    }
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
};

enum class EventType {
    KeyDown,
    KeyUp,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAxisMotion
};

struct Event {
    EventType type;
    union {
        KeyEvent key;
        MouseMotionEvent motion;
        MouseButtonEvent button;
        MouseWheelEvent wheel;
        GamepadButtonEvent gamepadButton;
        GamepadAxisMotionEvent gamepadAxisMotion;
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
};

} // namespace input

} // namespace reone
