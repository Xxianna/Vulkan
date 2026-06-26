/*
 * Vulkan Example - glTF scene rendering with RmlUi overlay (replacing ImGui)
 *
 * Demonstrates integrating RmlUi as a UI rendering layer using offscreen rendering
 * and texture compositing, with input forwarding from the Vulkan example's Win32 window.
 */

// Use the same tinygltf/stb_image configuration as the original gltfloading example
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"

// RmlUiOverlay.h MUST be included before vulkanexamplebase.h because it includes
// RmlUi's Vulkan wrapper (RmlUi_Vulkan/vulkan.h) which must come first.
#include "RmlUiOverlay.h"
#include "vulkanexamplebase.h"

// ============================================================================
// VulkanglTFModel - glTF scene model loader (from gltfloading example)
// ============================================================================

class VulkanglTFModel
{
public:
	vks::VulkanDevice* vulkanDevice;
	VkQueue copyQueue;

	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
	};

	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertices;

	struct {
		int count;
		VkBuffer buffer;
		VkDeviceMemory memory;
	} indices;

	struct Node;
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t materialIndex;
	};
	struct Mesh {
		std::vector<Primitive> primitives;
	};
	struct Node {
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 matrix;
		~Node() { for (auto& child : children) delete child; }
	};
	struct Material {
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		uint32_t baseColorTextureIndex;
	};
	struct Image {
		vks::Texture2D texture;
		VkDescriptorSet descriptorSet;
	};
	struct Texture {
		int32_t imageIndex;
	};

	std::vector<Image> images;
	std::vector<Texture> textures;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	~VulkanglTFModel()
	{
		for (auto node : nodes) delete node;
		vkDestroyBuffer(vulkanDevice->logicalDevice, vertices.buffer, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, vertices.memory, nullptr);
		vkDestroyBuffer(vulkanDevice->logicalDevice, indices.buffer, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, indices.memory, nullptr);
		for (Image image : images) {
			vkDestroyImageView(vulkanDevice->logicalDevice, image.texture.view, nullptr);
			vkDestroyImage(vulkanDevice->logicalDevice, image.texture.image, nullptr);
			vkDestroySampler(vulkanDevice->logicalDevice, image.texture.sampler, nullptr);
			vkFreeMemory(vulkanDevice->logicalDevice, image.texture.deviceMemory, nullptr);
		}
	}

	void loadImages(tinygltf::Model& input)
	{
		images.resize(input.images.size());
		for (size_t i = 0; i < input.images.size(); i++) {
			tinygltf::Image& glTFImage = input.images[i];
			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (glTFImage.component == 3) {
				bufferSize = glTFImage.width * glTFImage.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &glTFImage.image[0];
				for (size_t j = 0; j < glTFImage.width * glTFImage.height; ++j) {
					memcpy(rgba, rgb, sizeof(unsigned char) * 3);
					rgba += 4; rgb += 3;
				}
				deleteBuffer = true;
			} else {
				buffer = &glTFImage.image[0];
				bufferSize = glTFImage.image.size();
			}
			images[i].texture.fromBuffer(buffer, bufferSize, VK_FORMAT_R8G8B8A8_UNORM, glTFImage.width, glTFImage.height, vulkanDevice, copyQueue);
			if (deleteBuffer) delete[] buffer;
		}
	}

	void loadTextures(tinygltf::Model& input)
	{
		textures.resize(input.textures.size());
		for (size_t i = 0; i < input.textures.size(); i++)
			textures[i].imageIndex = input.textures[i].source;
	}

	void loadMaterials(tinygltf::Model& input)
	{
		materials.resize(input.materials.size());
		for (size_t i = 0; i < input.materials.size(); i++) {
			tinygltf::Material glTFMaterial = input.materials[i];
			if (glTFMaterial.values.find("baseColorFactor") != glTFMaterial.values.end())
				materials[i].baseColorFactor = glm::make_vec4(glTFMaterial.values["baseColorFactor"].ColorFactor().data());
			if (glTFMaterial.values.find("baseColorTexture") != glTFMaterial.values.end())
				materials[i].baseColorTextureIndex = glTFMaterial.values["baseColorTexture"].TextureIndex();
		}
	}

	void loadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer)
	{
		Node* node = new Node{};
		node->matrix = glm::mat4(1.0f);
		node->parent = parent;
		if (inputNode.translation.size() == 3) node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
		if (inputNode.rotation.size() == 4) { glm::quat q = glm::make_quat(inputNode.rotation.data()); node->matrix = node->matrix * glm::mat4(q); }
		if (inputNode.scale.size() == 3) node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
		if (inputNode.matrix.size() == 16) node->matrix = glm::make_mat4x4(inputNode.matrix.data());
		if (inputNode.children.size() > 0)
			for (size_t i = 0; i < inputNode.children.size(); i++)
				loadNode(input.nodes[inputNode.children[i]], input, node, indexBuffer, vertexBuffer);
		if (inputNode.mesh > -1) {
			const tinygltf::Mesh mesh = input.meshes[inputNode.mesh];
			for (size_t i = 0; i < mesh.primitives.size(); i++) {
				const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
				uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
				uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
				uint32_t indexCount = 0;
				{
					const float* positionBuffer = nullptr; const float* normalsBuffer = nullptr; const float* texCoordsBuffer = nullptr;
					size_t vertexCount = 0;
					if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
						const auto& accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
						const auto& view = input.bufferViews[accessor.bufferView];
						positionBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						vertexCount = accessor.count;
					}
					if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
						const auto& accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
						const auto& view = input.bufferViews[accessor.bufferView];
						normalsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}
					if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
						const auto& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
						const auto& view = input.bufferViews[accessor.bufferView];
						texCoordsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}
					for (size_t v = 0; v < vertexCount; v++) {
						Vertex vert{
							.pos = glm::make_vec3(&positionBuffer[v * 3]),
							.normal = normalsBuffer ? glm::normalize(glm::make_vec3(&normalsBuffer[v * 3])) : glm::vec3(0.0f),
							.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec2(0.0f),
							.color = glm::vec3(1.0f),
						};
						vertexBuffer.push_back(vert);
					}
				}
				{
					const auto& accessor = input.accessors[glTFPrimitive.indices];
					const auto& bufferView = input.bufferViews[accessor.bufferView];
					const auto& buffer = input.buffers[bufferView.buffer];
					indexCount += static_cast<uint32_t>(accessor.count);
					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t j = 0; j < accessor.count; j++) indexBuffer.push_back(buf[j] + vertexStart);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t j = 0; j < accessor.count; j++) indexBuffer.push_back(buf[j] + vertexStart);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t j = 0; j < accessor.count; j++) indexBuffer.push_back(buf[j] + vertexStart);
						break;
					}
					}
				}
				node->mesh.primitives.push_back({ firstIndex, indexCount, (int32_t)glTFPrimitive.material });
			}
		}
		if (parent) parent->children.push_back(node); else nodes.push_back(node);
	}

	void drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, Node* node)
	{
		if (node->mesh.primitives.size() > 0) {
			glm::mat4 nodeMatrix = node->matrix;
			Node* currentParent = node->parent;
			while (currentParent) { nodeMatrix = currentParent->matrix * nodeMatrix; currentParent = currentParent->parent; }
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeMatrix);
			for (auto& primitive : node->mesh.primitives) {
				if (primitive.indexCount > 0) {
					Texture texture = textures[materials[primitive.materialIndex].baseColorTextureIndex];
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &images[texture.imageIndex].descriptorSet, 0, nullptr);
					vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}
			}
		}
		for (auto& child : node->children) drawNode(commandBuffer, pipelineLayout, child);
	}

	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		for (auto& node : nodes) drawNode(commandBuffer, pipelineLayout, node);
	}
};

// ============================================================================
// VulkanExample - glTF rendering with RmlUi overlay
// ============================================================================

class VulkanExample : public VulkanExampleBase
{
public:
	bool wireframe = false;
	VulkanglTFModel glTFModel;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(5.0f, 5.0f, -5.0f, 1.0f);
		glm::vec4 viewPos;
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	struct { VkPipeline solid{ VK_NULL_HANDLE }; VkPipeline wireframe{ VK_NULL_HANDLE }; } pipelines;
	struct { VkDescriptorSetLayout matrices{ VK_NULL_HANDLE }; VkDescriptorSetLayout textures{ VK_NULL_HANDLE }; } descriptorSetLayouts;
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets{};

	// RmlUi overlay
	vks::RmlUiOverlay rmluiOverlay;

	// Fullscreen quad for compositing RmlUi output
	VkPipelineLayout overlayPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout overlayDescriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet overlayDescriptorSet{ VK_NULL_HANDLE };
	VkPipeline overlayPipeline{ VK_NULL_HANDLE };
	VkSampler overlaySampler{ VK_NULL_HANDLE };

	VulkanExample() : VulkanExampleBase()
	{
		title = "glTF model rendering with RmlUi";
		camera.type = Camera::CameraType::lookat;
		camera.flipY = true;
		camera.setPosition(glm::vec3(0.0f, -0.1f, -1.0f));
		camera.setRotation(glm::vec3(0.0f, 45.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = false; // Disable ImGui
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.solid, nullptr);
			if (pipelines.wireframe != VK_NULL_HANDLE) vkDestroyPipeline(device, pipelines.wireframe, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.matrices, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.textures, nullptr);
			for (auto& buffer : uniformBuffers) buffer.destroy();
			if (overlayPipeline) vkDestroyPipeline(device, overlayPipeline, nullptr);
			if (overlayPipelineLayout) vkDestroyPipelineLayout(device, overlayPipelineLayout, nullptr);
			if (overlayDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, overlayDescriptorSetLayout, nullptr);
			if (overlaySampler) vkDestroySampler(device, overlaySampler, nullptr);
		}
		rmluiOverlay.freeResources();
	}

	virtual void getEnabledFeatures()
	{
		if (deviceFeatures.fillModeNonSolid) enabledFeatures.fillModeNonSolid = VK_TRUE;
	}

	void loadAssets()
	{
		tinygltf::Model glTFInput;
		tinygltf::TinyGLTF gltfContext;
		std::string error, warning;
		bool fileLoaded = gltfContext.LoadASCIIFromFile(&glTFInput, &error, &warning, getAssetPath() + "models/FlightHelmet/glTF/FlightHelmet.gltf");
		glTFModel.vulkanDevice = vulkanDevice;
		glTFModel.copyQueue = queue;
		std::vector<uint32_t> indexBuffer;
		std::vector<VulkanglTFModel::Vertex> vertexBuffer;
		if (fileLoaded) {
			glTFModel.loadImages(glTFInput);
			glTFModel.loadMaterials(glTFInput);
			glTFModel.loadTextures(glTFInput);
			const tinygltf::Scene& scene = glTFInput.scenes[0];
			for (size_t i = 0; i < scene.nodes.size(); i++)
				glTFModel.loadNode(glTFInput.nodes[scene.nodes[i]], glTFInput, nullptr, indexBuffer, vertexBuffer);
		} else {
			vks::tools::exitFatal("Could not open the glTF file.", -1); return;
		}
		size_t vBufSize = vertexBuffer.size() * sizeof(VulkanglTFModel::Vertex);
		size_t iBufSize = indexBuffer.size() * sizeof(uint32_t);
		glTFModel.indices.count = static_cast<uint32_t>(indexBuffer.size());
		vks::Buffer vertexStaging, indexStaging;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexStaging, vBufSize, vertexBuffer.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indexStaging, iBufSize, indexBuffer.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vBufSize, &glTFModel.vertices.buffer, &glTFModel.vertices.memory));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, iBufSize, &glTFModel.indices.buffer, &glTFModel.indices.memory));
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};
		copyRegion.size = vBufSize; vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, glTFModel.vertices.buffer, 1, &copyRegion);
		copyRegion.size = iBufSize; vkCmdCopyBuffer(copyCmd, indexStaging.buffer, glTFModel.indices.buffer, 1, &copyRegion);
		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
		vertexStaging.destroy(); indexStaging.destroy();
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(glTFModel.images.size()) * maxConcurrentFrames + maxConcurrentFrames),
		};
		const uint32_t maxSetCount = (static_cast<uint32_t>(glTFModel.images.size()) + 2) * maxConcurrentFrames;
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxSetCount);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkDescriptorSetLayoutBinding setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&setLayoutBinding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.matrices));
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.textures));

		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.matrices, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}
		for (auto& image : glTFModel.images) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.textures, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &image.descriptorSet));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(image.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &image.texture.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Overlay descriptor set
		VkDescriptorSetLayoutBinding overlayBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VkDescriptorSetLayoutCreateInfo overlayLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&overlayBinding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &overlayLayoutCI, nullptr, &overlayDescriptorSetLayout));
		VkDescriptorSetAllocateInfo overlayAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &overlayDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &overlayAllocInfo, &overlayDescriptorSet));
	}

	void updateOverlayDescriptorSet()
	{
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.sampler = overlaySampler;
		imageInfo.imageView = rmluiOverlay.getOffscreenImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(overlayDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageInfo);
		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
	}

	void preparePipelines()
	{
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptorSetLayouts.matrices, descriptorSetLayouts.textures };
		VkPipelineLayoutCreateInfo pipelineLayoutCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 2, .pSetLayouts = setLayouts.data(), .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange };
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = { vks::initializers::vertexInputBindingDescription(0, sizeof(VulkanglTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX) };
		const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, normal)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanglTFModel::Vertex, uv)),
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, color)),
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = vertexInputBindings.data(), .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = vertexInputAttributes.data() };
		const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
			loadShader(getShadersPath() + "gltfloading/mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "gltfloading/mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		VkGraphicsPipelineCreateInfo pipelineCI{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages.data(), .pVertexInputState = &vertexInputStateCI, .pInputAssemblyState = &inputAssemblyStateCI, .pViewportState = &viewportStateCI, .pRasterizationState = &rasterizationStateCI, .pMultisampleState = &multisampleStateCI, .pDepthStencilState = &depthStencilStateCI, .pColorBlendState = &colorBlendStateCI, .pDynamicState = &dynamicStateCI, .layout = pipelineLayout, .renderPass = renderPass };
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.solid));
		if (deviceFeatures.fillModeNonSolid) {
			rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE; rasterizationStateCI.lineWidth = 1.0f;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.wireframe));
		}

		// Overlay pipeline
		VkPipelineLayoutCreateInfo overlayPLCI = vks::initializers::pipelineLayoutCreateInfo(&overlayDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &overlayPLCI, nullptr, &overlayPipelineLayout));
		VkPipelineColorBlendAttachmentState overlayBlend = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_TRUE);
		overlayBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; overlayBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		overlayBlend.colorBlendOp = VK_BLEND_OP_ADD; overlayBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; overlayBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		VkPipelineColorBlendStateCreateInfo overlayBlendCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &overlayBlend);
		VkPipelineDepthStencilStateCreateInfo overlayDSCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
		VkPipelineVertexInputStateCreateInfo overlayVICI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL; rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		const std::array<VkPipelineShaderStageCreateInfo, 2> overlayShaders = {
			loadShader(getShadersPath() + "gltfloading_rmlui/overlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "gltfloading_rmlui/overlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		VkGraphicsPipelineCreateInfo overlayCI{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = overlayShaders.data(), .pVertexInputState = &overlayVICI, .pInputAssemblyState = &inputAssemblyStateCI, .pViewportState = &viewportStateCI, .pRasterizationState = &rasterizationStateCI, .pMultisampleState = &multisampleStateCI, .pDepthStencilState = &overlayDSCI, .pColorBlendState = &overlayBlendCI, .pDynamicState = &dynamicStateCI, .layout = overlayPipelineLayout, .renderPass = renderPass };
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &overlayCI, nullptr, &overlayPipeline));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData), &uniformData));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void updateUniformBuffers()
	{
		uniformData.projection = camera.matrices.perspective;
		uniformData.model = camera.matrices.view;
		uniformData.viewPos = camera.viewPos;
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
	}

	void setupRmlUi()
	{
		uint32_t queueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
		rmluiOverlay.prepare(instance, vulkanDevice, queue, queueFamilyIndex, width, height);

		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = VK_FILTER_LINEAR; samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.addressModeU = samplerCI.addressModeV = samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &overlaySampler));

		Rml::LoadFontFace("E:/prj/bim_ntv/Vulkan/external/RmlUi/Samples/assets/HarmonyOS_Sans_SC_Regular.ttf");

		Rml::ElementDocument* doc = rmluiOverlay.getContext()->LoadDocument("overlay.rml");
		if (doc) {
			doc->Show();
			Rml::DataModelConstructor model = rmluiOverlay.getContext()->CreateDataModel("settings");
			if (model) {
				model.Bind("wireframe", &wireframe);
			}
		}
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		setupRmlUi();
		updateOverlayDescriptorSet();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VkClearValue clearValues[2] = { {{0.25f, 0.25f, 0.25f, 1.0f}}, {1.0f, 0} };
		VkRenderPassBeginInfo renderPassBeginInfo{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass, .framebuffer = frameBuffers[currentImageIndex], .renderArea = {{0,0}, {width, height}}, .clearValueCount = 2, .pClearValues = clearValues };
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, nullptr);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? pipelines.wireframe : pipelines.solid);
		glTFModel.draw(cmdBuffer, pipelineLayout);
		// Composite RmlUi overlay
		if (rmluiOverlay.visible) {
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipelineLayout, 0, 1, &overlayDescriptorSet, 0, nullptr);
			vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
		}
		vkCmdEndRenderPass(cmdBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	virtual void render()
	{
		if (!prepared) return;
		rmluiOverlay.update();
		rmluiOverlay.render();
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		updateOverlayDescriptorSet();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	// Input forwarding to RmlUi
	virtual void mouseMoved(double x, double y, bool &handled)
	{
		rmluiOverlay.processMouseMove((float)x, (float)y);
		if (rmluiOverlay.wantsCaptureMouse()) handled = true;
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		Rml::Input::KeyIdentifier rmlKey = vks::RmlUiOverlay::convertKey((int)keyCode);
		if (rmlKey != Rml::Input::KI_UNKNOWN)
			rmluiOverlay.processKeyDown(rmlKey, vks::RmlUiOverlay::getKeyModifiers());
	}

	virtual void OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		switch (uMsg) {
		case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
			rmluiOverlay.processMouseButton(uMsg == WM_LBUTTONDOWN ? 0 : (uMsg == WM_RBUTTONDOWN ? 1 : 2), true); break;
		case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
			rmluiOverlay.processMouseButton(uMsg == WM_LBUTTONUP ? 0 : (uMsg == WM_RBUTTONUP ? 1 : 2), false); break;
		case WM_MOUSEWHEEL:
			rmluiOverlay.processMouseWheel((float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA); break;
		case WM_CHAR:
			rmluiOverlay.processTextInput((Rml::Character)wParam); break;
		case WM_KEYUP: {
			Rml::Input::KeyIdentifier rmlKey = vks::RmlUiOverlay::convertKey((int)wParam);
			if (rmlKey != Rml::Input::KI_UNKNOWN)
				rmluiOverlay.processKeyUp(rmlKey, vks::RmlUiOverlay::getKeyModifiers());
			break;
		}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
