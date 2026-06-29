#ifndef RMLUI_BACKENDS_INCLUDE_VULKAN_H
#define RMLUI_BACKENDS_INCLUDE_VULKAN_H

#if defined RMLUI_PLATFORM_UNIX && !defined(__ANDROID__)
	#define VK_USE_PLATFORM_XCB_KHR 1
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

// Use the standard Vulkan SDK header instead of the bundled glad-generated one.
// The project requires full Vulkan extensions (ray tracing, etc.) not in the bundled header.
#include <vulkan/vulkan.h>
#include "RmlUi_Vulkan/vk_mem_alloc.h"

#endif
