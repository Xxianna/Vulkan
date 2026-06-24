/*
* Vulkan Example - Input event logger
*
* Demonstrates and tests all unified input handler interactions:
* mouse, keyboard, touch, text input, IME, clipboard, DPI, cursor.
* Events are printed to an ImGui multi-line text box with timestamps.
*
* Copyright (C) 2026 - MIT License
*/

#include "vulkanexamplebase.h"
#include <chrono>
#include <deque>
#include <sstream>
#include <iomanip>

static std::string getTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	auto timer = std::chrono::system_clock::to_time_t(now);
	std::ostringstream oss;
	oss << std::put_time(std::localtime(&timer), "%H:%M:%S")
		<< '.' << std::setfill('0') << std::setw(3) << ms.count();
	return oss.str();
}

class VulkanExample : public VulkanExampleBase
{
public:
	static constexpr int MAX_LOG_LINES = 200;
	std::deque<std::string> logLines;
	bool autoScroll = true;
	bool logMouse = true;
	bool logKeyboard = true;
	bool logTouch = true;
	bool logText = true;
	bool logSystem = true;

	void addLog(const std::string& category, const std::string& message)
	{
		std::string line = "[" + getTimestamp() + "] [" + category + "] " + message;
		logLines.push_front(line);
		if ((int)logLines.size() > MAX_LOG_LINES) {
			logLines.pop_back();
		}
	}

	VulkanExample() : VulkanExampleBase()
	{
		title = "Input Event Logger";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, 2.0f));
		camera.setRotation(glm::vec3(0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2]{};
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = frameBuffers[currentImageIndex];

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		// No scene geometry - just clear color + UI overlay
		drawUI(cmdBuffer);

		vkCmdEndRenderPass(cmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();

		// Set up input handler callbacks
		inputHandler.onMouseMove = [this](float x, float y) {
			if (logMouse) {
				std::ostringstream oss;
				oss << "MouseMove x=" << std::fixed << std::setprecision(1) << x << " y=" << y;
				addLog("Mouse", oss.str());
			}
		};
		inputHandler.onMouseButtonDown = [this](int button) {
			if (logMouse) {
				const char* names[] = { "Left", "Right", "Middle" };
				addLog("Mouse", std::string("ButtonDown ") + (button < 3 ? names[button] : "?"));
			}
		};
		inputHandler.onMouseButtonUp = [this](int button) {
			if (logMouse) {
				const char* names[] = { "Left", "Right", "Middle" };
				addLog("Mouse", std::string("ButtonUp ") + (button < 3 ? names[button] : "?"));
			}
		};
		inputHandler.onMouseWheel = [this](float dx, float dy) {
			if (logMouse) {
				std::ostringstream oss;
				oss << "Wheel dx=" << std::fixed << std::setprecision(1) << dx << " dy=" << dy;
				addLog("Mouse", oss.str());
			}
		};
		inputHandler.onMouseLeave = [this]() {
			if (logMouse) addLog("Mouse", "MouseLeave");
		};
		inputHandler.onKeyDown = [this](int key, int mod) {
			if (logKeyboard) {
				std::ostringstream oss;
				oss << "KeyDown key=0x" << std::hex << key;
				if (mod & vks::KM_SHIFT) oss << " +Shift";
				if (mod & vks::KM_CTRL) oss << " +Ctrl";
				if (mod & vks::KM_ALT) oss << " +Alt";
				if (mod & vks::KM_SUPER) oss << " +Super";
				addLog("Keyboard", oss.str());
			}
		};
		inputHandler.onKeyUp = [this](int key, int mod) {
			if (logKeyboard) {
				std::ostringstream oss;
				oss << "KeyUp key=0x" << std::hex << key;
				addLog("Keyboard", oss.str());
			}
		};
		inputHandler.onTextInput = [this](const std::string& text) {
			if (logText) addLog("Text", "TextInput: \"" + text + "\"");
		};
		inputHandler.onTouchEvent = [this](int action, const std::vector<vks::TouchPoint>& touches) {
			if (logTouch) {
				const char* actions[] = { "Start", "Move", "End" };
				std::ostringstream oss;
				oss << "Touch" << (action < 3 ? actions[action] : "?") << " count=" << touches.size();
				for (const auto& t : touches) {
					oss << " [id=" << t.id << " x=" << std::fixed << std::setprecision(1) << t.x << " y=" << t.y << "]";
				}
				addLog("Touch", oss.str());
			}
		};
		inputHandler.onDpiChanged = [this](float scale) {
			if (logSystem) {
				std::ostringstream oss;
				oss << "DpiChanged scale=" << std::fixed << std::setprecision(2) << scale;
				addLog("System", oss.str());
			}
		};
		inputHandler.onImeComposition = [this](const std::string& text) {
			if (logText) addLog("IME", "Composition: \"" + text + "\"");
		};

		addLog("System", "Input Event Logger started");
		addLog("System", "Interact with the window to see events here");
#if defined(_WIN32)
		addLog("System", "Platform: Win32");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		addLog("System", "Platform: Android");
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
		addLog("System", "Platform: macOS");
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
		addLog("System", "Platform: Wayland");
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		addLog("System", "Platform: XCB (Linux)");
#else
		addLog("System", "Platform: Unknown");
#endif
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		VulkanExampleBase::prepareFrame();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Input Event Log")) {
			if (overlay->checkBox("Mouse", &logMouse)) {}
			ImGui::SameLine();
			if (overlay->checkBox("Keyboard", &logKeyboard)) {}
			ImGui::SameLine();
			if (overlay->checkBox("Touch", &logTouch)) {}
			ImGui::SameLine();
			if (overlay->checkBox("Text/IME", &logText)) {}
			ImGui::SameLine();
			if (overlay->checkBox("System", &logSystem)) {}

			if (overlay->button("Clear")) {
				logLines.clear();
			}
			ImGui::SameLine();
			if (overlay->button("Test Clipboard")) {
				inputHandler.SetClipboardText("Hello from Vulkan InputEvents!");
				addLog("System", "Clipboard set to: \"Hello from Vulkan InputEvents!\"");
				std::string clip = inputHandler.GetClipboardText();
				addLog("System", "Clipboard get: \"" + clip + "\"");
			}
			ImGui::SameLine();
			if (overlay->button("Test Cursor")) {
				static int cursorIdx = 0;
				cursorIdx = (cursorIdx + 1) % 7;
				inputHandler.SetMouseCursor(cursorIdx);
				const char* cursorNames[] = { "Default", "Move", "Pointer", "Resize", "Cross", "Text", "Unavailable" };
				addLog("System", std::string("Cursor set to: ") + cursorNames[cursorIdx]);
			}

			std::string logTextStr;
			for (const auto& line : logLines) {
				logTextStr += line + "\n";
			}

			ImGui::BeginChild("LogScroll", ImVec2(0, 400 * overlay->scale), true);
			ImGui::TextUnformatted(logTextStr.c_str());
			if (autoScroll && !logLines.empty()) {
				ImGui::SetScrollHere(0.0f);
			}
			ImGui::EndChild();
		}
	}
};

VULKAN_EXAMPLE_MAIN()
