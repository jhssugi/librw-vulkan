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

#include "CommandBuffer.h"
#include "GraphicsContext.h"
#include "ImageConvert.h"
#include "SwapChain.h"
#include "Textures.h"

#define PLUGIN_ID ID_DRIVER

namespace rw
{
	namespace vulkan
	{
		std::vector<std::shared_ptr<maple::Texture>> textureCache;

		static maple::TextureFilter filterConvMap_NoMIP[] = {
		    maple::TextureFilter::None,   maple::TextureFilter::Nearest, maple::TextureFilter::Linear, maple::TextureFilter::Nearest,
		    maple::TextureFilter::Linear, maple::TextureFilter::Nearest, maple::TextureFilter::Linear};

		static maple::TextureFilter filterConvMap_MIP[] = {maple::TextureFilter::None,    maple::TextureFilter::Nearest, maple::TextureFilter::Linear,
		                                                   maple::TextureFilter::Nearest, maple::TextureFilter::Linear,  maple::TextureFilter::Nearest,
		                                                   maple::TextureFilter::Linear};

		static maple::TextureWrap addressConvMap[] = {maple::TextureWrap::None, maple::TextureWrap::Repeat, maple::TextureWrap::MirroredRepeat,
		                                              maple::TextureWrap::ClampToEdge, maple::TextureWrap::ClampToBorder};

		int32 nativeRasterOffset;

		static uint32 getLevelSize(Raster *raster, int32 level)
		{
			int32_t i;
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);

			int32_t w = raster->originalWidth;
			int32_t h = raster->originalHeight;
			int32_t s = raster->originalStride;
			int32_t minDim = 1;

			for(i = 0; i < level; i++) {
				if(w > minDim) {
					w /= 2;
					s /= 2;
				}
				if(h > minDim) h /= 2;
			}
			return s * h;
		}

		static Raster *rasterCreateTexture(Raster *raster)
		{
			if(raster->format & (Raster::PAL4 | Raster::PAL8)) {
				RWERROR((ERR_NOTEXTURE));
				return nullptr;
			}

			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);

			switch(raster->format & 0xF00) {
			case Raster::C8888:
				natras->internalFormat = maple::TextureFormat::RGBA8;
				natras->hasAlpha = 1;
				natras->bpp = 4;
				raster->depth = 32;
				break;
			case Raster::C888: // Vulkan did not support RGB8, so using RGBA8 instead.
				natras->internalFormat = maple::TextureFormat::RGBA8;
				natras->hasAlpha = 0;
				natras->bpp = 4;
				raster->depth = 32;
				break;
			case Raster::C1555:
				natras->internalFormat = maple::TextureFormat::R5G5B5A1;
				natras->hasAlpha = 1;
				natras->bpp = 4;
				raster->depth = 32;
				break;
			default: RWERROR((ERR_INVRASTER)); return nil;
			}

			raster->stride = raster->width * natras->bpp;
			natras->numLevels = 1;
			if(raster->format & Raster::MIPMAP) {
				int32_t w = raster->width;
				int32_t h = raster->height;
				natras->numLevels = 1;
				while(w != 1 || h != 1) {
					natras->numLevels++;
					if(w > 1) w /= 2;
					if(h > 1) h /= 2;
				}
			}

			natras->autogenMipmap = (raster->format & (Raster::MIPMAP | Raster::AUTOMIPMAP)) == (Raster::MIPMAP | Raster::AUTOMIPMAP);

			if(natras->autogenMipmap) natras->numLevels = 1;

			auto texture = maple::Texture2D::create();
			natras->textureId = textureCache.size();
			textureCache.emplace_back(texture);
			texture->buildTexture(natras->internalFormat, raster->width, raster->height, false, false, false, natras->numLevels > 1);
			natras->maxAnisotropy = 1;
			return raster;
		}

		std::shared_ptr<maple::Texture> getTexture(int32_t textureId)
		{
			if(textureId < 0) return nullptr;
			return textureCache[textureId];
		}

		inline void removeTexture(int32_t textureId)
		{
			if(textureId > -1) textureCache[textureId] = nullptr;
		}

		static Raster *rasterCreateCameraTexture(Raster *raster)
		{
			if(raster->format & (Raster::PAL4 | Raster::PAL8)) {
				RWERROR((ERR_NOTEXTURE));
				return nullptr;
			}

			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);

			natras->internalFormat = maple::TextureFormat::RGBA8;
			natras->hasAlpha = 1;
			natras->bpp = 4;
			raster->depth = 32;
			raster->stride = raster->width * natras->bpp;

			natras->autogenMipmap = (raster->format & (Raster::MIPMAP | Raster::AUTOMIPMAP)) == (Raster::MIPMAP | Raster::AUTOMIPMAP);
			auto texture = maple::Texture2D::create();
			natras->textureId = textureCache.size();
			textureCache.emplace_back(texture);
			texture->buildTexture(natras->internalFormat, raster->width, raster->height, false, false, false, natras->numLevels > 1);
			natras->maxAnisotropy = 1;
			return raster;
		}

		static Raster *rasterCreateCamera(Raster *raster)
		{
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);
			raster->format = Raster::C8888;
			natras->internalFormat = maple::TextureFormat::RGBA8;
			natras->hasAlpha = 0;
			natras->bpp = 4;
			natras->autogenMipmap = 0;
			return raster;
		}

		static Raster *rasterCreateZbuffer(Raster *raster)
		{
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);
			auto depth = maple::TextureDepth::create(raster->width, raster->height, true);
			natras->textureId = textureCache.size();
			textureCache.emplace_back(depth);
			natras->internalFormat = maple::TextureFormat::DEPTH_STENCIL;
			natras->autogenMipmap = 0;
			return raster;
		}

		void allocateDXT(Raster *raster, int32 dxt, int32 numLevels, bool32 hasAlpha) { MAPLE_ASSERT(false, "TODO..allocateDXT."); }

		Raster *rasterCreate(Raster *raster)
		{
			bool fail = false;

			if(raster->width == 0 || raster->height == 0) {
				raster->flags |= Raster::DONTALLOCATE;
				raster->stride = 0;
				fail = true;
			}
			if(raster->flags & Raster::DONTALLOCATE) fail = true;

			if(!fail) {
				switch(raster->type) {
				case Raster::NORMAL:
				case Raster::TEXTURE: raster = rasterCreateTexture(raster); break;
				case Raster::CAMERATEXTURE: raster = rasterCreateCameraTexture(raster); break;
				case Raster::ZBUFFER: raster = rasterCreateZbuffer(raster); break;
				case Raster::CAMERA: raster = rasterCreateCamera(raster); break;

				default: RWERROR((ERR_INVRASTER)); return nil;
				}
			}

			raster->originalWidth = raster->width;
			raster->originalHeight = raster->height;
			raster->originalStride = raster->stride;
			raster->originalPixels = raster->pixels;
			return raster;
		}

		uint8 *rasterLock(Raster *raster, int32 level, int32 lockMode)
		{
#ifdef RW_VULKAN
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);
			uint8 *px;
			uint32 allocSz;
			int i;

			assert(raster->privateFlags == 0);

			switch(raster->type) {
			case Raster::NORMAL:
			case Raster::TEXTURE:
			case Raster::CAMERATEXTURE:
				for(i = 0; i < level; i++) {
					if(raster->width > 1) {
						raster->width /= 2;
						raster->stride /= 2;
					}
					if(raster->height > 1) raster->height /= 2;
				}

				allocSz = getLevelSize(raster, level);
				px = (uint8 *)rwMalloc(allocSz, MEMDUR_EVENT | ID_DRIVER);
				assert(raster->pixels == nil);
				raster->pixels = px;

				if(lockMode & Raster::LOCKREAD || !(lockMode & Raster::LOCKNOFETCH)) {

					auto texture = getTexture(natras->textureId);
					texture->copyImage(nullptr, px);
				}

				raster->privateFlags = lockMode;
				break;

			case Raster::CAMERA:
				if(lockMode & Raster::PRIVATELOCK_WRITE) assert(0 && "can't lock framebuffer for writing");
				raster->width = vkGlobals.presentWidth;
				raster->height = vkGlobals.presentHeight;
				raster->stride = raster->width * natras->bpp;
				assert(natras->bpp == 3);
				allocSz = raster->height * raster->stride;
				px = (uint8 *)rwMalloc(allocSz, MEMDUR_EVENT | ID_DRIVER);
				assert(raster->pixels == nil);
				raster->pixels = px;
				{
					auto texture = maple::GraphicsContext::get()->getSwapChain()->getCurrentImage();
					texture->copyImage(nullptr,px);
				}
				raster->privateFlags = lockMode;
				break;

			default: assert(0 && "cannot lock this type of raster yet"); return nil;
			}

			return px;
#else
			return nil;
#endif
		}

		void rasterUnlock(Raster *raster, int32 level)
		{
#ifdef RW_VULKAN
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);

			assert(raster->pixels);

			switch(raster->type) {
			case Raster::NORMAL:
			case Raster::TEXTURE:
			case Raster::CAMERATEXTURE:
				if(raster->privateFlags & Raster::LOCKWRITE) {
					std::static_pointer_cast<maple::Texture2D>(getTexture(natras->textureId))
					    ->update(0, 0, raster->width, raster->height, raster->pixels, level == 0 && natras->autogenMipmap);
				}
				break;

			case Raster::CAMERA:
				// TODO: write?
				break;
			}

			rwFree(raster->pixels);
			raster->pixels = nil;
#endif
			raster->width = raster->originalWidth;
			raster->height = raster->originalHeight;
			raster->stride = raster->originalStride;
			raster->pixels = raster->originalPixels;
			raster->privateFlags = 0;
		}

		int32 rasterNumLevels(Raster *raster)
		{
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);
			uint32_t level = getTexture(natras->textureId)->getMipMapLevels();
			return natras->numLevels;
		}

		// Almost the same as d3d9 and ps2 function
		bool32 imageFindRasterFormat(Image *img, int32 type, int32 *pWidth, int32 *pHeight, int32 *pDepth, int32 *pFormat)
		{
			int32 width, height, depth, format;
			assert((type & 0xF) == Raster::TEXTURE);

			width = img->width;
			height = img->height;
			depth = img->depth;
			depth = 32;
			format = Raster::C8888;
			format |= type;
			*pWidth = width;
			*pHeight = height;
			*pDepth = depth;
			*pFormat = format;
			return 1;
		}

		bool32 rasterFromImage(Raster *raster, Image *image)
		{
			if((raster->type & 0xF) != Raster::TEXTURE) return 0;
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);
			auto vkTexture = getTexture(natras->textureId);
			image->convertTo32();
			natras->hasAlpha = image->hasAlpha();
			bool unlock = false;
			if(raster->pixels == nil) {
				raster->lock(0, Raster::LOCKWRITE | Raster::LOCKNOFETCH);
				unlock = true;
			}

			assert(raster->pixels);
			memcpy(raster->pixels, image->pixels, sizeof(uint32_t) * image->width * image->height);
			if(unlock) raster->unlock(0);

			return true;
		}

		Image *rasterToImage(Raster *raster)
		{
			MAPLE_ASSERT(false, "TODO");
			return 0;
		}

		static void *createNativeRaster(void *object, int32 offset, int32)
		{
			VulkanRaster *ras = PLUGINOFFSET(VulkanRaster, object, offset);
			memset(ras, 0, sizeof(VulkanRaster));
			ras->textureId = -1;
			return object;
		}

		void evictRaster(Raster *raster);

		static void *destroyNativeRaster(void *object, int32 offset, int32)
		{
			Raster *raster = (Raster *)object;
			VulkanRaster *natras = PLUGINOFFSET(VulkanRaster, object, offset);
#ifdef RW_VULKAN
			removeTexture(natras->textureId);
			evictRaster(raster);
			natras->textureId = -1;
#endif
			return object;
		}

		static void *copyNativeRaster(void *dst, void *, int32 offset, int32)
		{
			LOGI("copyNativeRaster TODO..");
			return nullptr;
		}

		Texture *readNativeTexture(Stream *stream)
		{
			uint32 platform;
			if(!findChunk(stream, ID_STRUCT, nil, nil)) {
				RWERROR((ERR_CHUNK, "STRUCT"));
				return nil;
			}
			platform = stream->readU32();
			if(platform != PLATFORM_VULKAN) {
				RWERROR((ERR_PLATFORM, platform));
				return nil;
			}
			Texture *tex = Texture::create(nil);
			if(tex == nil) return nil;

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

			Raster *raster;
			VulkanRaster *natras;
			if(flags & 2) {
				raster = Raster::create(width, height, depth, format | Raster::TEXTURE | Raster::DONTALLOCATE, PLATFORM_VULKAN);
				allocateDXT(raster, compression, numLevels, flags & 1);
			} else {
				raster = Raster::create(width, height, depth, format | Raster::TEXTURE, PLATFORM_VULKAN);
			}
			assert(raster);
			natras = GET_VULKAN_RASTEREXT(raster);
			tex->raster = raster;

			getTexture(natras->textureId)->setName(tex->name);

			uint32 size;
			uint8 *data;
			for(int32 i = 0; i < numLevels; i++) {
				size = stream->readU32();
				data = raster->lock(i, Raster::LOCKWRITE | Raster::LOCKNOFETCH);
				stream->read8(data, size);
				raster->unlock(i);
			}
			return tex;
		}

		void writeNativeTexture(Texture *tex, Stream *stream)
		{
			Raster *raster = tex->raster;
			VulkanRaster *natras = GET_VULKAN_RASTEREXT(raster);

			int32 chunksize = getSizeNativeTexture(tex);
			writeChunkHeader(stream, ID_STRUCT, chunksize - 12);
			stream->writeU32(PLATFORM_VULKAN);

			// Texture
			stream->writeU32(tex->filterAddressing);
			stream->write8(tex->name, 32);
			stream->write8(tex->mask, 32);

			// Raster
			int32 numLevels = natras->numLevels;
			stream->writeI32(raster->format);
			stream->writeI32(raster->width);
			stream->writeI32(raster->height);
			stream->writeI32(raster->depth);
			stream->writeI32(numLevels);

			// Native raster
			int32 flags = 0;
			int32 compression = 0;
			if(natras->hasAlpha) flags |= 1;

			MAPLE_ASSERT(!natras->isCompressed, "Vulkan did support compression now");

			stream->writeI32(0);
			stream->writeI32(flags);
			stream->writeI32(compression);
			// TODO: auto mipmaps?

			uint32 size;
			uint8 *data;
			for(int32 i = 0; i < numLevels; i++) {
				size = getLevelSize(raster, i);
				stream->writeU32(size);
				data = raster->lock(i, Raster::LOCKREAD);
				stream->write8(data, size);
				raster->unlock(i);
			}
		}

		uint32 getSizeNativeTexture(Texture *tex)
		{
			uint32 size = 12 + 72 + 32;
			int32 levels = tex->raster->getNumLevels();
			for(int32 i = 0; i < levels; i++) size += 4 + getLevelSize(tex->raster, i);
			return size;
		}

		void registerNativeRaster(void)
		{
			nativeRasterOffset =
			    Raster::registerPlugin(sizeof(VulkanRaster), ID_RASTERVULKAN, createNativeRaster, destroyNativeRaster, copyNativeRaster);
		}
	} // namespace vulkan
} // namespace rw
#endif