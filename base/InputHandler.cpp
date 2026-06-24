/*
* Unified input handler implementation
* Copyright (C) 2026 - MIT License
*/

#include "InputHandler.h"
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/input.h>
#include <android/native_activity.h>
#endif

namespace vks {

InputHandler::InputHandler() { initCursors(); }
InputHandler::~InputHandler() { destroyCursors(); }

void InputHandler::initCursors()
{
#if defined(_WIN32)
    cursor_handles_[CURSOR_DEFAULT]     = LoadCursor(nullptr, IDC_ARROW);
    cursor_handles_[CURSOR_MOVE]        = LoadCursor(nullptr, IDC_SIZEALL);
    cursor_handles_[CURSOR_POINTER]     = LoadCursor(nullptr, IDC_HAND);
    cursor_handles_[CURSOR_RESIZE]      = LoadCursor(nullptr, IDC_SIZENWSE);
    cursor_handles_[CURSOR_CROSS]       = LoadCursor(nullptr, IDC_CROSS);
    cursor_handles_[CURSOR_TEXT]        = LoadCursor(nullptr, IDC_IBEAM);
    cursor_handles_[CURSOR_UNAVAILABLE] = LoadCursor(nullptr, IDC_NO);
#endif
}

void InputHandler::destroyCursors() {}

void InputHandler::applyCursor(int type)
{
#if defined(_WIN32)
    HWND hwnd = (HWND)platform_window;
    if (hwnd && cursor_handles_[type])
        SetCursor((HCURSOR)cursor_handles_[type]);
#endif
}

void InputHandler::SetMouseCursor(int cursor_type) { applyCursor(cursor_type); }

void InputHandler::SetMouseCursor(const std::string& name)
{
    if (name.empty() || name == "arrow") SetMouseCursor(CURSOR_DEFAULT);
    else if (name == "move") SetMouseCursor(CURSOR_MOVE);
    else if (name == "pointer") SetMouseCursor(CURSOR_POINTER);
    else if (name == "resize") SetMouseCursor(CURSOR_RESIZE);
    else if (name == "cross") SetMouseCursor(CURSOR_CROSS);
    else if (name == "text") SetMouseCursor(CURSOR_TEXT);
    else if (name == "unavailable") SetMouseCursor(CURSOR_UNAVAILABLE);
}

void InputHandler::SetClipboardText(const std::string& text)
{
#if defined(_WIN32)
    HWND hwnd = (HWND)platform_window;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hglb) {
        char* p = (char*)GlobalLock(hglb);
        memcpy(p, text.c_str(), text.size() + 1);
        GlobalUnlock(hglb);
        SetClipboardData(CF_TEXT, hglb);
    }
    CloseClipboard();
#endif
}

std::string InputHandler::GetClipboardText()
{
#if defined(_WIN32)
    std::string result;
    HWND hwnd = (HWND)platform_window;
    if (!OpenClipboard(hwnd)) return result;
    HANDLE hglb = GetClipboardData(CF_TEXT);
    if (hglb) {
        char* p = (char*)GlobalLock(hglb);
        if (p) { result = p; GlobalUnlock(hglb); }
    }
    CloseClipboard();
    return result;
#else
    return "";
#endif
}

void InputHandler::ActivateKeyboard(int cx, int cy, int cw, int ch)
{
#if defined(_WIN32)
    HWND hwnd = (HWND)platform_window;
    if (!hwnd) return;
    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        COMPOSITIONFORM cf = {};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = cx;
        cf.ptCurrentPos.y = cy;
        ImmSetCompositionWindow(himc, &cf);
        ImmReleaseContext(hwnd, himc);
    }
#endif
    (void)cx; (void)cy; (void)cw; (void)ch;
}

void InputHandler::DeactivateKeyboard()
{
#if defined(_WIN32)
    HWND hwnd = (HWND)platform_window;
    if (!hwnd) return;
    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        ImmAssociateContext(hwnd, NULL);
        ImmReleaseContext(hwnd, himc);
    }
#endif
}

void InputHandler::ProcessMouseMove(float x, float y) { if (onMouseMove) onMouseMove(x, y); }
void InputHandler::ProcessMouseButtonDown(int b) { if (onMouseButtonDown) onMouseButtonDown(b); }
void InputHandler::ProcessMouseButtonUp(int b) { if (onMouseButtonUp) onMouseButtonUp(b); }

void InputHandler::ProcessMouseWheel(float dx, float dy)
{
    state_.wheel_delta.horizontal += dx;
    state_.wheel_delta.vertical += dy;
    if (onMouseWheel) onMouseWheel(dx, dy);
}

void InputHandler::ProcessMouseLeave()
{
    state_.mouse_inside_window = false;
    if (onMouseLeave) onMouseLeave();
}

void InputHandler::ProcessKeyDown(int key, int mod)
{
    state_.modifiers = mod;
    if (key >= 0 && key < Key::COUNT) state_.keys[key] = true;
    if (onKeyDown) onKeyDown(key, mod);
}

void InputHandler::ProcessKeyUp(int key, int mod)
{
    state_.modifiers = mod;
    if (key >= 0 && key < Key::COUNT) state_.keys[key] = false;
    if (onKeyUp) onKeyUp(key, mod);
}

void InputHandler::ProcessTextInput(const std::string& text)
{
    state_.text_input_buffer += text;
    if (onTextInput) onTextInput(text);
}

void InputHandler::ProcessTouchStart(const std::vector<TouchPoint>& touches)
{
    for (const auto& t : touches)
        if (std::find_if(state_.touches.begin(), state_.touches.end(),
            [&](const TouchPoint& e) { return e.id == t.id; }) == state_.touches.end())
            state_.touches.push_back(t);
    if (onTouchEvent) onTouchEvent(0, touches);
}

void InputHandler::ProcessTouchMove(const std::vector<TouchPoint>& touches)
{
    for (const auto& t : touches) {
        auto it = std::find_if(state_.touches.begin(), state_.touches.end(),
            [&](const TouchPoint& e) { return e.id == t.id; });
        if (it != state_.touches.end()) { it->x = t.x; it->y = t.y; }
    }
    if (onTouchEvent) onTouchEvent(1, touches);
}

void InputHandler::ProcessTouchEnd(const std::vector<TouchPoint>& touches)
{
    for (const auto& t : touches)
        state_.touches.erase(std::remove_if(state_.touches.begin(), state_.touches.end(),
            [&](const TouchPoint& e) { return e.id == t.id; }), state_.touches.end());
    if (onTouchEvent) onTouchEvent(2, touches);
}

void InputHandler::ProcessResize(int w, int h) { if (onResize) onResize(w, h); }

void InputHandler::ProcessDpiChanged(float s)
{
    state_.dpi_scale = s;
    if (onDpiChanged) onDpiChanged(s);
}

void InputHandler::ProcessImeComposition(const std::string& text, int start, int len)
{
    ime_state_.composing = true;
    ime_state_.composition_text = text;
    ime_state_.composition_start = start;
    ime_state_.composition_length = len;
    if (onImeComposition) onImeComposition(text);
}

void InputHandler::ProcessImeCommit(const std::string& text)
{
    ime_state_.composing = false;
    ime_state_.composition_text.clear();
    ProcessTextInput(text);
}

void InputHandler::ProcessImeEnd()
{
    ime_state_.composing = false;
    ime_state_.composition_text.clear();
    ime_state_.composition_start = 0;
    ime_state_.composition_length = 0;
}

void InputHandler::ResetFrameState()
{
    state_.wheel_delta.vertical = 0.0f;
    state_.wheel_delta.horizontal = 0.0f;
    state_.text_input_buffer.clear();
}

// Key code conversion
int InputHandler::ConvertKeyCode(uintptr_t native_keycode, uintptr_t flags)
{
#if defined(_WIN32)
    WPARAM vk = (WPARAM)native_keycode;
    switch (vk) {
    case 'A': return Key::A; case 'B': return Key::B; case 'C': return Key::C;
    case 'D': return Key::D; case 'E': return Key::E; case 'F': return Key::F;
    case 'G': return Key::G; case 'H': return Key::H; case 'I': return Key::I;
    case 'J': return Key::J; case 'K': return Key::K; case 'L': return Key::L;
    case 'M': return Key::M; case 'N': return Key::N; case 'O': return Key::O;
    case 'P': return Key::P; case 'Q': return Key::Q; case 'R': return Key::R;
    case 'S': return Key::S; case 'T': return Key::T; case 'U': return Key::U;
    case 'V': return Key::V; case 'W': return Key::W; case 'X': return Key::X;
    case 'Y': return Key::Y; case 'Z': return Key::Z;
    case '0': return Key::NUM_0; case '1': return Key::NUM_1; case '2': return Key::NUM_2;
    case '3': return Key::NUM_3; case '4': return Key::NUM_4; case '5': return Key::NUM_5;
    case '6': return Key::NUM_6; case '7': return Key::NUM_7; case '8': return Key::NUM_8;
    case '9': return Key::NUM_9;
    case VK_F1: return Key::F1; case VK_F2: return Key::F2; case VK_F3: return Key::F3;
    case VK_F4: return Key::F4; case VK_F5: return Key::F5; case VK_F6: return Key::F6;
    case VK_F7: return Key::F7; case VK_F8: return Key::F8; case VK_F9: return Key::F9;
    case VK_F10: return Key::F10; case VK_F11: return Key::F11; case VK_F12: return Key::F12;
    case VK_ESCAPE: return Key::ESCAPE;
    case VK_RETURN: return Key::ENTER;
    case VK_TAB: return Key::TAB;
    case VK_BACK: return Key::BACKSPACE;
    case VK_INSERT: return Key::INSERT_KEY;
    case VK_DELETE: return Key::DEL_KEY;
    case VK_HOME: return Key::HOME;
    case VK_END: return Key::END;
    case VK_PRIOR: return Key::PAGEUP;
    case VK_NEXT: return Key::PAGEDOWN;
    case VK_LEFT: return Key::LEFT;
    case VK_RIGHT: return Key::RIGHT;
    case VK_UP: return Key::UP;
    case VK_DOWN: return Key::DOWN;
    case VK_SPACE: return Key::SPACE_KEY;
    case VK_CAPITAL: return Key::CAPSLOCK;
    case VK_SCROLL: return Key::SCROLLLOCK;
    case VK_NUMLOCK: return Key::NUMLOCK;
    case VK_SNAPSHOT: return Key::PRINTSCREEN;
    case VK_PAUSE: return Key::PAUSE_KEY;
    case VK_OEM_1: return Key::SEMICOLON;
    case VK_OEM_COMMA: return Key::COMMA;
    case VK_OEM_PERIOD: return Key::PERIOD;
    case VK_OEM_2: return Key::SLASH;
    case VK_OEM_5: return Key::BACKSLASH;
    case VK_OEM_4: return Key::LBRACKET;
    case VK_OEM_6: return Key::RBRACKET;
    case VK_OEM_7: return Key::APOSTROPHE;
    case VK_OEM_3: return Key::GRAVE;
    case VK_OEM_MINUS: return Key::MINUS_KEY;
    case VK_OEM_PLUS: return Key::EQUALS;
    case VK_NUMPAD0: return Key::KP_0; case VK_NUMPAD1: return Key::KP_1;
    case VK_NUMPAD2: return Key::KP_2; case VK_NUMPAD3: return Key::KP_3;
    case VK_NUMPAD4: return Key::KP_4; case VK_NUMPAD5: return Key::KP_5;
    case VK_NUMPAD6: return Key::KP_6; case VK_NUMPAD7: return Key::KP_7;
    case VK_NUMPAD8: return Key::KP_8; case VK_NUMPAD9: return Key::KP_9;
    case VK_MULTIPLY: return Key::KP_MUL;
    case VK_ADD: return Key::KP_ADD;
    case VK_SUBTRACT: return Key::KP_SUB;
    case VK_DIVIDE: return Key::KP_DIV;
    case VK_DECIMAL: return Key::KP_DOT;
    case VK_LSHIFT: return Key::LSHIFT;
    case VK_RSHIFT: return Key::RSHIFT;
    case VK_LCONTROL: return Key::LCTRL;
    case VK_RCONTROL: return Key::RCTRL;
    case VK_LMENU: return Key::LALT;
    case VK_RMENU: return Key::RALT;
    case VK_LWIN: return Key::LSUPER;
    case VK_RWIN: return Key::RSUPER;
    default: return Key::UNKNOWN;
    }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    int32_t kc = (int32_t)native_keycode;
    switch (kc) {
    case AKEYCODE_A: return Key::A; case AKEYCODE_B: return Key::B;
    case AKEYCODE_C: return Key::C; case AKEYCODE_D: return Key::D;
    case AKEYCODE_E: return Key::E; case AKEYCODE_F: return Key::F;
    case AKEYCODE_G: return Key::G; case AKEYCODE_H: return Key::H;
    case AKEYCODE_I: return Key::I; case AKEYCODE_J: return Key::J;
    case AKEYCODE_K: return Key::K; case AKEYCODE_L: return Key::L;
    case AKEYCODE_M: return Key::M; case AKEYCODE_N: return Key::N;
    case AKEYCODE_O: return Key::O; case AKEYCODE_P: return Key::P;
    case AKEYCODE_Q: return Key::Q; case AKEYCODE_R: return Key::R;
    case AKEYCODE_S: return Key::S; case AKEYCODE_T: return Key::T;
    case AKEYCODE_U: return Key::U; case AKEYCODE_V: return Key::V;
    case AKEYCODE_W: return Key::W; case AKEYCODE_X: return Key::X;
    case AKEYCODE_Y: return Key::Y; case AKEYCODE_Z: return Key::Z;
    case AKEYCODE_0: return Key::NUM_0; case AKEYCODE_9: return Key::NUM_9;
    case AKEYCODE_ESCAPE: return Key::ESCAPE;
    case AKEYCODE_ENTER: return Key::ENTER;
    case AKEYCODE_DEL: return Key::BACKSPACE;
    case AKEYCODE_FORWARD_DEL: return Key::DEL_KEY;
    case AKEYCODE_SPACE: return Key::SPACE_KEY;
    case AKEYCODE_DPAD_LEFT: return Key::LEFT;
    case AKEYCODE_DPAD_RIGHT: return Key::RIGHT;
    case AKEYCODE_DPAD_UP: return Key::UP;
    case AKEYCODE_DPAD_DOWN: return Key::DOWN;
    default: return Key::UNKNOWN;
    }
#else
    (void)native_keycode; (void)flags;
    return Key::UNKNOWN;
#endif
}

int InputHandler::GetModifierState()
{
#if defined(_WIN32)
    int m = KM_NONE;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= KM_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= KM_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) m |= KM_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= KM_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1) m |= KM_CAPS;
    if (GetKeyState(VK_NUMLOCK) & 1) m |= KM_NUM;
    return m;
#else
    return KM_NONE;
#endif
}

} // namespace vks
