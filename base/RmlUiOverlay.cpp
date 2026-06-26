#include "RmlUiOverlay.h"
#include "VulkanDevice.h"

#include <chrono>
#include <stdio.h>

namespace vks
{
	// File interface - resolves paths relative to a root directory (like ShellFileInterface in bim)
	RmlUiFileInterface::RmlUiFileInterface(const Rml::String& root) : root(root) {}
	RmlUiFileInterface::~RmlUiFileInterface() {}
	Rml::FileHandle RmlUiFileInterface::Open(const Rml::String& path)
	{
		FILE* fp = fopen((root + path).c_str(), "rb");
		if (fp) return (Rml::FileHandle)fp;
		fp = fopen(path.c_str(), "rb");
		return (Rml::FileHandle)fp;
	}
	void RmlUiFileInterface::Close(Rml::FileHandle file) { fclose((FILE*)file); }
	size_t RmlUiFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) { return fread(buffer, 1, size, (FILE*)file); }
	bool RmlUiFileInterface::Seek(Rml::FileHandle file, long offset, int origin) { return fseek((FILE*)file, offset, origin) == 0; }
	size_t RmlUiFileInterface::Tell(Rml::FileHandle file) { return ftell((FILE*)file); }

	// Simple system interface using std::chrono
	class RmlUiSystemInterface : public Rml::SystemInterface
	{
	public:
		RmlUiSystemInterface()
		{
			start_time = std::chrono::steady_clock::now();
		}

		double GetElapsedTime() override
		{
			auto now = std::chrono::steady_clock::now();
			return std::chrono::duration<double>(now - start_time).count();
		}

		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			const char* type_str = "";
			switch (type)
			{
			case Rml::Log::LT_ALWAYS: type_str = "ALWAYS"; break;
			case Rml::Log::LT_ERROR: type_str = "ERROR"; break;
			case Rml::Log::LT_ASSERT: type_str = "ASSERT"; break;
			case Rml::Log::LT_WARNING: type_str = "WARNING"; break;
			case Rml::Log::LT_INFO: type_str = "INFO"; break;
			case Rml::Log::LT_DEBUG: type_str = "DEBUG"; break;
			default: type_str = "UNKNOWN"; break;
			}
			printf("[RmlUi %s] %s\n", type_str, message.c_str());
			return true;
		}

	private:
		std::chrono::steady_clock::time_point start_time;
	};

	static RmlUiSystemInterface g_system_interface;

	RmlUiOverlay::RmlUiOverlay()
	{
	}

	RmlUiOverlay::~RmlUiOverlay()
	{
		freeResources();
	}

	void RmlUiOverlay::prepare(VkInstance instance, vks::VulkanDevice* vulkanDevice, VkQueue graphicsQueue, uint32_t graphicsQueueFamily, int width, int height)
	{
		rmlui_width = width;
		rmlui_height = height;

		// Create VMA allocator for RmlUi's renderer
		VmaVulkanFunctions vulkanFunctions = {};
		vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocator_info = {};
		allocator_info.physicalDevice = vulkanDevice->physicalDevice;
		allocator_info.device = vulkanDevice->logicalDevice;
		allocator_info.instance = instance;
		allocator_info.vulkanApiVersion = VK_API_VERSION_1_0;
		allocator_info.pVulkanFunctions = &vulkanFunctions;
		vmaCreateAllocator(&allocator_info, &vma_allocator);

		// Set up file interface (like Shell in bim example) - must be before Rml::Initialise()
		file_interface = Rml::MakeUnique<RmlUiFileInterface>("E:/prj/bim_ntv/Vulkan/examples/gltfloading_rmlui/data/");
		Rml::SetFileInterface(file_interface.get());

		// Initialize RmlUi
		Rml::Initialise();

		// Install system interface
		Rml::SetSystemInterface(&g_system_interface);

		// Initialize the offscreen render interface
		bool init_result = render_interface.Initialize_Offscreen(
			instance,
			vulkanDevice->physicalDevice,
			vulkanDevice->logicalDevice,
			vma_allocator,
			graphicsQueue,
			graphicsQueueFamily,
			width,
			height
		);

		if (!init_result)
		{
			printf("[RmlUiOverlay] Failed to initialize offscreen renderer\n");
			return;
		}

		// Create RmlUi context
		context = Rml::CreateContext("main", Rml::Vector2i(width, height), &render_interface);
		if (!context)
		{
			printf("[RmlUiOverlay] Failed to create RmlUi context\n");
			return;
		}

		printf("[RmlUiOverlay] Initialized successfully (%dx%d)\n", width, height);
	}

	void RmlUiOverlay::freeResources()
	{
		if (context)
		{
			Rml::RemoveContext(context->GetName());
			context = nullptr;
		}

		render_interface.Shutdown();

		if (vma_allocator)
		{
			vmaDestroyAllocator(vma_allocator);
			vma_allocator = VK_NULL_HANDLE;
		}

		Rml::Shutdown();
		file_interface.reset();
	}

	void RmlUiOverlay::update()
	{
		if (!context || !visible)
			return;

		context->Update();
	}

	void RmlUiOverlay::render()
	{
		if (!context || !visible)
			return;

		render_interface.BeginFrame();
		context->Render();
		render_interface.EndFrame();
		render_interface.WaitForOffscreenFence();
	}

	VkImageView RmlUiOverlay::getOffscreenImageView() const
	{
		return render_interface.GetOffscreenImageView();
	}

	VkImage RmlUiOverlay::getOffscreenImage() const
	{
		return render_interface.GetOffscreenImage();
	}

	VkSemaphore RmlUiOverlay::getRenderCompleteSemaphore() const
	{
		return render_interface.GetRenderCompleteSemaphore();
	}

	void RmlUiOverlay::resize(int width, int height)
	{
		if (width <= 0 || height <= 0)
			return;

		rmlui_width = width;
		rmlui_height = height;

		if (context)
		{
			context->SetDimensions(Rml::Vector2i(width, height));
		}

		render_interface.SetViewport(width, height);
	}

	void RmlUiOverlay::processMouseMove(float x, float y)
	{
		if (!context)
			return;
		context->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), static_cast<int>(getKeyModifiers()));
	}

	void RmlUiOverlay::processMouseButton(int button, bool down)
	{
		if (!context)
			return;

		Rml::Input::KeyModifier modifiers = getKeyModifiers();
		if (down)
			context->ProcessMouseButtonDown(button, static_cast<int>(modifiers));
		else
			context->ProcessMouseButtonUp(button, static_cast<int>(modifiers));
	}

	void RmlUiOverlay::processMouseWheel(float delta)
	{
		if (!context)
			return;
		context->ProcessMouseWheel(-delta, static_cast<int>(getKeyModifiers()));
	}

	void RmlUiOverlay::processKeyDown(Rml::Input::KeyIdentifier key, Rml::Input::KeyModifier modifier)
	{
		if (!context)
			return;
		context->ProcessKeyDown(key, static_cast<int>(modifier));
	}

	void RmlUiOverlay::processKeyUp(Rml::Input::KeyIdentifier key, Rml::Input::KeyModifier modifier)
	{
		if (!context)
			return;
		context->ProcessKeyUp(key, static_cast<int>(modifier));
	}

	void RmlUiOverlay::processTextInput(Rml::Character character)
	{
		if (!context)
			return;
		context->ProcessTextInput(character);
	}

	bool RmlUiOverlay::wantsCaptureMouse() const
	{
		if (!context)
			return false;
		return context->IsMouseInteracting();
	}

	bool RmlUiOverlay::wantsCaptureKeyboard() const
	{
		if (!context)
			return false;
		// RmlUi doesn't have a direct IsKeyboardInteracting, but we can check if any element has focus
		return context->GetFocusElement() != nullptr;
	}

	Rml::Input::KeyIdentifier RmlUiOverlay::convertKey(int keyCode)
	{
		// Win32 VK codes to RmlUi key mapping
		switch (keyCode)
		{
		case 0x30: return Rml::Input::KI_0;
		case 0x31: return Rml::Input::KI_1;
		case 0x32: return Rml::Input::KI_2;
		case 0x33: return Rml::Input::KI_3;
		case 0x34: return Rml::Input::KI_4;
		case 0x35: return Rml::Input::KI_5;
		case 0x36: return Rml::Input::KI_6;
		case 0x37: return Rml::Input::KI_7;
		case 0x38: return Rml::Input::KI_8;
		case 0x39: return Rml::Input::KI_9;
		case 'A': return Rml::Input::KI_A;
		case 'B': return Rml::Input::KI_B;
		case 'C': return Rml::Input::KI_C;
		case 'D': return Rml::Input::KI_D;
		case 'E': return Rml::Input::KI_E;
		case 'F': return Rml::Input::KI_F;
		case 'G': return Rml::Input::KI_G;
		case 'H': return Rml::Input::KI_H;
		case 'I': return Rml::Input::KI_I;
		case 'J': return Rml::Input::KI_J;
		case 'K': return Rml::Input::KI_K;
		case 'L': return Rml::Input::KI_L;
		case 'M': return Rml::Input::KI_M;
		case 'N': return Rml::Input::KI_N;
		case 'O': return Rml::Input::KI_O;
		case 'P': return Rml::Input::KI_P;
		case 'Q': return Rml::Input::KI_Q;
		case 'R': return Rml::Input::KI_R;
		case 'S': return Rml::Input::KI_S;
		case 'T': return Rml::Input::KI_T;
		case 'U': return Rml::Input::KI_U;
		case 'V': return Rml::Input::KI_V;
		case 'W': return Rml::Input::KI_W;
		case 'X': return Rml::Input::KI_X;
		case 'Y': return Rml::Input::KI_Y;
		case 'Z': return Rml::Input::KI_Z;
		case VK_SPACE: return Rml::Input::KI_SPACE;
		case VK_RETURN: return Rml::Input::KI_RETURN;
		case VK_ESCAPE: return Rml::Input::KI_ESCAPE;
		case VK_TAB: return Rml::Input::KI_TAB;
		case VK_BACK: return Rml::Input::KI_BACK;
		case VK_DELETE: return Rml::Input::KI_DELETE;
		case VK_INSERT: return Rml::Input::KI_INSERT;
		case VK_HOME: return Rml::Input::KI_HOME;
		case VK_END: return Rml::Input::KI_END;
		case VK_PRIOR: return Rml::Input::KI_PRIOR;
		case VK_NEXT: return Rml::Input::KI_NEXT;
		case VK_LEFT: return Rml::Input::KI_LEFT;
		case VK_RIGHT: return Rml::Input::KI_RIGHT;
		case VK_UP: return Rml::Input::KI_UP;
		case VK_DOWN: return Rml::Input::KI_DOWN;
		case VK_SHIFT: return Rml::Input::KI_LSHIFT;
		case VK_CONTROL: return Rml::Input::KI_LCONTROL;
		case VK_MENU: return Rml::Input::KI_LMENU;
		case VK_F1: return Rml::Input::KI_F1;
		case VK_F2: return Rml::Input::KI_F2;
		case VK_F3: return Rml::Input::KI_F3;
		case VK_F4: return Rml::Input::KI_F4;
		case VK_F5: return Rml::Input::KI_F5;
		case VK_F6: return Rml::Input::KI_F6;
		case VK_F7: return Rml::Input::KI_F7;
		case VK_F8: return Rml::Input::KI_F8;
		case VK_F9: return Rml::Input::KI_F9;
		case VK_F10: return Rml::Input::KI_F10;
		case VK_F11: return Rml::Input::KI_F11;
		case VK_F12: return Rml::Input::KI_F12;
		default: return Rml::Input::KI_UNKNOWN;
		}
	}

	Rml::Input::KeyModifier RmlUiOverlay::getKeyModifiers()
	{
		int modifiers = 0;
		// Check current key state via Win32 API
		if (GetKeyState(VK_SHIFT) & 0x8000)
			modifiers |= Rml::Input::KM_SHIFT;
		if (GetKeyState(VK_CONTROL) & 0x8000)
			modifiers |= Rml::Input::KM_CTRL;
		if (GetKeyState(VK_MENU) & 0x8000)
			modifiers |= Rml::Input::KM_ALT;
		if (GetKeyState(VK_CAPITAL) & 1)
			modifiers |= Rml::Input::KM_CAPSLOCK;
		if (GetKeyState(VK_NUMLOCK) & 1)
			modifiers |= Rml::Input::KM_NUMLOCK;
		return static_cast<Rml::Input::KeyModifier>(modifiers);
	}
}
