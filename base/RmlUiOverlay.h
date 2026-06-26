#pragma once

// Include RmlUi's Vulkan wrapper FIRST - it sets up vulkan.h with VMA
// before any other headers can include plain vulkan/vulkan.h
#include "../external/RmlUi/Backends/RmlUi_Include_Vulkan.h"
#include "../external/RmlUi/Backends/RmlUi_Renderer_VK.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>

// Forward declarations to avoid including VulkanDevice.h (which includes vulkan/vulkan.h)
namespace vks { class VulkanDevice; }

namespace vks
{
	class RmlUiFileInterface : public Rml::FileInterface
	{
	public:
		RmlUiFileInterface(const Rml::String& root);
		~RmlUiFileInterface();
		Rml::FileHandle Open(const Rml::String& path) override;
		void Close(Rml::FileHandle file) override;
		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
		bool Seek(Rml::FileHandle file, long offset, int origin) override;
		size_t Tell(Rml::FileHandle file) override;
	private:
		Rml::String root;
	};

	class RmlUiOverlay
	{
	public:
		bool visible{ true };

		RmlUiOverlay();
		~RmlUiOverlay();

		void prepare(VkInstance instance, vks::VulkanDevice* vulkanDevice, VkQueue graphicsQueue, uint32_t graphicsQueueFamily, int width, int height);
		void freeResources();

		void update();
		void render();

		VkImageView getOffscreenImageView() const;
		VkImage getOffscreenImage() const;
		VkSemaphore getRenderCompleteSemaphore() const;

		void resize(int width, int height);

		// Input forwarding
		void processMouseMove(float x, float y);
		void processMouseButton(int button, bool down);
		void processMouseWheel(float delta);
		void processKeyDown(Rml::Input::KeyIdentifier key, Rml::Input::KeyModifier modifier = static_cast<Rml::Input::KeyModifier>(0));
		void processKeyUp(Rml::Input::KeyIdentifier key, Rml::Input::KeyModifier modifier = static_cast<Rml::Input::KeyModifier>(0));
		void processTextInput(Rml::Character character);

		bool wantsCaptureMouse() const;
		bool wantsCaptureKeyboard() const;

		Rml::Context* getContext() const { return context; }

		static Rml::Input::KeyIdentifier convertKey(int keyCode);
		static Rml::Input::KeyModifier getKeyModifiers();

	private:
		Rml::Context* context{ nullptr };
		RenderInterface_VK render_interface;
		Rml::SystemInterface system_interface;
		Rml::UniquePtr<RmlUiFileInterface> file_interface;

		VmaAllocator vma_allocator{ VK_NULL_HANDLE };

		int rmlui_width{ 0 };
		int rmlui_height{ 0 };
	};
}
