#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RW_VULKAN

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwerror.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "rwvk.h"
#include "rwvkimpl.h"
#include "rwvkshader.h"

#include "Textures.h"
#include <vulkan/vulkan.h>



#define PLUGIN_ID ID_DRIVER

namespace rw
{
	namespace vulkan
	{
		std::vector<std::shared_ptr<maple::Texture>> textureCache;

		static maple::TextureFilter filterConvMap_NoMIP[] = {
				maple::TextureFilter::None,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear
		};


		static maple::TextureFilter filterConvMap_MIP[] = {
				maple::TextureFilter::None,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear,
				maple::TextureFilter::Nearest,
				maple::TextureFilter::Linear
		};

		static maple::TextureWrap addressConvMap[] = {
			 maple::TextureWrap::None,
			 maple::TextureWrap::Repeat,
			 maple::TextureWrap::MirroredRepeat,
			 maple::TextureWrap::ClampToEdge,
			 maple::TextureWrap::ClampToBorder
		};

		int32 nativeRasterOffset;

		static uint32 getLevelSize(Raster* raster, int32 level)
		{
			return 0;
		}

		static Raster* rasterCreateTexture(Raster* raster)
		{
			if (raster->format & (Raster::PAL4 | Raster::PAL8))
			{
				RWERROR((ERR_NOTEXTURE));
				return nullptr;
			}

			VulkanRaster* natras = GET_VULKAN_RASTEREXT(raster);

			switch (raster->format & 0xF00) {
			case Raster::C8888:
				natras->internalFormat = maple::TextureFormat::RGBA8;
				natras->hasAlpha = 1;
				natras->bpp = 4;
				raster->depth = 32;
				break;
			case Raster::C888:
				natras->internalFormat = maple::TextureFormat::RGB8;
				natras->hasAlpha = 0;
				natras->bpp = 3;
				raster->depth = 24;
				break;
			case Raster::C1555:
				natras->internalFormat = maple::TextureFormat::R5G5B5A1;
				natras->hasAlpha = 1;
				natras->bpp = 2;
				raster->depth = 16;
				break;
			default:
				RWERROR((ERR_INVRASTER));
				return nil;
			}

			raster->stride = raster->width * natras->bpp;

			if (raster->format & Raster::MIPMAP) {
				int32_t w = raster->width;
				int32_t h = raster->height;
				natras->numLevels = 1;
				while (w != 1 || h != 1) {
					natras->numLevels++;
					if (w > 1) w /= 2;
					if (h > 1) h /= 2;
				}
			}

			natras->autogenMipmap = (raster->format & (Raster::MIPMAP | Raster::AUTOMIPMAP)) == (Raster::MIPMAP | Raster::AUTOMIPMAP);
			auto texture = maple::Texture2D::create();
			natras->textureId = textureCache.size();
			textureCache.emplace_back(texture);
			texture->buildTexture(natras->internalFormat, raster->width, raster->height, false, false, false, natras->numLevels > 1);
			natras->maxAnisotropy = 1;
			return raster;
		}

		std::shared_ptr<maple::Texture> getTexture(int32_t textureId) 
		{
			return textureCache[textureId];
		}

		static Raster* rasterCreateCameraTexture(Raster* raster)
		{
			MAPLE_ASSERT(false, "TODO..");

			auto ret = rasterCreateTexture(raster);
			if (ret != nullptr)
			{
				VulkanRaster* natras = GET_VULKAN_RASTEREXT(raster);

			}
			return ret;
		}

		static Raster* rasterCreateCamera(Raster* raster)
		{
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(raster);
			raster->format = Raster::C888;
			natras->internalFormat = maple::TextureFormat::RGB8;
			natras->hasAlpha = 0;
			natras->bpp = 3;
			natras->autogenMipmap = 0;
			return raster;
		}

		static Raster* rasterCreateZbuffer(Raster* raster)
		{
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(raster);
			auto depth = maple::TextureDepth::create(raster->width, raster->height, true);
			natras->textureId = textureCache.size();
			textureCache.emplace_back(depth);
			natras->internalFormat = maple::TextureFormat::DEPTH_STENCIL;
			natras->autogenMipmap = 0;
			return raster;
		}

		void allocateDXT(Raster* raster, int32 dxt, int32 numLevels, bool32 hasAlpha)
		{

		}

		Raster* rasterCreate(Raster* raster)
		{
			bool fail = false;

			if (raster->width == 0 || raster->height == 0) {
				raster->flags |= Raster::DONTALLOCATE;
				raster->stride = 0;
				fail = true;
			}
			if (raster->flags & Raster::DONTALLOCATE)
				fail = true;

			if (!fail)
			{

				switch (raster->type) {
				case Raster::NORMAL:
				case Raster::TEXTURE:
					raster = rasterCreateTexture(raster);
					break;
				case Raster::CAMERATEXTURE:
					raster = rasterCreateCameraTexture(raster);
					break;
				case Raster::ZBUFFER:
					raster = rasterCreateZbuffer(raster);
					break;
				case Raster::CAMERA:
					raster = rasterCreateCamera(raster);
					break;

				default:
					RWERROR((ERR_INVRASTER));
					return nil;
				}
			}

			if (fail)
			{
				raster->originalWidth = raster->width;
				raster->originalHeight = raster->height;
				raster->originalStride = raster->stride;
				raster->originalPixels = raster->pixels;
			}
			return raster;
		}

		uint8* rasterLock(Raster* raster, int32 level, int32 lockMode)
		{
			return 0;
		}

		void rasterUnlock(Raster* raster, int32 level)
		{
		}

		int32 rasterNumLevels(Raster* raster)
		{
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(raster);
			return getTexture(natras->textureId)->getMipMapLevels();
		}

		// Almost the same as d3d9 and ps2 function
		bool32 imageFindRasterFormat(Image* img, int32 type,
			int32* pWidth, int32* pHeight, int32* pDepth, int32* pFormat)
		{
			int32 width, height, depth, format;

			assert((type & 0xF) == Raster::TEXTURE);

			//	for(width = 1; width < img->width; width <<= 1);
			//	for(height = 1; height < img->height; height <<= 1);
			// Perhaps non-power-of-2 textures are acceptable?
			width = img->width;
			height = img->height;

			depth = img->depth;

			if (depth <= 8)
				depth = 32;

			switch (depth)
			{
			case 32:
				if (img->hasAlpha())
					format = Raster::C8888;
				else
				{
					format = Raster::C888;
					depth = 24;
				}
				break;
			case 24:
				format = Raster::C888;
				break;
			case 16:
				format = Raster::C1555;
				break;

			case 8:
			case 4:
			default:
				RWERROR((ERR_INVRASTER));
				return 0;
			}

			format |= type;

			*pWidth = width;
			*pHeight = height;
			*pDepth = depth;
			*pFormat = format;

			return 1;
		}

		bool32 rasterFromImage(Raster* raster, Image* image)
		{
			return false;
		}

		Image* rasterToImage(Raster* raster)
		{
			return 0;
		}

		static void* createNativeRaster(void* object, int32 offset, int32)
		{
			VulkanRaster* ras = PLUGINOFFSET(VulkanRaster, object, offset);
			memset(ras, 0, sizeof(VulkanRaster));
			return object;
		}

		void evictRaster(Raster* raster);

		static void* destroyNativeRaster(void* object, int32 offset, int32)
		{
			LOGI("destroyNativeRaster TODO..");
			return object;
		}

		static void* copyNativeRaster(void* dst, void*, int32 offset, int32)
		{
			LOGI("copyNativeRaster TODO..");
			return nullptr;
		}

		Texture* readNativeTexture(Stream* stream)
		{
			uint32 platform;
			if (!findChunk(stream, ID_STRUCT, nil, nil)) {
				RWERROR((ERR_CHUNK, "STRUCT"));
				return nil;
			}
			platform = stream->readU32();
			if (platform != PLATFORM_GL3) {
				RWERROR((ERR_PLATFORM, platform));
				return nil;
			}
			Texture* tex = Texture::create(nil);
			if (tex == nil)
				return nil;

			// Texture
			tex->filterAddressing = stream->readU32();
			stream->read8(tex->name, 32);
			stream->read8(tex->mask, 32);

			// Raster
			uint32 format = stream->readU32();
			int32 width = stream->readI32();
			int32 height = stream->readI32();
			int32 depth = stream->readI32();
			int32 numLevels = stream->readI32();

			// Native raster
			int32 subplatform = stream->readI32();
			int32 flags = stream->readI32();
			int32 compression = stream->readI32();

		/*	if (subplatform != gl3Caps.gles) {
				tex->destroy();
				RWERROR((ERR_PLATFORM, platform));
				return nil;
			}*/

			Raster* raster;
			VulkanRaster* natras;
			if (flags & 2) {
				raster = Raster::create(width, height, depth, format | Raster::TEXTURE | Raster::DONTALLOCATE, PLATFORM_VULKAN);
				allocateDXT(raster, compression, numLevels, flags & 1);
			}
			else {
				raster = Raster::create(width, height, depth, format | Raster::TEXTURE, PLATFORM_VULKAN);
			}
			assert(raster);
			natras = GET_VULKAN_RASTEREXT(raster);
			tex->raster = raster;

			uint32 size;
			uint8* data;
			for (int32 i = 0; i < numLevels; i++) {
				size = stream->readU32();
				data = raster->lock(i, Raster::LOCKWRITE | Raster::LOCKNOFETCH);
				stream->read8(data, size);
				raster->unlock(i);
			}
			return tex;
		}

		void writeNativeTexture(Texture* tex, Stream* stream)
		{

		}

		uint32 getSizeNativeTexture(Texture* tex)
		{
			return 0;
		}

		void registerNativeRaster(void)
		{
			nativeRasterOffset = Raster::registerPlugin(sizeof(VulkanRaster),
				ID_RASTERVULKAN,
				createNativeRaster,
				destroyNativeRaster,
				copyNativeRaster);
		}
	}        // namespace vulkan
}        // namespace rw
#endif