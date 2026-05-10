# SaschaWillems Vulkan 示例项目编译指南

## 环境要求

- **操作系统**: Linux (Ubuntu 24.04 已验证)
- **编译器**: g++ 13.3.0+
- **CMake**: 3.28+
- **Vulkan SDK**: 1.3.275+
- **Android 构建（可选）**:
  - Android SDK (路径: `$HOME/Android/Sdk`)
  - Android NDK 30.0.14904198
  - Gradle 8.13+
  - JDK 17

---

## 一、Linux 版本编译

### 1. 获取源码

```bash
cd /home/u/prj/tools
git clone --depth 1 https://github.com/SaschaWillems/Vulkan.git vulkan-sascha-demo
cd vulkan-sascha-demo
git submodule update --init --recursive
```

### 2. 配置 CMake

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

**可选参数**:
- `-DUSE_WAYLAND_WSI=ON` — 使用 Wayland 窗口系统（默认 XCB）
- `-DRESOURCE_INSTALL_DIR=/path/to/assets` — 自定义资源路径

### 3. 编译

```bash
make -j$(nproc)
```

编译完成后，可执行文件位于 `build/bin/` 目录，约 96 个示例。

### 4. 运行示例

```bash
cd build/bin
./triangle          # 最简单的三角形示例
./bloom             # Bloom 泛光效果
./pbrbasic          # PBR 基础渲染
./computeparticles  # 计算着色器粒子系统
```

**注意**: 示例程序默认从源码树读取 shaders 和 assets，需在 `build/bin/` 目录下运行。

---

## 二、Android 版本编译

### 1. 工具链位置

| 工具 | 路径 |
|------|------|
| Android SDK | `$HOME/Android/Sdk` |
| Android NDK | `$HOME/Android/Sdk/ndk/30.0.14904198` |
| CMake (Android) | `$HOME/Android/Sdk/cmake/3.22.1` |

### 2. 设置环境变量

```bash
export ANDROID_SDK_ROOT=$HOME/Android/Sdk
export ANDROID_HOME=$ANDROID_SDK_ROOT
export ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/30.0.14904198
export ANDROID_NDK_HOME=$ANDROID_NDK_ROOT
export PATH=$PATH:$ANDROID_SDK_ROOT/platform-tools
```

### 3. 检查 Gradle 版本

项目需要 Gradle 8.13+。检查本地是否有缓存：

```bash
ls ~/.gradle/wrapper/dists/
```

如果有 `gradle-8.13-bin` 或更高版本，可直接使用。否则需要下载：

```bash
# 如果 wrapper 下载失败，手动使用本地缓存的版本
# 示例使用 Gradle 8.14:
GRADLE_BIN=$HOME/.gradle/wrapper/dists/gradle-8.14-all/<hash>/gradle-8.14/bin/gradle
```

### 4. 执行编译

```bash
cd vulkan-sascha-demo/android

# 清理旧构建
rm -rf examples/*/build examples/*/.cxx examples/bin build .gradle

# 编译（Debug 版本）
$GRADLE_BIN assembleDebug
```

**编译参数说明**:
- `assembleDebug` — 编译所有示例的 Debug APK
- `assembleRelease` — 编译 Release APK（需签名配置）
- `--parallel` — 启用并行构建（加快速度）
- `--max-workers=8` — 并行线程数

### 5. 编译输出

APK 文件位于：

```
vulkan-sascha-demo/android/examples/bin/
```

文件名格式：`示例名-debug.apk`

例如：
- `bloom-debug.apk`
- `triangle-debug.apk`
- `pbrbasic-debug.apk`

### 6. 安装到设备

```bash
# 连接 Android 设备后执行
adb install -r examples/bin/triangle-debug.apk
```

---

## 三、常见问题

### 1. Gradle wrapper 下载失败

**现象**: `Connection refused` 或网络超时

**解决**: 使用本地缓存的 Gradle 版本：

```bash
find ~/.gradle/wrapper/dists/ -name "gradle" -type d | grep "bin/gradle$"
```

### 2. NDK 版本不匹配

**现象**: 构建使用了错误的 NDK 版本，导致链接错误

**解决**: 显式设置 `ANDROID_NDK_ROOT` 环境变量：

```bash
export ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/30.0.14904198
```

### 3. APK 打包冲突

**现象**: `Zip file already contains entry`

**原因**: 多个示例并行写入同一 bin 目录时路径冲突

**解决**: 使用串行构建（去掉 `--parallel` 参数）

### 4. CMake 警告 `No project() command`

**现象**: 构建时出现 CMake 警告

**说明**: 这是已知问题，不影响编译，可忽略

### 5. Vulkan 驱动不匹配

**现象**: Linux 运行时提示 GPU 驱动问题

**说明**: 本项目使用 MESA Dozen (D3D12-on-Vulkan) 驱动，在 WSL 环境下可能使用 Microsoft Direct3D12 后端。原生 Linux 应使用原生 Vulkan 驱动。

---

## 四、项目结构

```
vulkan-sascha-demo/
├── base/              # 公共基础库（VulkanExampleBase 等）
├── examples/          # 示例代码（约 96 个）
│   ├── triangle/      # 最简单的三角形
│   ├── bloom/         # Bloom 后处理
│   ├── pbrbasic/      # PBR 基础
│   └── ...
├── shaders/           # GLSL 着色器源码
│   └── glsl/
├── assets/            # 模型、纹理等资源（子模块）
├── external/          # 第三方库（glm, imgui, ktx, tinygltf 等）
├── android/           # Android 构建配置
│   ├── build.gradle
│   └── examples/      # 各示例的 build.gradle
├── CMakeLists.txt     # Linux CMake 主配置
└── BUILD.md           # 官方英文构建文档
```

---

## 五、示例分类

| 类别 | 示例 |
|------|------|
| 基础 | triangle, texture, gears, textoverlay |
| 光照 | pbrbasic, pbribl, hdr, bloom, ssao |
| 阴影 | shadowmapping, shadowmappingcascade, shadowmappingomni |
| 延迟渲染 | deferred, deferredmultisampling, deferredshadows |
| 计算着色器 | computeshader, computeparticles, computenbody, computecloth |
| 光线追踪 | raytracingbasic, raytracingreflections, raytracingshadows |
| 后处理 | radialblur, dof, fxaa |
| 几何 | tessellation, displacement, geometryshader |
| 高级特性 | meshshader, rayquery, descriptorindexing, pipeline library |
