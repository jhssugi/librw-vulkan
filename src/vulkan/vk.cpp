#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "rwvk.h"
#ifdef RW_VULKAN

#include "rwvkimpl.h"

namespace rw
{
	namespace vulkan
	{
		// TODO: make some of these things platform-independent

		static void* driverOpen(void* o, int32, int32)
		{
			engine->driver[PLATFORM_VULKAN]->defaultPipeline = makeDefaultPipeline();
			engine->driver[PLATFORM_VULKAN]->rasterNativeOffset = nativeRasterOffset;
			engine->driver[PLATFORM_VULKAN]->rasterCreate = rasterCreate;
			engine->driver[PLATFORM_VULKAN]->rasterLock = rasterLock;
			engine->driver[PLATFORM_VULKAN]->rasterUnlock = rasterUnlock;
			engine->driver[PLATFORM_VULKAN]->rasterNumLevels = rasterNumLevels;
			engine->driver[PLATFORM_VULKAN]->imageFindRasterFormat = imageFindRasterFormat;
			engine->driver[PLATFORM_VULKAN]->rasterFromImage = rasterFromImage;
			engine->driver[PLATFORM_VULKAN]->rasterToImage = rasterToImage;
			return o;
		}

		static void* driverClose(void* o, int32, int32)
		{
			return o;
		}

		void registerPlatformPlugins(void)
		{
			Driver::registerPlugin(PLATFORM_VULKAN, 0, PLATFORM_VULKAN, driverOpen, driverClose);
			registerNativeRaster();
		}
	}        // namespace vulkan
}        // namespace rw
#else
namespace rw
{
	namespace vulkan
	{
		void registerPlatformPlugins(void) {}

		Texture* readNativeTexture(Stream* stream) { return nullptr; }

		uint32   getSizeNativeTexture(Texture* tex) { return 0; }

		void     writeNativeTexture(Texture* tex, Stream* stream) {}
	}
};
#endif // RW_VULKAN

