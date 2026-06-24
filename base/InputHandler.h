/*
* Unified input handler for the Vulkan example base
* Interface design references the SDL platform layer patterns.
* Copyright (C) 2026 - MIT License
*/

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <cstring>

namespace vks {

struct TouchPoint {
    int32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
};

// Key modifiers
static constexpr int KM_NONE  = 0;
static constexpr int KM_SHIFT = 1 << 0;
static constexpr int KM_CTRL  = 1 << 1;
static constexpr int KM_ALT   = 1 << 2;
static constexpr int KM_SUPER = 1 << 3;
static constexpr int KM_CAPS  = 1 << 4;
static constexpr int KM_NUM   = 1 << 5;

// Key identifiers - anonymous enum inside namespace avoids Windows macro conflicts
namespace Key {
    enum : int {
        UNKNOWN = 0,
        // Letters
        A = 0x41, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        // Numbers
        NUM_0 = 0x30, NUM_1, NUM_2, NUM_3, NUM_4, NUM_5, NUM_6, NUM_7, NUM_8, NUM_9,
        // Function keys
        F1 = 0x100, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
        // Special keys
        ESCAPE = 0x200, ENTER, TAB, BACKSPACE,
        INSERT_KEY, DEL_KEY, HOME, END, PAGEUP, PAGEDOWN,
        LEFT, RIGHT, UP, DOWN,
        SPACE_KEY, CAPSLOCK, SCROLLLOCK, NUMLOCK,
        PRINTSCREEN, PAUSE_KEY,
        // Punctuation
        SEMICOLON, COMMA, PERIOD, SLASH, BACKSLASH,
        LBRACKET, RBRACKET, APOSTROPHE, GRAVE,
        MINUS_KEY, EQUALS,
        // Numpad
        KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9,
        KP_ENTER, KP_ADD, KP_SUB, KP_MUL, KP_DIV, KP_DOT,
        // Modifiers
        LSHIFT, RSHIFT, LCTRL, RCTRL,
        LALT, RALT, LSUPER, RSUPER,
        COUNT
    };
}

// Mouse buttons (renamed to avoid Windows MB_RIGHT macro conflict)
static constexpr int MOUSE_LEFT = 0;
static constexpr int MOUSE_RIGHT = 1;
static constexpr int MOUSE_MIDDLE = 2;

// Cursor types
static constexpr int CURSOR_DEFAULT = 0;
static constexpr int CURSOR_MOVE = 1;
static constexpr int CURSOR_POINTER = 2;
static constexpr int CURSOR_RESIZE = 3;
static constexpr int CURSOR_CROSS = 4;
static constexpr int CURSOR_TEXT = 5;
static constexpr int CURSOR_UNAVAILABLE = 6;

struct InputState {
    bool mouse_inside_window = true;
    struct { float vertical = 0.0f; float horizontal = 0.0f; } wheel_delta;
    std::vector<TouchPoint> touches;
    std::string text_input_buffer;
    float dpi_scale = 1.0f;
    bool keys[Key::COUNT] = {};
    int modifiers = KM_NONE;
};

struct ImeState {
    bool composing = false;
    std::string composition_text;
    int composition_start = 0;
    int composition_length = 0;
};

class InputHandler {
public:
    InputHandler();
    ~InputHandler();

    void SetMouseCursor(int cursor_type);
    void SetMouseCursor(const std::string& cursor_name);
    void SetClipboardText(const std::string& text);
    std::string GetClipboardText();
    void ActivateKeyboard(int x, int y, int w, int h);
    void DeactivateKeyboard();

    void ProcessMouseMove(float x, float y);
    void ProcessMouseButtonDown(int button);
    void ProcessMouseButtonUp(int button);
    void ProcessMouseWheel(float dx, float dy);
    void ProcessMouseLeave();
    void ProcessKeyDown(int key, int modifiers);
    void ProcessKeyUp(int key, int modifiers);
    void ProcessTextInput(const std::string& text);
    void ProcessTouchStart(const std::vector<TouchPoint>& touches);
    void ProcessTouchMove(const std::vector<TouchPoint>& touches);
    void ProcessTouchEnd(const std::vector<TouchPoint>& touches);
    void ProcessResize(int w, int h);
    void ProcessDpiChanged(float scale);
    void ProcessImeComposition(const std::string& text, int start, int len);
    void ProcessImeCommit(const std::string& text);
    void ProcessImeEnd();

    const InputState& GetState() const { return state_; }
    const ImeState& GetImeState() const { return ime_state_; }
    void ResetFrameState();

    void* platform_window = nullptr;

    std::function<void(float, float)> onMouseMove;
    std::function<void(int)> onMouseButtonDown;
    std::function<void(int)> onMouseButtonUp;
    std::function<void(float, float)> onMouseWheel;
    std::function<void()> onMouseLeave;
    std::function<void(int, int)> onKeyDown;
    std::function<void(int, int)> onKeyUp;
    std::function<void(const std::string&)> onTextInput;
    std::function<void(int, const std::vector<TouchPoint>&)> onTouchEvent;
    std::function<void(int, int)> onResize;
    std::function<void(float)> onDpiChanged;
    std::function<void(const std::string&)> onImeComposition;

    static int ConvertKeyCode(uintptr_t native_keycode, uintptr_t flags = 0);
    static int GetModifierState();

private:
    InputState state_;
    ImeState ime_state_;
    void* cursor_handles_[7] = {};
    void initCursors();
    void destroyCursors();
    void applyCursor(int type);
};

} // namespace vks
