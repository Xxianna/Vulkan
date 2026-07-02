/*
 * WebGL Example - glTF scene rendering with RmlUi overlay
 *
 * Emscripten/WebGL port of the Vulkan gltfloading_rmlui example.
 * Uses SDL2 for windowing, OpenGL ES 3.0 for 3D rendering,
 * and RmlUi GL3 backend for UI overlay with offscreen FBO compositing.
 */

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include "../external/RmlUi/Backends/RmlUi_Renderer_GL3.h"
#include "../external/RmlUi/Backends/RmlUi_Platform_SDL.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
out vec3 outNormal;
out vec3 outColor;
out vec2 outUV;
out vec3 outViewVec;
out vec3 outLightVec;
void main() {
	outNormal = inNormal;
	outColor = inColor;
	outUV = inUV;
	gl_Position = projection * view * model * vec4(inPos.xyz, 1.0);
	vec4 pos = view * vec4(inPos, 1.0);
	outNormal = mat3(view) * inNormal;
	outLightVec = vec3(view * vec4(5.0, 5.0, -5.0, 1.0)) - pos.xyz;
	outViewVec = -pos.xyz;
}
)";

static const char* mesh_frag_src = R"(#version 300 es
precision highp float;
in vec3 outNormal;
in vec3 outColor;
in vec2 outUV;
in vec3 outViewVec;
in vec3 outLightVec;
uniform sampler2D samplerColorMap;
layout (location = 0) out vec4 fragColor;
void main() {
	vec4 color = texture(samplerColorMap, outUV) * vec4(outColor, 1.0);
	vec3 N = normalize(outNormal);
	vec3 L = normalize(outLightVec);
	vec3 V = normalize(outViewVec);
	vec3 R = reflect(L, N);
	vec3 diffuse = max(dot(N, L), 0.15) * outColor;
	vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.75);
	fragColor = vec4(diffuse * color.rgb + specular, 1.0);
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
// RmlUiFileInterface - Standard C file I/O (works with Emscripten VFS)
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
// Custom GL3 Render Interface - uses stb_image instead of SDL_image
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

		// Convert to premultiplied alpha
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
// GL3glTFModel - glTF model loading and rendering with OpenGL ES 3.0
// ============================================================================

class GL3glTFModel
{
public:
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
	};

	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t materialIndex;
	};
	struct Mesh { std::vector<Primitive> primitives; };
	struct Node {
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 matrix;
		~Node() { for (auto c : children) delete c; }
	};
	struct Material {
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		int32_t baseColorTextureIndex = -1;
	};

	GLuint vao = 0, vbo = 0, ibo = 0;
	uint32_t indexCount = 0;
	GLuint shaderProgram = 0;
	GLint locProjection = -1, locView = -1, locModel = -1;

	struct Image {
		GLuint texture = 0;
	};
	std::vector<Image> images;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	struct TexRef { int32_t source; };
	std::vector<TexRef> textures;

	~GL3glTFModel()
	{
		if (vao) glDeleteVertexArrays(1, &vao);
		if (vbo) glDeleteBuffers(1, &vbo);
		if (ibo) glDeleteBuffers(1, &ibo);
		if (shaderProgram) glDeleteProgram(shaderProgram);
		for (auto& img : images)
			if (img.texture) glDeleteTextures(1, &img.texture);
		for (auto n : nodes) delete n;
	}

	void initShader()
	{
		shaderProgram = linkProgram(mesh_vert_src, mesh_frag_src);
		locProjection = glGetUniformLocation(shaderProgram, "projection");
		locView = glGetUniformLocation(shaderProgram, "view");
		locModel = glGetUniformLocation(shaderProgram, "model");
	}

	void uploadBuffers(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		indexCount = (uint32_t)indices.size();
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ibo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
		glBindVertexArray(0);
	}

	void loadImages(tinygltf::Model& input)
	{
		images.resize(input.images.size());
		for (size_t i = 0; i < input.images.size(); i++) {
			auto& glTFImage = input.images[i];
			unsigned char* buffer;
			int w = glTFImage.width, h = glTFImage.height;
			if (glTFImage.component == 3) {
				buffer = new unsigned char[w * h * 4];
				for (int j = 0; j < w * h; j++) {
					memcpy(buffer + j * 4, &glTFImage.image[j * 3], 3);
					buffer[j * 4 + 3] = 255;
				}
			} else {
				buffer = (unsigned char*)&glTFImage.image[0];
			}
			glGenTextures(1, &images[i].texture);
			glBindTexture(GL_TEXTURE_2D, images[i].texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
			if (glTFImage.component == 3) delete[] buffer;
		}
	}

	void loadTextures(tinygltf::Model& input)
	{
		textures.resize(input.textures.size());
		for (size_t i = 0; i < input.textures.size(); i++)
			textures[i].source = input.textures[i].source;
	}

	void loadMaterials(tinygltf::Model& input)
	{
		materials.resize(input.materials.size());
		for (size_t i = 0; i < input.materials.size(); i++) {
			auto& m = input.materials[i];
			if (m.values.count("baseColorFactor"))
				materials[i].baseColorFactor = glm::vec4(glm::make_vec4(m.values["baseColorFactor"].ColorFactor().data()));
			if (m.values.count("baseColorTexture"))
				materials[i].baseColorTextureIndex = m.values["baseColorTexture"].TextureIndex();
		}
	}

	void loadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input,
		Node* parent, std::vector<uint32_t>& indexBuf, std::vector<Vertex>& vertexBuf)
	{
		Node* node = new Node{};
		node->matrix = glm::mat4(1.0f);
		node->parent = parent;
		if (inputNode.translation.size() == 3)
			node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
		if (inputNode.rotation.size() == 4) {
			glm::quat q(glm::make_quat(inputNode.rotation.data()));
			node->matrix = node->matrix * glm::mat4(q);
		}
		if (inputNode.scale.size() == 3)
			node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
		if (inputNode.matrix.size() == 16)
			node->matrix = glm::mat4(glm::make_mat4x4(inputNode.matrix.data()));
		if (inputNode.children.size() > 0)
			for (size_t i = 0; i < inputNode.children.size(); i++)
				loadNode(input.nodes[inputNode.children[i]], input, node, indexBuf, vertexBuf);

		if (inputNode.mesh > -1) {
			auto& mesh = input.meshes[inputNode.mesh];
			for (auto& prim : mesh.primitives) {
				uint32_t firstIndex = (uint32_t)indexBuf.size();
				uint32_t vertexStart = (uint32_t)vertexBuf.size();
				uint32_t idxCount = 0;

				const float *posBuf = nullptr, *normBuf = nullptr, *uvBuf = nullptr;
				size_t vertCount = 0;
				auto getAccessor = [&](const char* name) -> const tinygltf::Accessor* {
					auto it = prim.attributes.find(name);
					if (it == prim.attributes.end()) return nullptr;
					return &input.accessors[it->second];
				};
				if (auto* acc = getAccessor("POSITION")) {
					auto& view = input.bufferViews[acc->bufferView];
					posBuf = reinterpret_cast<const float*>(&input.buffers[view.buffer].data[acc->byteOffset + view.byteOffset]);
					vertCount = acc->count;
				}
				if (auto* acc = getAccessor("NORMAL")) {
					auto& view = input.bufferViews[acc->bufferView];
					normBuf = reinterpret_cast<const float*>(&input.buffers[view.buffer].data[acc->byteOffset + view.byteOffset]);
				}
				if (auto* acc = getAccessor("TEXCOORD_0")) {
					auto& view = input.bufferViews[acc->bufferView];
					uvBuf = reinterpret_cast<const float*>(&input.buffers[view.buffer].data[acc->byteOffset + view.byteOffset]);
				}
				for (size_t v = 0; v < vertCount; v++) {
					vertexBuf.push_back({
						posBuf ? glm::make_vec3(&posBuf[v * 3]) : glm::vec3(0.0f),
						normBuf ? glm::normalize(glm::make_vec3(&normBuf[v * 3])) : glm::vec3(0.0f),
						uvBuf ? glm::make_vec2(&uvBuf[v * 2]) : glm::vec2(0.0f),
						glm::vec3(1.0f),
					});
				}

				if (prim.indices >= 0) {
					auto& acc = input.accessors[prim.indices];
					auto& bv = input.bufferViews[acc.bufferView];
					auto& buf = input.buffers[bv.buffer];
					idxCount = (uint32_t)acc.count;
					switch (acc.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						auto* p = reinterpret_cast<const uint32_t*>(&buf.data[acc.byteOffset + bv.byteOffset]);
						for (size_t j = 0; j < idxCount; j++) indexBuf.push_back(p[j] + vertexStart);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						auto* p = reinterpret_cast<const uint16_t*>(&buf.data[acc.byteOffset + bv.byteOffset]);
						for (size_t j = 0; j < idxCount; j++) indexBuf.push_back(p[j] + vertexStart);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						auto* p = reinterpret_cast<const uint8_t*>(&buf.data[acc.byteOffset + bv.byteOffset]);
						for (size_t j = 0; j < idxCount; j++) indexBuf.push_back(p[j] + vertexStart);
						break;
					}
					}
				}
				node->mesh.primitives.push_back({ firstIndex, idxCount, (int32_t)prim.material });
			}
		}
		if (parent) parent->children.push_back(node); else nodes.push_back(node);
	}

	void drawNode(Node* node, const glm::mat4& parentMatrix)
	{
		glm::mat4 nodeMatrix = parentMatrix * node->matrix;
		if (node->mesh.primitives.size() > 0) {
			glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(nodeMatrix));
			for (auto& prim : node->mesh.primitives) {
				if (prim.indexCount > 0 && prim.materialIndex >= 0) {
					int texIdx = -1;
					auto& mat = materials[prim.materialIndex];
					if (mat.baseColorTextureIndex >= 0)
						texIdx = textures[mat.baseColorTextureIndex].source;
					if (texIdx >= 0) {
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_2D, images[texIdx].texture);
					}
					glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, (void*)(prim.firstIndex * sizeof(uint32_t)));
				}
			}
		}
		for (auto& child : node->children) drawNode(child, nodeMatrix);
	}

	void draw(const glm::mat4& projection, const glm::mat4& view)
	{
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
		glBindVertexArray(vao);
		glm::mat4 identity(1.0f);
		for (auto& node : nodes) drawNode(node, identity);
		glBindVertexArray(0);
	}
};

// ============================================================================
// SimpleCamera - Orbit camera
// ============================================================================

class SimpleCamera
{
public:
	glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	float distance = 1.5f;
	float rotationX = 0.0f, rotationY = 45.0f;
	float fov = 60.0f, aspect = 1.0f, nearPlane = 0.1f, farPlane = 256.0f;
	bool flipY = true;
	glm::mat4 viewMatrix = glm::mat4(1.0f);
	glm::mat4 projMatrix = glm::mat4(1.0f);
	glm::vec4 viewPos = glm::vec4(0.0f);

	void update()
	{
		float pitch = glm::radians(rotationX);
		float yaw = glm::radians(rotationY);
		glm::vec3 position;
		position.x = target.x - distance * cos(pitch) * sin(yaw);
		position.y = target.y + distance * sin(pitch);
		position.z = target.z + distance * cos(pitch) * cos(yaw);
		viewMatrix = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
		viewPos = glm::vec4(position, 1.0f);
		float fovRad = glm::radians(fov);
		projMatrix = glm::perspective(fovRad, aspect, nearPlane, farPlane);
		if (flipY) projMatrix[1][1] *= -1.0f;
	}

	void rotate(float dx, float dy) { rotationY += dx * 0.3f; rotationX += dy * 0.3f; }
	void zoom(float delta) { distance -= delta * 0.5f; if (distance < 0.1f) distance = 0.1f; }
	void translate(float dx, float dy)
	{
		float yaw = glm::radians(rotationY);
		target.x -= dx * distance * 0.002f * cos(yaw);
		target.z -= dx * distance * 0.002f * sin(yaw);
		target.y += dy * distance * 0.002f;
	}
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

		// File interface
		file_interface = new RmlUiFileInterface(dataRoot);
		Rml::SetFileInterface(file_interface);
		Rml::Initialise();

		// Render interface
		render_interface = new RenderInterface_GL3_WebGL();
		Rml::SetRenderInterface(render_interface);

		// System interface
		Rml::SetSystemInterface(&system_interface);

		// Context
		context = Rml::CreateContext("main", Rml::Vector2i(w, h));

		// Overlay compositing shader and fullscreen triangle VAO/VBO
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

	void loadFont(const char* path)
	{
		Rml::LoadFontFace(path);
	}

	void loadDocument(const char* path)
	{
		Rml::ElementDocument* doc = context->LoadDocument(path);
		if (doc) doc->Show();
	}

	void resize(int w, int h)
	{
		if (w <= 0 || h <= 0) return;
		width = w; height = h;
		if (context) context->SetDimensions(Rml::Vector2i(w, h));
		destroyFramebuffer();
		createFramebuffer(w, h);
	}

	void processMouseMove(int x, int y) { if (context) context->ProcessMouseMove(x, y, 0); }
	void processMouseButton(int button, bool down)
	{
		if (!context) return;
		if (down) context->ProcessMouseButtonDown(button, 0);
		else context->ProcessMouseButtonUp(button, 0);
	}
	bool processMouseWheel(float delta) { if (context) return context->ProcessMouseWheel(-delta, 0); return true; }
	void processKeyDown(Rml::Input::KeyIdentifier key) { if (context) context->ProcessKeyDown(key, 0); }
	void processKeyUp(Rml::Input::KeyIdentifier key) { if (context) context->ProcessKeyUp(key, 0); }
	void processTextInput(Rml::Character ch) { if (context) context->ProcessTextInput(ch); }
	bool wantsCaptureMouse() const { return context && context->IsMouseInteracting(); }

	void readOffscreenPixel(int x, int y, uint8_t* rgba)
	{
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
		if (x < 0 || x >= width || y < 0 || y >= height) return;
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void update() { if (context && visible) context->Update(); }

	void render()
	{
		if (!context || !visible) return;

		// Render RmlUi to overlay FBO
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, width, height);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		render_interface->SetViewport(width, height);
		render_interface->BeginFrame();
		context->Render();
		render_interface->EndFrame();

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Composite overlay texture onto default framebuffer
		glViewport(0, 0, width, height);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);

		glUseProgram(overlayProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, colorTexture);
		glUniform1i(locSamplerUI, 0);
		glBindVertexArray(overlayVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}

	Rml::Context* getContext() const { return context; }
	int getWidth() const { return width; }
	int getHeight() const { return height; }

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
// Emscripten Wheel Event Fallback
// SDL_MOUSEWHEEL may not fire in all browsers if the canvas lacks focus.
// This uses Emscripten's HTML5 API to capture wheel events directly.
// ============================================================================

#ifdef __EMSCRIPTEN__
static float g_pendingWheelDelta = 0.0f;

static EM_BOOL em_wheel_callback(int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData)
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
// WebGLExample - Main application
// ============================================================================

class WebGLExample
{
public:
	int width, height;
	SDL_Window* window = nullptr;
	SDL_GLContext glContext = nullptr;

	GL3glTFModel glTFModel;
	GL3RmlUiOverlay rmluiOverlay;
	SimpleCamera camera;

	bool wireframe = false;
	bool rmlui_passthrough = false;
	bool running = true;
	bool mouseDown = false;
	bool middleMouseDown = false;
	int lastMouseX = 0, lastMouseY = 0;

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

		window = SDL_CreateWindow("glTF + RmlUi (WebGL)",
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
		camera.flipY = false;
		camera.update();

		// glTF model
		glTFModel.initShader();
		loadAssets();

		// RmlUi overlay
		Rml::String dataRoot = Rml::String(ASSET_ROOT) + "/examples/gltfloading_rmlui/data/";
		rmluiOverlay.init(width, height, dataRoot);
		rmluiOverlay.loadFont(ASSET_ROOT "/external/RmlUi/Samples/assets/HarmonyOS_Sans_SC_Regular.ttf");
		rmluiOverlay.loadDocument("overlay.rml");

		// Data model for wireframe toggle
		Rml::DataModelConstructor model = rmluiOverlay.getContext()->CreateDataModel("settings");
		if (model) model.Bind("wireframe", &wireframe);

#ifdef __EMSCRIPTEN__
		// Register wheel event callback directly on canvas for reliable capture
		emscripten_set_wheel_callback("#canvas", nullptr, true, em_wheel_callback);
		// Focus canvas so it receives input events
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

	void loadAssets()
	{
		tinygltf::Model glTFInput;
		tinygltf::TinyGLTF gltfContext;
		std::string error, warning;
		std::string assetPath = std::string(ASSET_ROOT) + "/assets/";
		bool loaded = gltfContext.LoadASCIIFromFile(&glTFInput, &error, &warning,
			assetPath + "models/FlightHelmet/glTF/FlightHelmet.gltf");

		std::vector<uint32_t> indexBuffer;
		std::vector<GL3glTFModel::Vertex> vertexBuffer;
		if (loaded) {
			glTFModel.loadImages(glTFInput);
			glTFModel.loadMaterials(glTFInput);
			glTFModel.loadTextures(glTFInput);
			const auto& scene = glTFInput.scenes[0];
			for (size_t i = 0; i < scene.nodes.size(); i++)
				glTFModel.loadNode(glTFInput.nodes[scene.nodes[i]], glTFInput, nullptr, indexBuffer, vertexBuffer);
		} else {
			printf("Failed to load glTF: %s\n", error.c_str());
			return;
		}
		glTFModel.uploadBuffers(vertexBuffer, indexBuffer);
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
					uint8_t rgba[4];
					rmluiOverlay.readOffscreenPixel(mx, my, rgba);
					if (rgba[3] < 10) {
						rmlui_passthrough = true;
					} else {
						rmlui_passthrough = false;
						rmluiOverlay.processMouseButton(0, true);
					}
					mouseDown = true;
					lastMouseX = mx; lastMouseY = my;
				} else if (ev.button.button == SDL_BUTTON_MIDDLE) {
					middleMouseDown = true;
					lastMouseX = mx; lastMouseY = my;
				}
				break;
			}
			case SDL_MOUSEBUTTONUP:
				if (ev.button.button == SDL_BUTTON_LEFT) {
					rmlui_passthrough = false;
					rmluiOverlay.processMouseButton(0, false);
					mouseDown = false;
				} else if (ev.button.button == SDL_BUTTON_MIDDLE) {
					middleMouseDown = false;
				}
				break;
			case SDL_MOUSEMOTION: {
				int mx = ev.motion.x, my = ev.motion.y;
				if (!rmlui_passthrough)
					rmluiOverlay.processMouseMove(mx, my);
				if (mouseDown && rmlui_passthrough)
					camera.rotate(mx - lastMouseX, my - lastMouseY);
				if (middleMouseDown)
					camera.translate((float)(mx - lastMouseX), (float)(my - lastMouseY));
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
				break;
			}
			case SDL_KEYUP: {
				Rml::Input::KeyIdentifier rmlKey = RmlSDL::ConvertKey(ev.key.keysym.sym);
				if (rmlKey != Rml::Input::KI_UNKNOWN)
					rmluiOverlay.processKeyUp(rmlKey);
				break;
			}
			case SDL_TEXTINPUT:
				if (!rmlui_passthrough)
					rmluiOverlay.processTextInput((Rml::Character)ev.text.text[0]);
				break;
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

	void render()
	{
		camera.update();

		rmluiOverlay.update();

		// Clear default framebuffer
		glViewport(0, 0, width, height);
		glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		// Draw glTF model
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glTFModel.draw(camera.projMatrix, camera.viewMatrix);

		// Composite RmlUi overlay
		rmluiOverlay.render();

		SDL_GL_SwapWindow(window);
	}

	void mainLoopIteration()
	{
		handleEvents();
		render();
	}

	void shutdown()
	{
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
