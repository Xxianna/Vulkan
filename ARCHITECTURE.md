# SaschaWillems Vulkan 示例项目 — 架构深度 文档

## 一、项目概览

这是一个包含 **100 个 Vulkan 功能示例** 的教学/参考项目，覆盖从基础三角形绘制到光线追踪的全部现代 Vulkan API 能力。项目由 Sascha Willems 维护，是 Vulkan 学习的事实标准参考实现。

**技术栈：**
- 语言：C++20
- 图形 API：Vulkan 1.0 ~ 1.3（按示例分级）
- 构建系统：CMake（Linux/Windows/macOS）+ Gradle（Android）+ Xcode（macOS/iOS）
- 着色器语言：GLSL（主力）+ HLSL + Slang

---

## 二、整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                    具 体 示 例  (100 个)                      │
│  triangle.cpp │ bloom.cpp │ raytracingbasic.cpp │ ...        │
│  每个示例 200~3000 行，继承 VulkanExampleBase                 │
├─────────────────────────────────────────────────────────────┤
│              VulkanRaytracingSample (扩展基类)                │
│  光线追踪专用：封装 TLAS/BLAS、SBT、光线追踪管线初始化          │
├─────────────────────────────────────────────────────────────┤
│               VulkanExampleBase (核心基类)                     │
│  3313 行 | 平台窗口/输入/事件循环 | Vulkan 实例/设备/交换链    │
│  定义 render() 纯虚接口，子类必须实现                         │
├──────────────────────┬──────────────────────────────────────┤
│     资源管理层         │          工具层                       │
│  VulkanDevice        │  VulkanTools (vks::tools)            │
│  VulkanBuffer        │  VulkanDebug  (vks::debug)           │
│  VulkanTexture       │  VulkanInitializers (vks::initializers)│
│  VulkanglTFModel     │  UIOverlay    (vks::UIOverlay)       │
│  VulkanFrameBuffer   │  Camera, Benchmark, ThreadPool        │
│  VulkanSwapChain     │  CommandLineParser, Frustum          │
├──────────────────────┴──────────────────────────────────────┤
│                    平 台 抽 象 层                              │
│  Windows(XCB/Win32) │ Linux(Wayland/D2D) │ Android │ Apple   │
├─────────────────────────────────────────────────────────────┤
│                    第 三 方 库                                 │
│  glm │ imgui │ tinygltf │ ktx │ stb │ gli                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 三、核心基类：VulkanExampleBase

### 3.1 位置

- 头文件：`base/vulkanexamplebase.h`
- 实现：`base/vulkanexamplebase.cpp`
- 行数：3313 行

### 3.2 核心职责

`VulkanExampleBase` 是整个框架的**心脏**，承担了以下职责：

| 职责 | 说明 |
|------|------|
| **Vulkan 初始化** | 创建 Instance → 选择 PhysicalDevice → 创建 Logical Device → 创建 Queue |
| **窗口系统集成** | 根据不同平台创建 VkSurfaceKHR 和 SwapChain |
| **帧循环** | 平台特定的事件循环（WinMain / main / android_main / Cocoa RunLoop） |
| **输入处理** | 键盘/鼠标/手柄事件分发到虚方法 |
| **UI 叠加层** | Dear ImGui 集成，`OnUpdateUIOverlay()` 虚方法供子类自定义 |
| **命令缓冲管理** | 维护 `drawCmdBuffers[maxConcurrentFrames]`（默认 2 帧并发） |
| **基准测试** | `vks::Benchmark` 类支持自动化帧率/时间统计 |

### 3.3 核心成员变量

```cpp
// Vulkan 核心
VkInstance           instance;
VkPhysicalDevice     physicalDevice;
VkDevice             device;
vks::VulkanDevice   *vulkanDevice;   // 封装对象
VkQueue              queue;           // 图形队列

// 渲染管线
VulkanSwapChain      swapChain;
VkRenderPass         renderPass;
VkPipelineCache      pipelineCache;
Camera               camera;

// 帧管理
std::array<VkCommandBuffer, 2> drawCmdBuffers;  // 默认 2 帧并发

// UI
vks::UIOverlay       ui;           // ImGui 叠加层
```

### 3.4 虚方法接口（子类可重写）

| 方法 | 类型 | 说明 |
|------|------|------|
| `render()` | **纯虚** | 每帧渲染逻辑，**必须实现** |
| `prepare()` | virtual | Vulkan 资源初始化 |
| `buildCommandBuffers()` | virtual | 录制命令缓冲 |
| `getEnabledFeatures()` | virtual | 启用物理设备特性 |
| `getEnabledExtensions()` | virtual | 启用设备扩展 |
| `createInstance()` | virtual | 自定义 VkInstance 创建 |
| `setupDepthStencil()` | virtual | 深度/模板缓冲 |
| `setupFrameBuffer()` | virtual | Framebuffer |
| `setupRenderPass()` | virtual | RenderPass |
| `windowResized()` | virtual | 窗口 resize |
| `keyPressed(uint32_t)` | virtual | 按键处理 |
| `mouseMoved(...)` | virtual | 鼠标移动 |
| `OnUpdateUIOverlay(vks::UIOverlay*)` | virtual | 自定义 ImGui UI |
| `OnHandleMessage(...)` | virtual | Windows 消息处理 |

---

## 四、继承层次

```
VulkanExampleBase
    │
    ├─── VulkanExample (直接继承)
    │     ├── triangle/triangle.cpp           (~200 行)
    │     ├── texture/texture.cpp             (~500 行)
    │     ├── bloom/bloom.cpp                 (~800 行)
    │     ├── pbrbasic/pbrbasic.cpp           (~600 行)
    │     ├── shadowmapping/shadowmapping.cpp (~1200 行)
    │     └── ... (约 90 个)
    │
    └─── VulkanRaytracingSample
          ├── raytracingbasic.cpp
          ├── raytracingreflections.cpp
          ├── raytracingshadows.cpp
          └── ... (约 8 个)
```

### 4.1 VulkanRaytracingSample

光线追踪专用扩展基类，封装了：
- TLAS/BLAS（顶层/底层加速结构）创建
- SBT（Shader Binding Table）设置
- 光线追踪管线创建
- `VkAccelerationStructureKHR` 封装

---

## 五、典型示例代码模式

每个示例遵循统一的代码结构：

```cpp
class VulkanExample : public VulkanExampleBase
{
    // 1. 声明自定义资源
    vks::Buffer vertexBuffer;
    VkPipeline pipeline;
    VkDescriptorSet descriptorSet;

public:
    VulkanExample() : VulkanExampleBase() {
        title = "示例名称";
        // 设置 camera、settings 等
    }

    ~VulkanExample() {
        // 清理自定义资源
    }

    // 2. 可选：启用设备特性
    void getEnabledFeatures() override {
        enabledFeatures.samplerAnisotropy = VK_TRUE;
    }

    // 3. 可选：启用设备扩展
    void getEnabledExtensions() override {
        enabledDeviceExtensions.push_back(VK_KHR_EXTENSION_NAME);
    }

    // 4. 重写 prepare()
    void prepare() override {
        VulkanExampleBase::prepare();  // 必须调用基类
        loadAssets();                   // 初始化自定义资源
        preparePipelines();
        prepareDescriptorSets();
    }

    // 5. 录制命令缓冲
    void buildCommandBuffers() override {
        // vkCmdBeginRenderPass
        // vkCmdBindPipeline
        // vkCmdDraw / vkCmdDrawIndexed
        // vkCmdEndRenderPass
    }

    // 6. 每帧渲染
    void render() override {
        VulkanExampleBase::prepareFrame();
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VulkanExampleBase::submitFrame();
    }

    // 7. 可选：UI 叠加层
    void OnUpdateUIOverlay(vks::UIOverlay *overlay) override {
        if (overlay->header("设置")) {
            overlay->checkBox("开关", &enabled);
        }
    }
};

VULKAN_EXAMPLE_MAIN()  // 平台特定入口点
```

---

## 六、平台抽象层

### 6.1 平台识别机制

使用 Vulkan 标准平台宏进行条件编译：

| 宏 | 平台 | 窗口系统 | 入口函数 |
|---|---|---|---|
| `_WIN32` | Windows | Win32 HWND | `WinMain()` |
| `VK_USE_PLATFORM_XCB_KHR` | Linux X11 | XCB | `main()` |
| `VK_USE_PLATFORM_WAYLAND_KHR` | Linux Wayland | Wayland | `main()` |
| `VK_USE_PLATFORM_ANDROID_KHR` | Android | ANativeWindow | `android_main()` |
| `VK_USE_PLATFORM_METAL_EXT` | macOS (Xcode) | CAMetalLayer | `main()` |
| `VK_USE_PLATFORM_MACOS_MVK` | macOS (MoltenVK) | CAMetalLayer | `main()` |
| `VK_USE_PLATFORM_IOS_MVK` | iOS (MoltenVK) | CAMetalLayer | `main()` |
| `_DIRECT2DISPLAY` | Linux 直连显示 | DirectToDisplay | `main()` |
| `VK_USE_PLATFORM_HEADLESS_EXT` | 无头渲染 | 无窗口 | `main()` |
| `VK_USE_PLATFORM_SCREEN_QNX` | QNX | Screen | `main()` |

### 6.2 入口点宏

`base/Entrypoints.h` 定义了 `VULKAN_EXAMPLE_MAIN()` 宏，根据平台条件编译出不同的入口函数：

```cpp
// Windows
#ifdef _WIN32
int WINAPI WinMain(...) { ... }
#endif

// Android
#ifdef VK_USE_PLATFORM_ANDROID_KHR
void android_main(struct android_app* state) { ... }
#endif

// Linux/其他
#if defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_WAYLAND_KHR) ...
int main(int argc, char *argv[]) { ... }
#endif
```

### 6.3 各平台差异对比

| 维度 | Windows | Linux (XCB) | Android | macOS/iOS |
|------|---------|-------------|---------|-----------|
| **Vulkan Loader** | 静态链接 `vulkan-1.dll` | 静态链接 `libvulkan.so` | 动态加载 (`VK_NO_PROTOTYPES`) | MoltenVK dylib |
| **表面类型** | `VK_USE_PLATFORM_WIN32_KHR` | `VK_USE_PLATFORM_XCB_KHR` | `VK_USE_PLATFORM_ANDROID_KHR` | `VK_USE_PLATFORM_METAL_EXT` |
| **输入系统** | Win32 消息 | XCB 事件 | AInputEvent | Cocoa/Touch |
| **文件系统** | `fopen` | `fopen` | `AAssetManager` | NSBundle |
| **日志** | `OutputDebugString` | `std::cerr` | `__android_log_print` | `NSLog` |
| **着色器格式** | SPIR-V | SPIR-V | SPIR-V | SPIR-V |
| **复杂度** | 中 | 低 | 高 | 高 |

### 6.4 Android 特殊处理

Android 是最复杂的平台，需要额外处理：

**Vulkan 动态加载：**
- 所有 Vulkan 函数通过 `PFN_vk*` 函数指针调用
- `VulkanAndroid.h` 声明所有函数指针
- `VulkanAndroid.cpp` 在运行时从 `libvulkan.so` 加载
- 编译 flag：`-DVK_NO_PROTOTYPES`

**资源访问：**
- 着色器和资产通过 `AAssetManager_fromJava()` 读取
- `VulkanTools.cpp` 中 `readFile()` 在 Android 上走 AAssetManager 路径

**构建流程：**
```
Gradle → CMake → Ninja → NDK clang → native-lib.so + libbase.so → APK
```

---

## 七、资源管理层

### 7.1 VulkanDevice (`base/VulkanDevice.h`)

封装 `VkPhysicalDevice` 和 `VkDevice`：
- 内存类型查找（`getMemoryType()`)
- 设备特性查询
- 队列族属性

### 7.2 Buffer (`base/VulkanBuffer.h`)

`vks::Buffer` 类封装 `VkBuffer` + `VkDeviceMemory`：
- 支持 `VK_BUFFER_USAGE_UNIFORM_BUFFER`、`VK_BUFFER_USAGE_STORAGE_BUFFER` 等
- 自动选择内存类型（HOST_VISIBLE / DEVICE_LOCAL）
- 映射/取消映射辅助方法

### 7.3 Texture (`base/VulkanTexture.h`)

`vks::Texture` 系列类：
- `Texture2D` — 2D 纹理
- `Texture2DArray` — 纹理数组
- `TextureCubeMap` — 立方体贴图
- 底层使用 KTX 库加载压缩纹理

### 7.4 glTF Model (`base/VulkanglTFModel.h`)

`vkglTF::Model` 类：
- 基于 tinygltf 加载 glTF 2.0 模型
- 自动创建顶点/索引 buffer
- 支持 PBR 材质（metallic-roughness workflow）
- 支持骨骼动画（skinning）

### 7.5 FrameBuffer (`base/VulkanFrameBuffer.hpp`)

`vks::Framebuffer` 完整封装：
- RenderPass + Attachment + ImageView + Framebuffer 一键创建
- 支持多 color attachment + depth attachment

---

## 八、工具层

### 8.1 Initializers (`base/VulkanInitializers.hpp`)

`vks::initializers` 命名空间提供所有 Vulkan 结构体的工厂函数：

```cpp
VkCommandBufferAllocateInfo cmdBufAllocInfo = 
    vks::initializers::commandBufferAllocateInfo(cmdPool, 1);

VkPipelineVertexInputStateCreateInfo vertexInput = 
    vks::initializers::pipelineVertexInputStateCreateInfo();

VkDescriptorSetLayoutBinding layoutBinding = 
    vks::initializers::descriptorSetLayoutBinding(...);
```

极大简化了 Vulkan 冗长的结构体初始化。

### 8.2 Tools (`base/VulkanTools.h`)

`vks::tools` 命名空间：
- `isSupported()` — 检查扩展支持
- `getAssetPath()` — 获取资源路径（Android 特殊处理）
- `getShaderBasePath()` — 获取着色器路径
- `readFile()` — 文件读取（Android 走 AAssetManager）
- `errorBox()` — 错误对话框

### 8.3 Debug (`base/VulkanDebug.h`)

- `vks::debug` — 验证层回调设置
- `vks::debugutils` — VK_EXT_debug_utils 集成
- 自动注册 `VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT` 回调

### 8.4 UIOverlay (`base/VulkanUIOverlay.h`)

`vks::UIOverlay` 封装 Dear ImGui：
- 自动创建 ImGui 渲染管线
- 提供 `header()`、`checkBox()`、`slider()` 等辅助方法
- 平台特定的输入处理

### 8.5 其他工具

| 文件 | 类/命名空间 | 功能 |
|------|------------|------|
| `camera.hpp` | `Camera` | FPS 相机 / LookAt 相机 |
| `benchmark.hpp` | `vks::Benchmark` | 自动化帧率统计 |
| `threadpool.hpp` | `vks::ThreadPool` | C++11 线程池 |
| `frustum.hpp` | `Frustum` | 视锥体计算 |
| `keycodes.hpp` | `KeyCode` | 跨平台按键映射 |
| `CommandLineParser.hpp` | `CommandLineParser` | 命令行参数解析 |

---

## 九、着色器系统

### 9.1 目录结构

```
shaders/
├── glsl/              # GLSL 源码 + 预编译 .spv（主力）
│   ├── base/          # UI overlay 等基础着色器
│   ├── triangle/      # 每个示例一个子目录
│   ├── bloom/
│   ├── raytracingbasic/
│   └── compileshaders.py
├── hlsl/              # HLSL 源码 + 预编译 .spv
└── slang/             # Slang 源码（shader-slang.org）
```

### 9.2 着色器阶段覆盖

支持所有 Vulkan 着色器阶段：

| 阶段 | 扩展名 | 说明 |
|------|--------|------|
| Vertex | `.vert` | 顶点着色器 |
| Fragment | `.frag` | 片元着色器 |
| Compute | `.comp` | 计算着色器 |
| Geometry | `.geom` | 几何着色器 |
| Tessellation Control | `.tesc` | 细分控制 |
| Tessellation Evaluation | `.tese` | 细分评估 |
| Mesh | `.mesh` | Mesh 着色器（NV/AMD） |
| Task | `.task` | Task 着色器 |
| Ray Generation | `.rgen` | 光线生成 |
| Ray Closest Hit | `.rchit` | 最近命中 |
| Ray Miss | `.rmiss` | 未命中 |
| Ray Callable | `.rcall` | 可调函数 |
| Ray Any Hit | `.rahit` | 任意命中 |
| Ray Intersection | `.rint` | 相交测试 |

### 9.3 编译流程

`shaders/glsl/compileshaders.py`：
- 使用 `glslangValidator` 编译 GLSL → SPIR-V
- 光线追踪着色器：`--target-env vulkan1.2`
- Mesh/Task 着色器：`--target-env spirv1.4`
- 支持 `--sample <name>` 编译单个示例
- 支持 `--g` 编译调试符号

CMake 中着色器作为源文件添加（IDE 可见），预编译 `.spv` 在运行时直接加载。

---

## 十、第三方库

| 库 | 路径 | 用途 | 复杂度 |
|---|------|------|--------|
| **GLM** | `external/glm/` | OpenGL Mathematics，矩阵/向量运算 | 大 |
| **Dear ImGui** | `external/imgui/` | 即时模式 UI，调试面板 | 中 |
| **tinygltf** | `external/tinygltf/` | glTF 2.0 模型加载 | 中 |
| **KTX** | `external/ktx/` | KTX/Basis 压缩纹理加载 | 中 |
| **stb** | `external/stb/` | stb_image 等单文件图像库 | 小 |
| **gli** | `external/gli/` | GLI 图像库，纹理格式转换 | 小 |
| **Vulkan Headers** | `external/vulkan/` | Vulkan SDK 头文件 | — |

---

## 十一、构建系统

### 11.1 CMake 结构

```
CMakeLists.txt (顶层)
├── 设置 C++20
├── 包含 external/ 头文件
├── 查找 Vulkan SDK
├── 根据平台设置宏和链接库
├── add_subdirectory(base)
│   └── 编译 libbase.a（含 ImGui + KTX）
└── add_subdirectory(examples)
    └── buildExample() 函数循环构建 100 个示例
```

### 11.2 可选编译选项

| 选项 | 说明 | 默认 |
|------|------|------|
| `USE_WAYLAND_WSI` | Wayland 交换链 | OFF |
| `USE_HEADLESS` | Headless 渲染 | OFF |
| `USE_D2D_WSI` | Direct-to-Display | OFF |
| `USE_DIRECTFB_WSI` | DirectFB | OFF |
| `USE_RELATIVE_ASSET_PATH` | 相对资源路径 | OFF |
| `FORCE_VALIDATION` | 强制验证层 | OFF |

### 11.3 Android 构建

```
Gradle (AGP 8.12.3)
├── settings.gradle — 自动发现所有示例子项目
├── build.gradle — 全局配置（minSdk 21, arm64-v8a）
└── examples/
    ├── base/CMakeLists.txt — 编译 libbase.so + libktx
    └── <example>/
        ├── build.gradle — 复制着色器/资源到 assets
        └── CMakeLists.txt — 编译 native-lib.so
            ├── 链接 native-app-glue
            ├── 链接 libbase
            └── 编译 flag: -DVK_USE_PLATFORM_ANDROID_KHR -DVK_NO_PROTOTYPES
```

### 11.4 macOS/iOS 构建

- **CMake 生成**：使用标准 `VULKAN_EXAMPLE_MAIN()` 宏
- **Xcode 手动项目**：`apple/examples.xcodeproj`，使用 `VK_EXAMPLE_XCODE_GENERATED` 宏走 Cocoa 事件循环
- 使用 **MoltenVK** 将 Vulkan 翻译为 Metal

---

## 十二、文件位置索引

| 文件/目录 | 路径 | 说明 |
|-----------|------|------|
| 项目根目录 | `/home/u/prj/tools/vulkan-sascha-demo/` | — |
| 核心基类 | `base/vulkanexamplebase.h` | VulkanExampleBase |
| 设备封装 | `base/VulkanDevice.h` | vks::VulkanDevice |
| Buffer 封装 | `base/VulkanBuffer.h` | vks::Buffer |
| 纹理加载 | `base/VulkanTexture.h` | vks::Texture |
| glTF 模型 | `base/VulkanglTFModel.h` | vkglTF::Model |
| 工具函数 | `base/VulkanTools.h` | vks::tools |
| 初始化辅助 | `base/VulkanInitializers.hpp` | vks::initializers |
| 调试回调 | `base/VulkanDebug.h` | vks::debug |
| UI 叠加层 | `base/VulkanUIOverlay.h` | vks::UIOverlay |
| SwapChain | `base/VulkanSwapChain.h` | VulkanSwapChain |
| FrameBuffer | `base/VulkanFrameBuffer.hpp` | vks::Framebuffer |
| 入口点宏 | `base/Entrypoints.h` | VULKAN_EXAMPLE_MAIN() |
| Android 加载 | `base/VulkanAndroid.h` | 函数指针声明 |
| 光线追踪基类 | `base/VulkanRaytracingSample.h` | VulkanRaytracingSample |
| 相机 | `base/camera.hpp` | Camera |
| 基准测试 | `base/benchmark.hpp` | vks::Benchmark |
| 线程池 | `base/threadpool.hpp` | vks::ThreadPool |
| 按键映射 | `base/keycodes.hpp` | 跨平台 keycode |
| 视锥体 | `base/frustum.hpp` | Frustum |
| CMake 顶层 | `CMakeLists.txt` | 主构建配置 |
| Android Gradle | `android/build.gradle` | AGP 配置 |
| Android CMake | `android/examples/base/CMakeLists.txt` | base 库编译 |
| 着色器 GLSL | `shaders/glsl/` | GLSL 源码 + spv |
| 着色器 HLSL | `shaders/hlsl/` | HLSL 源码 + spv |
| 着色器 Slang | `shaders/slang/` | Slang 源码 |
| glTF 资源 | `assets/models/` | glTF 2.0 模型 |
| 纹理资源 | `assets/textures/` | KTX 纹理 |
| ImGui | `external/imgui/` | Dear ImGui |
| 数学库 | `external/glm/` | GLM |
| glTF 加载器 | `external/tinygltf/` | tinygltf |
| KTX 库 | `external/ktx/` | KTX 纹理库 |
| macOS/iOS 适配 | `apple/` | MoltenVK 包装 |
| Linux 编译输出 | `build/bin/` | ELF 可执行文件 |
| Android APK | `android/examples/bin/` | APK 文件 |

---

## 十三、复杂度评估

| 模块 | 代码量 | 复杂度 | 说明 |
|------|--------|--------|------|
| VulkanExampleBase | 3313 行 | ★★★★★ | 核心框架，平台抽象，大量条件编译 |
| VulkanRaytracingSample | ~800 行 | ★★★★ | 光线追踪加速结构管理 |
| VulkanSwapChain | ~400 行 | ★★★ | 交换链封装 |
| VulkanDevice | ~300 行 | ★★ | 设备封装，内存类型查找 |
| VulkanBuffer | ~250 行 | ★★ | Buffer + Memory 封装 |
| VulkanTexture | ~500 行 | ★★★ | KTX 加载，多格式支持 |
| VulkanglTFModel | ~1500 行 | ★★★★ | glTF 加载，PBR 材质，骨骼动画 |
| VulkanTools | ~400 行 | ★★★ | 平台特定文件 I/O |
| VulkanUIOverlay | ~600 行 | ★★★ | ImGui 集成 |
| VulkanInitializers | ~800 行 | ★★ | 纯模板代码，简单但量大 |
| VulkanAndroid | ~200 行 | ★★★ | 动态加载 Vulkan 函数 |
| 示例 (平均) | ~600 行 | ★★~★★★★ | triangle ~200 行，raytracing ~3000 行 |

---

## 十四、设计模式总结

| 模式 | 应用 |
|------|------|
| **模板方法** | `VulkanExampleBase` 定义 `prepare()→render()→submitFrame()` 流程，子类重写步骤 |
| **工厂** | `vks::initializers` 提供所有 Vulkan 结构体构造函数 |
| **RAII** | `vks::Buffer`、`vks::VulkanDevice`、`vks::Framebuffer` 封装资源生命周期 |
| **策略** | 不同平台通过 `#ifdef` 选择不同窗口/输入/文件 I/O 实现 |
| **分层继承** | `VulkanExampleBase` → `VulkanRaytracingSample` → 具体示例，逐层添加功能 |
| **PIMPL** | `VulkanDevice` 封装物理设备和逻辑设备的复杂性 |

---

## 十五、与 sdl3-gpu-starter 对比

| 维度 | sdl3-gpu-starter | vulkan-sascha-demo |
|------|------------------|-------------------|
| **抽象层级** | 高级抽象（SDL3 GPU API） | 低级原始 Vulkan API |
| **代码量/示例** | ~500 行/完整项目 | ~600 行/单个示例 |
| **跨平台** | SDL3 统一接口 | 条件编译区分平台 |
| **着色器编译** | 运行时 SDL_GPU_ShaderFormat | 预编译 SPIR-V |
| **学习曲线** | 低（API 简洁） | 高（需理解 Vulkan 所有概念） |
| **适合场景** | 快速原型、跨平台游戏 | Vulkan API 学习、功能验证 |
| **后端** | D3D12 / Vulkan / Metal | 纯 Vulkan |
