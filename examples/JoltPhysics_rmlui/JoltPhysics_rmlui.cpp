/*
 * Vulkan Example - JoltPhysics rigid body simulation with RmlUi overlay
 *
 * Demonstrates integrating JoltPhysics for rigid body simulation with RmlUi
 * as a UI overlay showing simulation statistics. The scene resets every 5 seconds.
 */

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

#include <cstdarg>

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

#include "RmlUiOverlay.h"
#include "vulkanexamplebase.h"

#include <RmlUi/Core/DataModelHandle.h>

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
// PhysicsScene - Jolt Physics simulation
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

		physicsSystem.Init(256, 0, 1024, 1024,
			broadPhaseLayerInterface, objectVsBroadPhaseFilter, objectLayerFilter);

		bodyInterface = &physicsSystem.GetBodyInterface();
		createScene();
	}

	void shutdown()
	{
		removeAllBodies();
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

		std::uniform_real_distribution<float> posDist(-8.0f, 8.0f);
		std::uniform_real_distribution<float> sizeDist(0.3f, 1.0f);
		std::uniform_real_distribution<float> heightDist(5.0f, 20.0f);
		std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);
		std::uniform_real_distribution<float> velDist(-3.0f, 3.0f);

		for (int i = 0; i < 15; i++) {
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
			dynamicBodies.push_back({ body->GetID(), glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng)), true, radius });
		}

		for (int i = 0; i < 15; i++) {
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
			dynamicBodies.push_back({ body->GetID(), glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng)), false, halfSize * 2.0f });
		}

		physicsSystem.OptimizeBroadPhase();
	}

	void step(float deltaTime)
	{
		if (!bodyInterface) return;
		physicsSystem.Update(1.0f / 60.0f, 1, tempAllocator, jobSystem);
		elapsedTime += deltaTime;
		if (elapsedTime >= RESET_INTERVAL)
			createScene();
	}
};

// ============================================================================
// VulkanExample
// ============================================================================

class VulkanExample : public VulkanExampleBase
{
public:
	bool rmlui_passthrough = false;

	PhysicsScene physicsScene;

	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
	};

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 lightPos = glm::vec4(5.0f, 10.0f, -5.0f, 1.0f);
		glm::vec4 viewPos;
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	// Mesh pipelines (one for opaque objects)
	VkPipelineLayout meshPipelineLayout{ VK_NULL_HANDLE };
	VkPipeline meshPipeline{ VK_NULL_HANDLE };

	// Shared mesh vertex/index buffers
	VkBuffer cubeVBO{ VK_NULL_HANDLE }; VkDeviceMemory cubeVBOMem{ VK_NULL_HANDLE };
	VkBuffer cubeIBO{ VK_NULL_HANDLE }; VkDeviceMemory cubeIBOMem{ VK_NULL_HANDLE };
	uint32_t cubeIndexCount = 0;
	VkBuffer sphereVBO{ VK_NULL_HANDLE }; VkDeviceMemory sphereVBOMem{ VK_NULL_HANDLE };
	VkBuffer sphereIBO{ VK_NULL_HANDLE }; VkDeviceMemory sphereIBOMem{ VK_NULL_HANDLE };
	uint32_t sphereIndexCount = 0;

	VkDescriptorSetLayout uboDescriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> uboDescriptorSets{};
	VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };

	// RmlUi overlay
	vks::RmlUiOverlay rmluiOverlay;

	// Overlay compositing
	VkPipelineLayout overlayPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout overlayDescriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet overlayDescriptorSet{ VK_NULL_HANDLE };
	VkPipeline overlayPipeline{ VK_NULL_HANDLE };
	VkSampler overlaySampler{ VK_NULL_HANDLE };

	// RmlUi data model
	int bodyCount = 0;
	float fpsValue = 0.0f;
	float resetTimer = 0.0f;
	int resetCountValue = 0;
	Rml::DataModelHandle dataModelHandle;

	// FPS tracking
	int frameCount = 0;
	float fpsAccumulator = 0.0f;

	VulkanExample() : VulkanExampleBase()
	{
		title = "JoltPhysics + RmlUi";
		camera.type = Camera::CameraType::lookat;
		camera.flipY = true;
		camera.setPosition(glm::vec3(0.0f, 8.0f, -20.0f));
		camera.setRotation(glm::vec3(15.0f, 30.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = false;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, meshPipeline, nullptr);
			vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
			vkDestroyBuffer(device, cubeVBO, nullptr);
			vkFreeMemory(device, cubeVBOMem, nullptr);
			vkDestroyBuffer(device, cubeIBO, nullptr);
			vkFreeMemory(device, cubeIBOMem, nullptr);
			vkDestroyBuffer(device, sphereVBO, nullptr);
			vkFreeMemory(device, sphereVBOMem, nullptr);
			vkDestroyBuffer(device, sphereIBO, nullptr);
			vkFreeMemory(device, sphereIBOMem, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, nullptr);
			for (auto& buffer : uniformBuffers) buffer.destroy();
			if (overlayPipeline) vkDestroyPipeline(device, overlayPipeline, nullptr);
			if (overlayPipelineLayout) vkDestroyPipelineLayout(device, overlayPipelineLayout, nullptr);
			if (overlayDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, overlayDescriptorSetLayout, nullptr);
			if (overlaySampler) vkDestroySampler(device, overlaySampler, nullptr);
		}
		rmluiOverlay.freeResources();
		physicsScene.shutdown();
	}

	void createCubeMesh()
	{
		Vertex vertices[] = {
			{{ 0.5f,-0.5f,-0.5f},{ 1, 0, 0}},{{ 0.5f, 0.5f,-0.5f},{ 1, 0, 0}},
			{{ 0.5f, 0.5f, 0.5f},{ 1, 0, 0}},{{ 0.5f,-0.5f, 0.5f},{ 1, 0, 0}},
			{{-0.5f,-0.5f, 0.5f},{-1, 0, 0}},{{-0.5f, 0.5f, 0.5f},{-1, 0, 0}},
			{{-0.5f, 0.5f,-0.5f},{-1, 0, 0}},{{-0.5f,-0.5f,-0.5f},{-1, 0, 0}},
			{{-0.5f, 0.5f,-0.5f},{ 0, 1, 0}},{{-0.5f, 0.5f, 0.5f},{ 0, 1, 0}},
			{{ 0.5f, 0.5f, 0.5f},{ 0, 1, 0}},{{ 0.5f, 0.5f,-0.5f},{ 0, 1, 0}},
			{{-0.5f,-0.5f, 0.5f},{ 0,-1, 0}},{{-0.5f,-0.5f,-0.5f},{ 0,-1, 0}},
			{{ 0.5f,-0.5f,-0.5f},{ 0,-1, 0}},{{ 0.5f,-0.5f, 0.5f},{ 0,-1, 0}},
			{{ 0.5f,-0.5f, 0.5f},{ 0, 0, 1}},{{ 0.5f, 0.5f, 0.5f},{ 0, 0, 1}},
			{{-0.5f, 0.5f, 0.5f},{ 0, 0, 1}},{{-0.5f,-0.5f, 0.5f},{ 0, 0, 1}},
			{{-0.5f,-0.5f,-0.5f},{ 0, 0,-1}},{{-0.5f, 0.5f,-0.5f},{ 0, 0,-1}},
			{{ 0.5f, 0.5f,-0.5f},{ 0, 0,-1}},{{ 0.5f,-0.5f,-0.5f},{ 0, 0,-1}},
		};
		uint32_t indices[] = {
			0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
			12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23,
		};
		cubeIndexCount = 36;
		VkDeviceSize vSize = sizeof(vertices), iSize = sizeof(indices);
		vks::Buffer stagingV, stagingI;
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingV, vSize, vertices);
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingI, iSize, indices);
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vSize, &cubeVBO, &cubeVBOMem);
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, iSize, &cubeIBO, &cubeIBOMem);
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy region{};
		region.size = vSize; vkCmdCopyBuffer(copyCmd, stagingV.buffer, cubeVBO, 1, &region);
		region.size = iSize; vkCmdCopyBuffer(copyCmd, stagingI.buffer, cubeIBO, 1, &region);
		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
		stagingV.destroy(); stagingI.destroy();
	}

	void createSphereMesh(int sectors = 16, int stacks = 12)
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
		VkDeviceSize vSize = vertices.size() * sizeof(Vertex), iSize = indices.size() * sizeof(uint32_t);
		vks::Buffer stagingV, stagingI;
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingV, vSize, vertices.data());
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingI, iSize, indices.data());
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vSize, &sphereVBO, &sphereVBOMem);
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, iSize, &sphereIBO, &sphereIBOMem);
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy region{};
		region.size = vSize; vkCmdCopyBuffer(copyCmd, stagingV.buffer, sphereVBO, 1, &region);
		region.size = iSize; vkCmdCopyBuffer(copyCmd, stagingI.buffer, sphereIBO, 1, &region);
		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
		stagingV.destroy(); stagingI.destroy();
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, (uint32_t)(maxConcurrentFrames * 2));
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkDescriptorSetLayoutBinding uboBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		VkDescriptorSetLayoutCreateInfo layoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&uboBinding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &uboDescriptorSetLayout));

		for (auto i = 0u; i < maxConcurrentFrames; i++) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &uboDescriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uboDescriptorSets[i]));
			VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(uboDescriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor);
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		// Overlay descriptor
		VkDescriptorSetLayoutBinding overlayBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VkDescriptorSetLayoutCreateInfo overlayLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&overlayBinding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &overlayLayoutCI, nullptr, &overlayDescriptorSetLayout));
		VkDescriptorSetAllocateInfo overlayAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &overlayDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &overlayAllocInfo, &overlayDescriptorSet));
	}

	void updateOverlayDescriptorSet()
	{
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = overlaySampler;
		imageInfo.imageView = rmluiOverlay.getOffscreenImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(overlayDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageInfo);
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	void preparePipelines()
	{
		// Mesh pipeline
		VkPushConstantRange pushRanges[2] = {
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0),
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec3), sizeof(glm::mat4)),
		};
		VkPipelineLayoutCreateInfo plCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &uboDescriptorSetLayout, .pushConstantRangeCount = 2, .pPushConstantRanges = pushRanges };
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &plCI, nullptr, &meshPipelineLayout));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttach = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo blendCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttach);
		VkPipelineDepthStencilStateCreateInfo depthCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicEnables);
		std::vector<VkVertexInputBindingDescription> bindings = { vks::initializers::vertexInputBindingDescription(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX) };
		std::vector<VkVertexInputAttributeDescription> attributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)),
		};
		VkPipelineVertexInputStateCreateInfo vertexCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = bindings.data(), .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes.data() };
		std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
			loadShader(getShadersPath() + "JoltPhysics_rmlui/mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "JoltPhysics_rmlui/mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		VkGraphicsPipelineCreateInfo pipelineCI{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages.data(), .pVertexInputState = &vertexCI, .pInputAssemblyState = &inputAssemblyCI, .pViewportState = &viewportCI, .pRasterizationState = &rasterCI, .pMultisampleState = &multisampleCI, .pDepthStencilState = &depthCI, .pColorBlendState = &blendCI, .pDynamicState = &dynamicCI, .layout = meshPipelineLayout, .renderPass = renderPass };
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &meshPipeline));

		// Overlay pipeline
		VkPipelineLayoutCreateInfo overlayPLCI = vks::initializers::pipelineLayoutCreateInfo(&overlayDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &overlayPLCI, nullptr, &overlayPipelineLayout));
		VkPipelineColorBlendAttachmentState overlayBlend = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_TRUE);
		overlayBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; overlayBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		overlayBlend.colorBlendOp = VK_BLEND_OP_ADD; overlayBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; overlayBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		VkPipelineColorBlendStateCreateInfo overlayBlendCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &overlayBlend);
		VkPipelineDepthStencilStateCreateInfo overlayDSCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
		VkPipelineVertexInputStateCreateInfo overlayVICI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		rasterCI.polygonMode = VK_POLYGON_MODE_FILL; rasterCI.cullMode = VK_CULL_MODE_NONE;
		std::array<VkPipelineShaderStageCreateInfo, 2> overlayStages = {
			loadShader(getShadersPath() + "JoltPhysics_rmlui/overlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "JoltPhysics_rmlui/overlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		VkGraphicsPipelineCreateInfo overlayCI{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = overlayStages.data(), .pVertexInputState = &overlayVICI, .pInputAssemblyState = &inputAssemblyCI, .pViewportState = &viewportCI, .pRasterizationState = &rasterCI, .pMultisampleState = &multisampleCI, .pDepthStencilState = &overlayDSCI, .pColorBlendState = &overlayBlendCI, .pDynamicState = &dynamicCI, .layout = overlayPipelineLayout, .renderPass = renderPass };
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
		uniformData.view = camera.matrices.view;
		uniformData.viewPos = camera.viewPos;
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
	}

	void drawPhysicsBody(VkCommandBuffer cmd, const glm::mat4& model, const glm::vec3& color, bool isSphere)
	{
		struct PushData { glm::mat4 model; glm::vec3 color; } push = { model, color };
		vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &push.model);
		vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec3), &push.color);
		if (isSphere) {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, &sphereVBO, offsets);
			vkCmdBindIndexBuffer(cmd, sphereIBO, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, sphereIndexCount, 1, 0, 0, 0);
		} else {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, &cubeVBO, offsets);
			vkCmdBindIndexBuffer(cmd, cubeIBO, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, cubeIndexCount, 1, 0, 0, 0);
		}
	}

	void setupRmlUi()
	{
		uint32_t queueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
		rmluiOverlay.prepare(instance, vulkanDevice, queue, queueFamilyIndex, width, height);

		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = VK_FILTER_LINEAR; samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.addressModeU = samplerCI.addressModeV = samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &overlaySampler));

#ifdef VK_USE_PLATFORM_ANDROID_KHR
		rmluiOverlay.setAssetManager(androidApp->activity->assetManager);
		rmluiOverlay.loadFontFromAssets("rmlui_assets/HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS Sans SC");
#elif defined(VK_PROJECT_SOURCE_DIR)
		Rml::LoadFontFace(VK_PROJECT_SOURCE_DIR "/external/RmlUi/Samples/assets/HarmonyOS_Sans_SC_Regular.ttf");
#else
		Rml::LoadFontFace("external/RmlUi/Samples/assets/HarmonyOS_Sans_SC_Regular.ttf");
#endif

		// Data model must be created BEFORE loading the document
		Rml::DataModelConstructor model = rmluiOverlay.getContext()->CreateDataModel("physics");
		if (model) {
			model.Bind("body_count", &bodyCount);
			model.Bind("fps", &fpsValue);
			model.Bind("reset_timer", &resetTimer);
			model.Bind("reset_count", &resetCountValue);
			dataModelHandle = model.GetModelHandle();
		}

		Rml::ElementDocument* doc = rmluiOverlay.getContext()->LoadDocument(
#ifdef VK_PROJECT_SOURCE_DIR
			VK_PROJECT_SOURCE_DIR "/examples/JoltPhysics_rmlui/data/overlay.rml"
#else
			"examples/JoltPhysics_rmlui/data/overlay.rml"
#endif
		);
		if (doc) doc->Show();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		physicsScene.init();
		createCubeMesh();
		createSphereMesh();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		setupRmlUi();
		updateOverlayDescriptorSet();
		prepared = true;
	}

	void windowResized() override
	{
		rmluiOverlay.resize(width, height);
		updateOverlayDescriptorSet();
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

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VkClearValue clearValues[2] = { {{0.15f, 0.15f, 0.2f, 1.0f}}, {1.0f, 0} };
		VkRenderPassBeginInfo renderPassBI{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass, .framebuffer = frameBuffers[currentImageIndex], .renderArea = {{0,0}, {width, height}}, .clearValueCount = 2, .pClearValues = clearValues };
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		vkCmdBeginRenderPass(cmdBuffer, &renderPassBI, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout, 0, 1, &uboDescriptorSets[currentBuffer], 0, nullptr);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

		// Draw ground
		{
			glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
			model = glm::scale(model, glm::vec3(30.0f, 1.0f, 30.0f));
			drawPhysicsBody(cmdBuffer, model, glm::vec3(0.4f, 0.45f, 0.5f), false);
		}

		// Draw dynamic bodies
		auto* bi = physicsScene.bodyInterface;
		if (bi) {
			for (auto& db : physicsScene.dynamicBodies) {
				if (!bi->IsActive(db.id)) continue;
				RVec3 pos = bi->GetCenterOfMassPosition(db.id);
				Quat rot = bi->GetRotation(db.id);
				glm::vec3 p((float)pos.GetX(), (float)pos.GetY(), (float)pos.GetZ());
				glm::quat q((float)rot.GetW(), (float)rot.GetX(), (float)rot.GetY(), (float)rot.GetZ());
				glm::mat4 model = glm::translate(glm::mat4(1.0f), p) * glm::mat4(q);
				float s = db.isSphere ? db.size * 2.0f : db.size;
				model = glm::scale(model, glm::vec3(s));
				drawPhysicsBody(cmdBuffer, model, db.color, db.isSphere);
			}
		}

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
		float dt = 1.0f / 60.0f;
		physicsScene.step(frameTimer);
		rmluiOverlay.update();
		rmluiOverlay.render();

		// FPS tracking
		frameCount++;
		fpsAccumulator += frameTimer;
		if (fpsAccumulator >= 1.0f) {
			fpsValue = (float)frameCount / fpsAccumulator;
			frameCount = 0;
			fpsAccumulator = 0.0f;
		}

		updateDataModel();
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		updateOverlayDescriptorSet();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	// Input handling
	virtual void mouseMoved(double x, double y, bool &handled)
	{
		if (rmlui_passthrough) return;
		rmluiOverlay.processMouseMove((float)x, (float)y);
		if (rmluiOverlay.wantsCaptureMouse()) handled = true;
	}

	virtual void keyPressed(uint32_t keyCode)
	{
#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
		vks::RmlUiOverlay::updateModifierState((int)keyCode, true);
#endif
		Rml::Input::KeyIdentifier rmlKey = vks::RmlUiOverlay::convertKey((int)keyCode);
		if (rmlKey != Rml::Input::KI_UNKNOWN)
			rmluiOverlay.processKeyDown(rmlKey, vks::RmlUiOverlay::getKeyModifiers());
		// Press R to manually reset
		if (keyCode == 0x13) // VK_R on Windows, or 'R' key
			physicsScene.createScene();
	}

#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
	virtual void keyReleased(uint32_t keyCode) override
	{
		vks::RmlUiOverlay::updateModifierState((int)keyCode, false);
		Rml::Input::KeyIdentifier rmlKey = vks::RmlUiOverlay::convertKey((int)keyCode);
		if (rmlKey != Rml::Input::KI_UNKNOWN)
			rmluiOverlay.processKeyUp(rmlKey, vks::RmlUiOverlay::getKeyModifiers());
	}

	virtual bool mouseButtonPressed(int button, int x, int y) override
	{
		uint8_t rgba[4];
		rmluiOverlay.readOffscreenPixel(x, y, rgba);
		if (rgba[3] < 10) {
			rmlui_passthrough = true;
			return false;
		}
		rmlui_passthrough = false;
		rmluiOverlay.processMouseButton(button, true);
		return rmluiOverlay.wantsCaptureMouse();
	}

	virtual void mouseButtonReleased(int button, int x, int y) override
	{
		rmlui_passthrough = false;
		rmluiOverlay.processMouseButton(button, false);
	}

	virtual void mouseWheel(float delta) override
	{
		if (!rmlui_passthrough)
			rmluiOverlay.processMouseWheel(delta);
	}

	virtual void mouseLeave() override { rmlui_passthrough = false; }
#endif

#ifdef _WIN32
	virtual void OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		switch (uMsg) {
		case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
			int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
			uint8_t rgba[4];
			rmluiOverlay.readOffscreenPixel(mx, my, rgba);
			if (rgba[3] < 10) {
				rmlui_passthrough = true;
			} else {
				rmlui_passthrough = false;
				rmluiOverlay.processMouseButton(uMsg == WM_LBUTTONDOWN ? 0 : (uMsg == WM_RBUTTONDOWN ? 1 : 2), true);
			}
			break;
		}
		case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
			rmlui_passthrough = false;
			rmluiOverlay.processMouseButton(uMsg == WM_LBUTTONUP ? 0 : (uMsg == WM_RBUTTONUP ? 1 : 2), false); break;
		case WM_MOUSELEAVE:
			rmlui_passthrough = false; break;
		case WM_MOUSEWHEEL:
			if (!rmlui_passthrough)
				rmluiOverlay.processMouseWheel((float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA); break;
		case WM_CHAR:
			if (!rmlui_passthrough)
				rmluiOverlay.processTextInput((Rml::Character)wParam); break;
		case WM_KEYUP: {
			Rml::Input::KeyIdentifier rmlKey = vks::RmlUiOverlay::convertKey((int)wParam);
			if (rmlKey != Rml::Input::KI_UNKNOWN)
				rmluiOverlay.processKeyUp(rmlKey, vks::RmlUiOverlay::getKeyModifiers());
			if (wParam == 'R') physicsScene.createScene();
			break;
		}
		}
	}
#endif
};

VULKAN_EXAMPLE_MAIN()
