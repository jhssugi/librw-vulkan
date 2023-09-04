
namespace maple
{
	class TextureDepth;
	class Texture2D;
	class DescriptorSet;
}

namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		extern uint32 im2DVbo, im2DIbo;
		void          openIm2D(uint32_t width, uint32_t height);
		void          closeIm2D(void);
		void          im2DRenderLine(void* vertices, int32 numVertices,
			int32 vert1, int32 vert2);
		void          im2DRenderTriangle(void* vertices, int32 numVertices,
			int32 vert1, int32 vert2, int32 vert3);
		void          im2DRenderPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices);
		void          im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices, void* indices, int32 numIndices);

		void openIm3D(void);
		void closeIm3D(void);
		void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags);
		void im3DRenderPrimitive(PrimitiveType primType);
		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices);
		void im3DEnd(void);

		struct DisplayMode
		{
			GLFWvidmode mode;
			int32       depth;
			uint32      flags;
		};

		struct VkGlobals
		{
			GLFWwindow** pWindow;
			GLFWwindow* window;

			GLFWmonitor* monitor;
			int          numMonitors;
			int          currentMonitor;

			DisplayMode* modes;
			int          numModes;
			int          currentMode;
			int          presentWidth, presentHeight;
			int          presentOffX, presentOffY;

			// for opening the window
			int         winWidth, winHeight;
			const char* winTitle;
			uint32      numSamples;

			std::shared_ptr<maple::Texture> currentDepth;
			std::shared_ptr<maple::Texture> colorTarget;
		};

		extern VkGlobals vkGlobals;
		extern std::shared_ptr <maple::DescriptorSet> commonSet;
#endif

		Raster* rasterCreate(Raster* raster);
		uint8* rasterLock(Raster*, int32 level, int32 lockMode);
		void    rasterUnlock(Raster*, int32);
		int32   rasterNumLevels(Raster*);
		bool32  imageFindRasterFormat(Image* img, int32 type,
			int32* width, int32* height, int32* depth, int32* format);
		bool32  rasterFromImage(Raster* raster, Image* image);
		Image* rasterToImage(Raster* raster);

	}        // namespace vulkan
}        // namespace rw
