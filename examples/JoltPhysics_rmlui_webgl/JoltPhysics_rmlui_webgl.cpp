/*
 * WebGL Example - JoltPhysics simulation with RmlUi overlay
 *
 * Emscripten/WebGL port demonstrating JoltPhysics rigid body simulation
 * rendered with OpenGL ES 3.0, with a RmlUi GL3 backend overlay showing
 * simulation statistics. The scene resets every 5 seconds.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace JPH::literals;

#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <cstdarg>

// Jolt trace/assert callbacks
static void JoltTraceImpl(const char* inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	printf("%s\n", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
{
	printf("%s:%u: (%s) %s\n", inFile, inLine, inExpression, inMessage ? inMessage : "");
	return false;
}
#endif
#include <RmlUi/Core/FileInterface.h>
#include "../external/RmlUi/Backends/RmlUi_Renderer_GL3.h"
#include "../external/RmlUi/Backends/RmlUi_Platform_SDL.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <random>

#ifndef VK_PROJECT_SOURCE_DIR
#define VK_PROJECT_SOURCE_DIR "."
#endif

#ifdef __EMSCRIPTEN__
#define ASSET_ROOT ""
#else
#define ASSET_ROOT VK_PROJECT_SOURCE_DIR
#endif

// ============================================================================
// GL Shader Sources (GLSL ES 3.00)
// ============================================================================

static const char* mesh_vert_src = R"(#version 300 es
precision highp float;
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform vec3 objectColor;
out vec3 outNormal;
out vec3 outColor;
out vec3 outViewVec;
out vec3 outLightVec;
void main() {
	outColor = objectColor;
	gl_Position = projection * view * model * vec4(inPos.xyz, 1.0);
	vec4 pos = view * model * vec4(inPos, 1.0);
	outNormal = mat3(view) * mat3(model) * inNormal;
	outLightVec = vec3(view * vec4(5.0, 10.0, -5.0, 1.0)) - pos.xyz;
	outViewVec = -pos.xyz;
}
)";

static const char* mesh_frag_src = R"(#version 300 es
precision highp float;
in vec3 outNormal;
in vec3 outColor;
in vec3 outViewVec;
in vec3 outLightVec;
layout (location = 0) out vec4 fragColor;
void main() {
	vec3 N = normalize(outNormal);
	vec3 L = normalize(outLightVec);
	vec3 V = normalize(outViewVec);
	vec3 R = reflect(-L, N);
	vec3 diffuse = max(dot(N, L), 0.15) * outColor;
	vec3 specular = pow(max(dot(R, V), 0.0), 32.0) * vec3(0.5);
	fragColor = vec4(diffuse + specular, 1.0);
}
)";

static const char* overlay_vert_src = R"(#version 300 es
precision highp float;
layout (location = 0) in vec2 inPosition;
out vec2 outUV;
void main() {
	outUV = inPosition * 0.5 + 0.5;
	gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";

static const char* overlay_frag_src = R"(#version 300 es
precision highp float;
in vec2 outUV;
layout (location = 0) out vec4 fragColor;
uniform sampler2D samplerUI;
void main() {
	fragColor = texture(samplerUI, outUV);
}
)";

// ============================================================================
// GL Utility Functions
// ============================================================================

static GLuint compileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, 512, nullptr, log);
		printf("[Shader Error] %s\n", log);
	}
	return shader;
}

static GLuint linkProgram(const char* vertSrc, const char* fragSrc)
{
	GLuint prog = glCreateProgram();
	GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetProgramInfoLog(prog, 512, nullptr, log);
		printf("[Link Error] %s\n", log);
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return prog;
}

// ============================================================================
// RmlUiFileInterface
// ============================================================================

class RmlUiFileInterface : public Rml::FileInterface
{
public:
	RmlUiFileInterface(const Rml::String& root) : root(root) {}
	Rml::FileHandle Open(const Rml::String& path) override {
		FILE* fp = fopen((root + path).c_str(), "rb");
		if (fp) return (Rml::FileHandle)fp;
		return (Rml::FileHandle)fopen(path.c_str(), "rb");
	}
	void Close(Rml::FileHandle file) override { fclose((FILE*)file); }
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override { return fread(buffer, 1, size, (FILE*)file); }
	bool Seek(Rml::FileHandle file, long offset, int origin) override { return fseek((FILE*)file, offset, origin) == 0; }
	size_t Tell(Rml::FileHandle file) override { return ftell((FILE*)file); }
private:
	Rml::String root;
};

// ============================================================================
// Custom GL3 Render Interface
// ============================================================================

class RenderInterface_GL3_WebGL : public RenderInterface_GL3
{
public:
	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override
	{
		FILE* fp = fopen(source.c_str(), "rb");
		if (!fp) return {};
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		std::vector<unsigned char> buffer(size);
		fread(buffer.data(), 1, size, fp);
		fclose(fp);

		int w, h, channels;
		unsigned char* pixels = stbi_load_from_memory(buffer.data(), (int)size, &w, &h, &channels, 4);
		if (!pixels) return {};
		texture_dimensions = { w, h };

		for (int i = 0; i < w * h; i++) {
			unsigned char a = pixels[i * 4 + 3];
			pixels[i * 4 + 0] = (unsigned char)(pixels[i * 4 + 0] * a / 255);
			pixels[i * 4 + 1] = (unsigned char)(pixels[i * 4 + 1] * a / 255);
			pixels[i * 4 + 2] = (unsigned char)(pixels[i * 4 + 2] * a / 255);
		}

		Rml::TextureHandle handle = RenderInterface_GL3::GenerateTexture(
			{ pixels, (size_t)(w * h * 4) }, texture_dimensions);
		stbi_image_free(pixels);
		return handle;
	}
};

// ============================================================================
// SimpleCamera - Orbit camera
// ============================================================================

class SimpleCamera
{
public:
	glm::vec3 target = glm::vec3(0.0f, 2.0f, 0.0f);
	float distance = 20.0f;
	float rotationX = 20.0f, rotationY = 30.0f;
	float fov = 60.0f, aspect = 1.0f, nearPlane = 0.1f, farPlane = 256.0f;
	bool flipY = false;
	glm::mat4 viewMatrix = glm::mat4(1.0f);
	glm::mat4 projMatrix = glm::mat4(1.0f);

	void update()
	{
		float pitch = glm::radians(rotationX);
		float yaw = glm::radians(rotationY);
		glm::vec3 position;
		position.x = target.x + distance * cos(pitch) * sin(yaw);
		position.y = target.y + distance * sin(pitch);
		position.z = target.z + distance * cos(pitch) * cos(yaw);
		viewMatrix = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
		float fovRad = glm::radians(fov);
		projMatrix = glm::perspective(fovRad, aspect, nearPlane, farPlane);
		if (flipY) projMatrix[1][1] *= -1.0f;
	}

	void rotate(float dx, float dy) { rotationY += dx * 0.3f; rotationX += dy * 0.3f; rotationX = glm::clamp(rotationX, -89.0f, 89.0f); }
	void zoom(float delta) { distance -= delta * 1.0f; if (distance < 2.0f) distance = 2.0f; if (distance > 80.0f) distance = 80.0f; }
};

// ============================================================================
// GL3RmlUiOverlay - RmlUi overlay with FBO offscreen rendering
// ============================================================================

class GL3RmlUiOverlay
{
public:
	bool visible = true;

	void init(int w, int h, const Rml::String& dataRoot)
	{
		width = w; height = h;

		file_interface = new RmlUiFileInterface(dataRoot);
		Rml::SetFileInterface(file_interface);
		Rml::Initialise();

		render_interface = new RenderInterface_GL3_WebGL();
		Rml::SetRenderInterface(render_interface);
		Rml::SetSystemInterface(&system_interface);

		context = Rml::CreateContext("main", Rml::Vector2i(w, h));

		overlayProgram = linkProgram(overlay_vert_src, overlay_frag_src);
		locSamplerUI = glGetUniformLocation(overlayProgram, "samplerUI");
		static const float quadVerts[] = { -1.f,-1.f, 3.f,-1.f, -1.f,3.f };
		glGenVertexArrays(1, &overlayVAO);
		glGenBuffers(1, &overlayVBO);
		glBindVertexArray(overlayVAO);
		glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
		glBindVertexArray(0);

		createFramebuffer(w, h);
	}

	void shutdown()
	{
		destroyFramebuffer();
		if (overlayVAO) glDeleteVertexArrays(1, &overlayVAO);
		if (overlayVBO) glDeleteBuffers(1, &overlayVBO);
		if (overlayProgram) glDeleteProgram(overlayProgram);
		if (context) { Rml::RemoveContext(context->GetName()); context = nullptr; }
		Rml::Shutdown();
		delete render_interface;
		delete file_interface;
	}

	void createFramebuffer(int w, int h)
	{
		glGenFramebuffers(1, &fbo);
		glGenTextures(1, &colorTexture);
		glGenRenderbuffers(1, &depthRBO);

		glBindTexture(GL_TEXTURE_2D, colorTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void destroyFramebuffer()
	{
		if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
		if (colorTexture) { glDeleteTextures(1, &colorTexture); colorTexture = 0; }
		if (depthRBO) { glDeleteRenderbuffers(1, &depthRBO); depthRBO = 0; }
	}

	void loadFont(const char* path) { Rml::LoadFontFace(path); }
	void loadDocument(const char* path) { auto* doc = context->LoadDocument(path); if (doc) doc->Show(); }

	void resize(int w, int h)
	{
		if (w <= 0 || h <= 0) return;
		width = w; height = h;
		if (context) context->SetDimensions(Rml::Vector2i(w, h));
		destroyFramebuffer();
		createFramebuffer(w, h);
	}

	void processMouseMove(int x, int y) { if (context) context->ProcessMouseMove(x, y, 0); }
	void processMouseButton(int button, bool down) {
		if (!context) return;
		if (down) context->ProcessMouseButtonDown(button, 0);
		else context->ProcessMouseButtonUp(button, 0);
	}
	bool processMouseWheel(float delta) { if (context) return context->ProcessMouseWheel(-delta, 0); return true; }
	void processKeyDown(Rml::Input::KeyIdentifier key) { if (context) context->ProcessKeyDown(key, 0); }
	void processKeyUp(Rml::Input::KeyIdentifier key) { if (context) context->ProcessKeyUp(key, 0); }

	void readOffscreenPixel(int x, int y, uint8_t* rgba)
	{
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
		if (x < 0 || x >= width || y < 0 || y >= height) return;
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void update() { if (context && visible) context->Update(); }

	void renderOffscreen()
	{
		if (!context || !visible) return;
		glViewport(0, 0, width, height);
		glClearColor(0, 0, 0, 0);
		render_interface->SetViewport(width, height);
		render_interface->BeginFrame();
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glClear(GL_COLOR_BUFFER_BIT);
		context->Render();
		render_interface->EndFrame();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	GLuint getColorTexture() const { return colorTexture; }

	void compositeTexture(GLuint texture)
	{
		glViewport(0, 0, width, height);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glUseProgram(overlayProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glUniform1i(locSamplerUI, 0);
		glBindVertexArray(overlayVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}

	Rml::Context* getContext() const { return context; }

private:
	Rml::Context* context = nullptr;
	RenderInterface_GL3_WebGL* render_interface = nullptr;
	RmlUiFileInterface* file_interface = nullptr;
	Rml::SystemInterface system_interface;
	GLuint fbo = 0, colorTexture = 0, depthRBO = 0;
	GLuint overlayVAO = 0, overlayVBO = 0, overlayProgram = 0;
	GLint locSamplerUI = -1;
	int width = 0, height = 0;
};

// ============================================================================
// PhysicsRenderer - Draws simple boxes and spheres
// ============================================================================

class PhysicsRenderer
{
public:
	GLuint shaderProgram = 0;
	GLint locProjection = -1, locView = -1, locModel = -1, locColor = -1;
	GLuint cubeVAO = 0, cubeVBO = 0, cubeIBO = 0;
	GLuint sphereVAO = 0, sphereVBO = 0, sphereIBO = 0;
	uint32_t cubeIndexCount = 0, sphereIndexCount = 0;

	void init()
	{
		shaderProgram = linkProgram(mesh_vert_src, mesh_frag_src);
		locProjection = glGetUniformLocation(shaderProgram, "projection");
		locView = glGetUniformLocation(shaderProgram, "view");
		locModel = glGetUniformLocation(shaderProgram, "model");
		locColor = glGetUniformLocation(shaderProgram, "objectColor");
		createCube();
		createSphere(16, 12);
	}

	void shutdown()
	{
		if (shaderProgram) glDeleteProgram(shaderProgram);
		if (cubeVAO) glDeleteVertexArrays(1, &cubeVAO);
		if (cubeVBO) glDeleteBuffers(1, &cubeVBO);
		if (cubeIBO) glDeleteBuffers(1, &cubeIBO);
		if (sphereVAO) glDeleteVertexArrays(1, &sphereVAO);
		if (sphereVBO) glDeleteBuffers(1, &sphereVBO);
		if (sphereIBO) glDeleteBuffers(1, &sphereIBO);
	}

	void drawBox(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model, const glm::vec3& color)
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
		glUniform3fv(locColor, 1, glm::value_ptr(color));
		glBindVertexArray(cubeVAO);
		glDrawElements(GL_TRIANGLES, cubeIndexCount, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);
	}

	void drawSphere(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model, const glm::vec3& color)
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
		glUniform3fv(locColor, 1, glm::value_ptr(color));
		glBindVertexArray(sphereVAO);
		glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);
	}

private:
	struct Vertex { glm::vec3 pos; glm::vec3 normal; };

	void createCube()
	{
		// 36 vertices (4 per face * 6 faces, with per-face normals), 36 indices
		Vertex vertices[] = {
			// +X face
			{{ 0.5f,-0.5f,-0.5f},{ 1, 0, 0}},{{ 0.5f, 0.5f,-0.5f},{ 1, 0, 0}},
			{{ 0.5f, 0.5f, 0.5f},{ 1, 0, 0}},{{ 0.5f,-0.5f, 0.5f},{ 1, 0, 0}},
			// -X face
			{{-0.5f,-0.5f, 0.5f},{-1, 0, 0}},{{-0.5f, 0.5f, 0.5f},{-1, 0, 0}},
			{{-0.5f, 0.5f,-0.5f},{-1, 0, 0}},{{-0.5f,-0.5f,-0.5f},{-1, 0, 0}},
			// +Y face
			{{-0.5f, 0.5f,-0.5f},{ 0, 1, 0}},{{-0.5f, 0.5f, 0.5f},{ 0, 1, 0}},
			{{ 0.5f, 0.5f, 0.5f},{ 0, 1, 0}},{{ 0.5f, 0.5f,-0.5f},{ 0, 1, 0}},
			// -Y face
			{{-0.5f,-0.5f, 0.5f},{ 0,-1, 0}},{{-0.5f,-0.5f,-0.5f},{ 0,-1, 0}},
			{{ 0.5f,-0.5f,-0.5f},{ 0,-1, 0}},{{ 0.5f,-0.5f, 0.5f},{ 0,-1, 0}},
			// +Z face
			{{ 0.5f,-0.5f, 0.5f},{ 0, 0, 1}},{{ 0.5f, 0.5f, 0.5f},{ 0, 0, 1}},
			{{-0.5f, 0.5f, 0.5f},{ 0, 0, 1}},{{-0.5f,-0.5f, 0.5f},{ 0, 0, 1}},
			// -Z face
			{{-0.5f,-0.5f,-0.5f},{ 0, 0,-1}},{{-0.5f, 0.5f,-0.5f},{ 0, 0,-1}},
			{{ 0.5f, 0.5f,-0.5f},{ 0, 0,-1}},{{ 0.5f,-0.5f,-0.5f},{ 0, 0,-1}},
		};
		uint32_t indices[] = {
			0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
			12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23,
		};
		cubeIndexCount = 36;
		glGenVertexArrays(1, &cubeVAO);
		glGenBuffers(1, &cubeVBO);
		glGenBuffers(1, &cubeIBO);
		glBindVertexArray(cubeVAO);
		glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
		glBindVertexArray(0);
	}

	void createSphere(int sectors, int stacks)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		const float pi = 3.14159265358979323846f;
		for (int i = 0; i <= stacks; i++) {
			float phi = pi * (float)i / stacks - pi / 2.0f;
			for (int j = 0; j <= sectors; j++) {
				float theta = 2.0f * pi * (float)j / sectors;
				glm::vec3 n(cos(phi) * cos(theta), sin(phi), cos(phi) * sin(theta));
				vertices.push_back({ n * 0.5f, n });
			}
		}
		for (int i = 0; i < stacks; i++) {
			for (int j = 0; j < sectors; j++) {
				uint32_t a = i * (sectors + 1) + j;
				uint32_t b = a + sectors + 1;
				indices.push_back(a); indices.push_back(b); indices.push_back(a + 1);
				indices.push_back(a + 1); indices.push_back(b); indices.push_back(b + 1);
			}
		}
		sphereIndexCount = (uint32_t)indices.size();
		glGenVertexArrays(1, &sphereVAO);
		glGenBuffers(1, &sphereVBO);
		glGenBuffers(1, &sphereIBO);
		glBindVertexArray(sphereVAO);
		glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereIBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
		glBindVertexArray(0);
	}
};

// ============================================================================
// Jolt Physics Layer Definitions
// ============================================================================

namespace Layers
{
	static constexpr ObjectLayer NON_MOVING = 0;
	static constexpr ObjectLayer MOVING = 1;
	static constexpr ObjectLayer NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
	virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
	{
		switch (inObject1) {
		case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
		case Layers::MOVING: return true;
		default: return false;
		}
	}
};

namespace BroadPhaseLayers
{
	static constexpr BroadPhaseLayer NON_MOVING(0);
	static constexpr BroadPhaseLayer MOVING(1);
	static constexpr uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}
	virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
	virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
	{
		switch ((BroadPhaseLayer::Type)inLayer) {
		case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
		default: return "INVALID";
		}
	}
#endif
private:
	BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1) {
		case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING: return true;
		default: return false;
		}
	}
};

// ============================================================================
// Emscripten Wheel Event Fallback
// ============================================================================

#ifdef __EMSCRIPTEN__
static float g_pendingWheelDelta = 0.0f;

static EM_BOOL em_wheel_callback(int, const EmscriptenWheelEvent* wheelEvent, void*)
{
	float deltaY = 0;
	switch (wheelEvent->deltaMode) {
	case DOM_DELTA_PIXEL: deltaY = wheelEvent->deltaY * 0.01f; break;
	case DOM_DELTA_LINE:  deltaY = wheelEvent->deltaY; break;
	case DOM_DELTA_PAGE:  deltaY = wheelEvent->deltaY * 3.0f; break;
	}
	g_pendingWheelDelta += deltaY;
	return EM_TRUE;
}
#endif

// ============================================================================
// PhysicsScene - Jolt Physics scene with bouncing objects
// ============================================================================

class PhysicsScene
{
public:
	struct DynamicBody {
		BodyID id;
		glm::vec3 color;
		bool isSphere;
		float size;
	};

	PhysicsSystem physicsSystem;
	BodyInterface* bodyInterface = nullptr;
	BPLayerInterfaceImpl broadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseFilter;
	ObjectLayerPairFilterImpl objectLayerFilter;
	TempAllocatorImpl* tempAllocator = nullptr;
	JobSystemThreadPool* jobSystem = nullptr;

	std::vector<DynamicBody> dynamicBodies;
	BodyID floorID;
	bool floorCreated = false;

	std::mt19937 rng{42};
	float elapsedTime = 0.0f;
	int resetCount = 0;
	static constexpr float RESET_INTERVAL = 5.0f;

	void init()
	{
		RegisterDefaultAllocator();
		Trace = JoltTraceImpl;
		JPH_IF_ENABLE_ASSERTS(AssertFailed = JoltAssertFailedImpl;)
		Factory::sInstance = new Factory();
		RegisterTypes();

		tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
		jobSystem = new JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);

		const uint cMaxBodies = 256;
		const uint cMaxBodyPairs = 1024;
		const uint cMaxContactConstraints = 1024;

		physicsSystem.Init(cMaxBodies, 0, cMaxBodyPairs, cMaxContactConstraints,
			broadPhaseLayerInterface, objectVsBroadPhaseFilter, objectLayerFilter);

		bodyInterface = &physicsSystem.GetBodyInterface();
		createScene();
	}

	void shutdown()
	{
		removeAllBodies();
		physicsSystem.OptimizeBroadPhase();
		UnregisterTypes();
		delete Factory::sInstance;
		Factory::sInstance = nullptr;
		delete jobSystem;
		jobSystem = nullptr;
		delete tempAllocator;
		tempAllocator = nullptr;
	}

	void removeAllBodies()
	{
		for (auto& db : dynamicBodies) {
			bodyInterface->RemoveBody(db.id);
			bodyInterface->DestroyBody(db.id);
		}
		dynamicBodies.clear();
		if (floorCreated) {
			bodyInterface->RemoveBody(floorID);
			bodyInterface->DestroyBody(floorID);
			floorCreated = false;
		}
	}

	void createScene()
	{
		removeAllBodies();
		elapsedTime = 0.0f;
		resetCount++;

		// Ground plane (large flat box)
		{
			BoxShapeSettings floorShape(Vec3(15.0f, 0.5f, 15.0f));
			floorShape.SetEmbedded();
			BodyCreationSettings floorSettings(&floorShape, RVec3(0.0_r, -0.5_r, 0.0_r), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
			floorSettings.mRestitution = 0.5f;
			floorSettings.mFriction = 0.5f;
			Body* floor = bodyInterface->CreateBody(floorSettings);
			bodyInterface->AddBody(floor->GetID(), EActivation::DontActivate);
			floorID = floor->GetID();
			floorCreated = true;
		}

		// Spawn bouncing objects
		std::uniform_real_distribution<float> posDist(-8.0f, 8.0f);
		std::uniform_real_distribution<float> sizeDist(0.3f, 1.0f);
		std::uniform_real_distribution<float> heightDist(5.0f, 20.0f);
		std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);
		std::uniform_real_distribution<float> velDist(-3.0f, 3.0f);

		const int numSpheres = 15;
		const int numBoxes = 15;

		for (int i = 0; i < numSpheres; i++) {
			float radius = sizeDist(rng);
			RVec3 pos(posDist(rng), heightDist(rng), posDist(rng));
			SphereShapeSettings sphereShape(radius);
			sphereShape.SetEmbedded();
			BodyCreationSettings settings(&sphereShape, pos, Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
			settings.mRestitution = 0.7f;
			settings.mFriction = 0.3f;
			settings.mLinearDamping = 0.05f;
			Body* body = bodyInterface->CreateBody(settings);
			bodyInterface->AddBody(body->GetID(), EActivation::Activate);
			bodyInterface->SetLinearVelocity(body->GetID(), Vec3(velDist(rng), -2.0f, velDist(rng)));
			dynamicBodies.push_back({
				body->GetID(),
				glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng)),
				true, radius
			});
		}

		for (int i = 0; i < numBoxes; i++) {
			float halfSize = sizeDist(rng) * 0.5f;
			RVec3 pos(posDist(rng), heightDist(rng), posDist(rng));
			BoxShapeSettings boxShape(Vec3(halfSize, halfSize, halfSize));
			boxShape.SetEmbedded();
			BodyCreationSettings settings(&boxShape, pos, Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
			settings.mRestitution = 0.6f;
			settings.mFriction = 0.4f;
			settings.mLinearDamping = 0.05f;
			Body* body = bodyInterface->CreateBody(settings);
			bodyInterface->AddBody(body->GetID(), EActivation::Activate);
			bodyInterface->SetLinearVelocity(body->GetID(), Vec3(velDist(rng), -2.0f, velDist(rng)));
			dynamicBodies.push_back({
				body->GetID(),
				glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng)),
				false, halfSize * 2.0f
			});
		}

		physicsSystem.OptimizeBroadPhase();
	}

	void step(float deltaTime)
	{
		if (!bodyInterface) return;
		const float fixedDt = 1.0f / 60.0f;
		const int collisionSteps = 1;

		physicsSystem.Update(fixedDt, collisionSteps, tempAllocator, jobSystem);
		elapsedTime += deltaTime;

		if (elapsedTime >= RESET_INTERVAL) {
			createScene();
		}
	}

	void render(PhysicsRenderer& renderer, const glm::mat4& projection, const glm::mat4& view)
	{
		if (!bodyInterface) return;

		// Draw ground
		if (floorCreated) {
			glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
			model = glm::scale(model, glm::vec3(30.0f, 1.0f, 30.0f));
			renderer.drawBox(projection, view, model, glm::vec3(0.4f, 0.45f, 0.5f));
		}

		// Draw dynamic bodies
		for (auto& db : dynamicBodies) {
			if (!bodyInterface->IsActive(db.id)) continue;
			RVec3 pos = bodyInterface->GetCenterOfMassPosition(db.id);
			Quat rot = bodyInterface->GetRotation(db.id);
			glm::vec3 p((float)pos.GetX(), (float)pos.GetY(), (float)pos.GetZ());
			glm::quat q((float)rot.GetW(), (float)rot.GetX(), (float)rot.GetY(), (float)rot.GetZ());
			glm::mat4 model = glm::translate(glm::mat4(1.0f), p) * glm::mat4(q);

			if (db.isSphere) {
				float s = db.size * 2.0f;
				model = glm::scale(model, glm::vec3(s));
				renderer.drawSphere(projection, view, model, db.color);
			} else {
				float s = db.size;
				model = glm::scale(model, glm::vec3(s));
				renderer.drawBox(projection, view, model, db.color);
			}
		}
	}
};

// ============================================================================
// WebGLExample - Main application
// ============================================================================

class WebGLExample
{
public:
	int width, height;
	SDL_Window* window = nullptr;
	SDL_GLContext glContext = nullptr;

	PhysicsScene physicsScene;
	PhysicsRenderer physicsRenderer;
	GL3RmlUiOverlay rmluiOverlay;
	SimpleCamera camera;

	bool running = true;
	bool mouseDown = false;
	bool rmlui_passthrough = false;
	int lastMouseX = 0, lastMouseY = 0;
	bool pendingClick = false;
	int pendingClickX = 0, pendingClickY = 0;

	// RmlUi data model
	int bodyCount = 0;
	float fpsValue = 0.0f;
	float resetTimer = 0.0f;
	int resetCountValue = 0;
	Rml::DataModelHandle dataModelHandle;

	// FPS tracking
	int frameCount = 0;
	float fpsAccumulator = 0.0f;

	WebGLExample() : width(1280), height(720) {}

	bool init()
	{
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
			printf("SDL_Init Error: %s\n", SDL_GetError());
			return false;
		}

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		window = SDL_CreateWindow("JoltPhysics + RmlUi (WebGL)",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
		if (!window) { printf("SDL_CreateWindow Error: %s\n", SDL_GetError()); return false; }

		glContext = SDL_GL_CreateContext(window);
		SDL_GL_MakeCurrent(window, glContext);
		SDL_GL_SetSwapInterval(1);

		if (!RmlGL3::Initialize()) {
			printf("Failed to initialize RmlUi GL3 renderer\n");
			return false;
		}

		camera.aspect = (float)width / (float)height;
		camera.update();

		// Physics
		physicsScene.init();

		// Renderer
		physicsRenderer.init();

		// RmlUi overlay
		Rml::String dataRoot = Rml::String(ASSET_ROOT) + "/examples/JoltPhysics_rmlui_webgl/data/";
		rmluiOverlay.init(width, height, dataRoot);
		rmluiOverlay.loadFont(ASSET_ROOT "/external/RmlUi/Samples/assets/HarmonyOS_Sans_SC_Regular.ttf");

		// Data model for RmlUi - must be created BEFORE loading the document
		Rml::DataModelConstructor model = rmluiOverlay.getContext()->CreateDataModel("physics");
		if (model) {
			model.Bind("body_count", &bodyCount);
			model.Bind("fps", &fpsValue);
			model.Bind("reset_timer", &resetTimer);
			model.Bind("reset_count", &resetCountValue);
			dataModelHandle = model.GetModelHandle();
		}

		// Load document AFTER data model is created
		rmluiOverlay.loadDocument("overlay.rml");

#ifdef __EMSCRIPTEN__
		emscripten_set_wheel_callback("#canvas", nullptr, true, em_wheel_callback);
		EM_ASM({
			var canvas = document.getElementById('canvas');
			if (canvas) {
				canvas.focus();
				canvas.addEventListener('click', function() { canvas.focus(); });
			}
		});
#endif

		return true;
	}

	void handleEvents()
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
			case SDL_QUIT:
				running = false; break;
			case SDL_WINDOWEVENT:
				if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					width = ev.window.data1;
					height = ev.window.data2;
					camera.aspect = (float)width / (float)height;
					rmluiOverlay.resize(width, height);
				}
				break;
			case SDL_MOUSEBUTTONDOWN: {
				int mx = ev.button.x, my = ev.button.y;
				if (ev.button.button == SDL_BUTTON_LEFT) {
					pendingClick = true;
					pendingClickX = mx;
					pendingClickY = my;
					mouseDown = true;
					lastMouseX = mx; lastMouseY = my;
				} else if (ev.button.button == SDL_BUTTON_RIGHT) {
					mouseDown = true;
					lastMouseX = mx; lastMouseY = my;
				}
				break;
			}
			case SDL_MOUSEBUTTONUP:
				if (ev.button.button == SDL_BUTTON_LEFT) {
					if (!rmlui_passthrough)
						rmluiOverlay.processMouseButton(0, false);
					rmlui_passthrough = false;
					mouseDown = false;
				} else if (ev.button.button == SDL_BUTTON_RIGHT) {
					mouseDown = false;
				}
				break;
			case SDL_MOUSEMOTION: {
				int mx = ev.motion.x, my = ev.motion.y;
				if (!rmlui_passthrough)
					rmluiOverlay.processMouseMove(mx, my);
				if (mouseDown && rmlui_passthrough)
					camera.rotate(mx - lastMouseX, my - lastMouseY);
				lastMouseX = mx; lastMouseY = my;
				break;
			}
			case SDL_MOUSEWHEEL:
#ifndef __EMSCRIPTEN__
				if (!rmlui_passthrough) {
					if (rmluiOverlay.processMouseWheel((float)ev.wheel.y))
						camera.zoom((float)ev.wheel.y);
				} else {
					camera.zoom((float)ev.wheel.y);
				}
#endif
				break;
			case SDL_KEYDOWN: {
				Rml::Input::KeyIdentifier rmlKey = RmlSDL::ConvertKey(ev.key.keysym.sym);
				if (rmlKey != Rml::Input::KI_UNKNOWN)
					rmluiOverlay.processKeyDown(rmlKey);
				// Press R to manually reset
				if (ev.key.keysym.sym == SDLK_r)
					physicsScene.createScene();
				break;
			}
			case SDL_KEYUP: {
				Rml::Input::KeyIdentifier rmlKey = RmlSDL::ConvertKey(ev.key.keysym.sym);
				if (rmlKey != Rml::Input::KI_UNKNOWN)
					rmluiOverlay.processKeyUp(rmlKey);
				break;
			}
			}
		}

#ifdef __EMSCRIPTEN__
		if (g_pendingWheelDelta != 0.0f) {
			if (!rmlui_passthrough) {
				if (rmluiOverlay.processMouseWheel(g_pendingWheelDelta))
					camera.zoom(g_pendingWheelDelta);
			} else {
				camera.zoom(g_pendingWheelDelta);
			}
			g_pendingWheelDelta = 0.0f;
		}
#endif
	}

	void updateDataModel()
	{
		bodyCount = (int)physicsScene.dynamicBodies.size();
		resetTimer = physicsScene.RESET_INTERVAL - physicsScene.elapsedTime;
		if (resetTimer < 0) resetTimer = 0;
		resetCountValue = physicsScene.resetCount;

		dataModelHandle.DirtyVariable("body_count");
		dataModelHandle.DirtyVariable("fps");
		dataModelHandle.DirtyVariable("reset_timer");
		dataModelHandle.DirtyVariable("reset_count");
	}

	void render()
	{
		camera.update();
		rmluiOverlay.update();

		// 1) Render UI to offscreen FBO
		rmluiOverlay.renderOffscreen();

		// 2) Process pending click
		if (pendingClick) {
			uint8_t rgba[4];
			rmluiOverlay.readOffscreenPixel(pendingClickX, pendingClickY, rgba);
			if (rgba[3] < 10) {
				rmlui_passthrough = true;
			} else {
				rmlui_passthrough = false;
				rmluiOverlay.processMouseButton(0, true);
			}
			pendingClick = false;
		}

		// 3) Render 3D scene to default framebuffer
		glViewport(0, 0, width, height);
		glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);

		physicsScene.render(physicsRenderer, camera.projMatrix, camera.viewMatrix);

		// 4) Composite UI overlay on top
		rmluiOverlay.compositeTexture(rmluiOverlay.getColorTexture());

		glEnable(GL_DEPTH_TEST);
		SDL_GL_SwapWindow(window);
	}

	void mainLoopIteration()
	{
		float deltaTime = 1.0f / 60.0f;

		handleEvents();

		// Step physics
		physicsScene.step(deltaTime);

		// FPS tracking
		frameCount++;
		fpsAccumulator += deltaTime;
		if (fpsAccumulator >= 1.0f) {
			fpsValue = (float)frameCount / fpsAccumulator;
			frameCount = 0;
			fpsAccumulator = 0.0f;
		}

		updateDataModel();
		render();
	}

	void shutdown()
	{
		physicsScene.shutdown();
		physicsRenderer.shutdown();
		rmluiOverlay.shutdown();
		RmlGL3::Shutdown();
		if (glContext) SDL_GL_DeleteContext(glContext);
		if (window) SDL_DestroyWindow(window);
		SDL_Quit();
	}
};

// ============================================================================
// Entry Point
// ============================================================================

static WebGLExample* g_app = nullptr;

#ifdef __EMSCRIPTEN__
static void emscriptenMainLoop()
{
	g_app->mainLoopIteration();
}
#endif

int main(int, char**)
{
	WebGLExample app;
	if (!app.init()) {
		printf("Failed to initialize application\n");
		return 1;
	}

	g_app = &app;

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(emscriptenMainLoop, 0, 1);
#else
	while (app.running)
		app.mainLoopIteration();
#endif

	app.shutdown();
	return 0;
}
