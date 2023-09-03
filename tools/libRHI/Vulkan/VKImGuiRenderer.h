#pragma once

#include "ImGuiRenderer.h"

#include "FrameBuffer.h"
#include "RenderPass.h"
#include "VkCommon.h"

struct ImGui_ImplVulkanH_Window;

namespace maple
{
	class Texture2D;

	class VKImGuiRenderer : public ImGuiRenderer
	{
	  public:
		VKImGuiRenderer(uint32_t width, uint32_t height, bool clearScreen);
		~VKImGuiRenderer();

		auto init() -> void override;
		auto newFrame() -> void override;
		auto render() -> void override;
		auto onResize(uint32_t width, uint32_t height) -> void override;
		auto rebuildFontTexture() -> void override;

	  private:
		auto setupVulkanWindowData(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int32_t width, int32_t height) -> void;

		void *                     windowHandle = nullptr;
		bool                       clearScreen  = false;
		std::shared_ptr<Texture2D> fontTexture;
		uint32_t                   width;
		uint32_t                   height;

		std::vector<std::shared_ptr<FrameBuffer>> frameBuffers;
		std::shared_ptr<RenderPass>               renderPass;
	};
}        // namespace maple
