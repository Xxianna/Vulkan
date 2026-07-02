# gltfloading_rmlui_webgl 移植注意事项

## 1. Emscripten shell-file 模板使用

`--shell-file` 必须通过 `target_link_options` 传递，**不能**放在 `target_link_libraries` 中。

`target_link_libraries` 会将参数当作库名处理，加上 `-l` 前缀传给链接器，导致：
```
wasm-ld: error: unable to find library -lSHELL:--shell-file
wasm-ld: error: unknown file type: .../template.html
```

正确写法：
```cmake
target_link_options(${TARGET} PRIVATE
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/template.html"
)
```

`SHELL:` 前缀告诉 CMake 不要拆分参数，原样传给链接命令行。此语法仅在 `target_link_options` 中生效。

## 2. 浏览器滚轮事件链路

Emscripten 的 SDL2 移植**可能不会**将浏览器的 `wheel` 事件翻译为 `SDL_MOUSEWHEEL`。即使 canvas 设置了 `tabindex=-1`，SDL 的 wheel 事件注册目标可能是 `document` 而非 `canvas`，导致事件丢失。

### 解决方案：使用 Emscripten HTML5 API 直接捕获

```cpp
emscripten_set_wheel_callback("#canvas", nullptr, true, em_wheel_callback);
```

回调中根据 `deltaMode` 归一化 delta 值：
- `DOM_DELTA_PIXEL`（Chrome/Firefox 默认）：`deltaY` 单位为像素，典型值 ±100~200，乘以 0.01 归一化
- `DOM_DELTA_LINE`：`deltaY` 为行数，典型值 ±3，直接使用
- `DOM_DELTA_PAGE`：`deltaY` 为页数，典型值 ±1

### 避免双重处理

浏览器的同一个 wheel 事件可能同时触发：
1. Emscripten SDL2 内部处理器 → 生成 `SDL_MOUSEWHEEL`
2. `emscripten_set_wheel_callback` 注册的回调

两者在同一帧内都会触发，导致滚轮效果翻倍。**必须在 Emscripten 构建中禁用 `SDL_MOUSEWHEEL` 处理**，只保留 Emscripten 回调路径：

```cpp
case SDL_MOUSEWHEEL:
#ifndef __EMSCRIPTEN__
    // 非 Emscripten 平台正常处理
    processMouseWheel(ev.wheel.y);
#endif
    break;
```

## 3. RmlUi ProcessMouseWheel 的静默丢弃

`Rml::Context::ProcessMouseWheel` 内部逻辑：

```cpp
if (!hover) {
    scroll_controller->Reset();
    return true;  // 静默丢弃，不做任何事
}
Element* target = hover->GetClosestScrollableContainer();
// 如果 target 为 nullptr，smoothscroll 激活但无效果
```

关键问题：
- **`hover` 为 null**：如果 `ProcessMouseMove` 未先被调用（或坐标不在任何元素上），`hover` 为空，滚轮事件被直接丢弃
- **无可滚动容器**：即使 `hover` 有效，如果 overlay 的 RCSS 中没有设置 `overflow: auto/scroll` 的元素，`GetClosestScrollableContainer()` 返回 null，事件被"消费"（返回 false）但不产生任何效果

### 返回值语义

| 返回值 | 含义 |
|--------|------|
| `false` | 事件被 RmlUi 处理（但不代表产生了可见的滚动效果） |
| `true` | 事件未被 RmlUi 处理（hover 为 null，或不在 Autoscroll/Smoothscroll 模式） |

### 解决方案：未消费则转发给相机

```cpp
if (!rmlui_passthrough) {
    if (rmluiOverlay.processMouseWheel(delta))
        camera.zoom(delta);  // RmlUi 未消费 → 给相机
} else {
    camera.zoom(delta);
}
```

这确保了：
- 鼠标在可滚动 RmlUi 面板上 → RmlUi 滚动
- 鼠标在透明区域或无可滚动元素上 → 相机缩放（与桌面版行为一致）

## 4. Canvas 焦点与页面布局

### 焦点

canvas 必须获得焦点才能接收键盘和滚轮输入。`tabindex=-1` 使 canvas 可编程聚焦（不可 Tab 导航），但需要主动调用 `canvas.focus()`：

```javascript
canvas.focus();
canvas.addEventListener('click', function() { canvas.focus(); });
```

### 页面布局

如果页面有其他元素（进度条、文本框等），浏览器可能将 wheel 事件用于页面滚动而非转发给 canvas。最简单的解决方式是让 canvas 充满整个窗口：

```css
html, body {
    margin: 0; padding: 0;
    width: 100%; height: 100%;
    overflow: hidden;
}
canvas { display: block; width: 100%; height: 100%; }
```

日志输出改为 `console.log`，不在页面上显示 textarea。

## 5. 中键（滚轮按下）平移

原始代码只处理了 `SDL_BUTTON_LEFT`，中键完全被忽略。桌面版中键行为（`vulkanexamplebase.cpp`）：

```cpp
if (mouseState.buttons.middle) {
    camera.translate(glm::vec3(-dx * 0.005f, -dy * 0.005f, 0.0f));
}
```

WebGL 版需要：
1. 添加 `middleMouseDown` 状态跟踪
2. 在 `SDL_MOUSEBUTTONDOWN/UP` 中处理 `SDL_BUTTON_MIDDLE`
3. 在 `SDL_MOUSEMOTION` 中，中键按下时调用 `camera.translate(dx, dy)`
4. `SimpleCamera` 需要新增 `translate` 方法，沿相机右向量在 XZ 平面平移 target，Y 轴直接上下移动

注意平移方向：鼠标右拖 → target 右移 → 相机右移 → 场景视觉左移。如果方向反了，翻转 dx 的符号即可。
