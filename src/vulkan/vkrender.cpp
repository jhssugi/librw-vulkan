#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwerror.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwrender.h"
#ifdef RW_VULKAN
#	include "rwvk.h"
#	include "rwvkshader.h"
#	include "rwvkimpl.h"
#   include "Pipeline.h"
#	include "Textures.h"
#	include "GraphicsContext.h"
#	include "SwapChain.h"

namespace rw
{
	namespace vulkan
	{
#	define MAX_LIGHTS

		static maple::Pipeline::Ptr currentPipeline = nullptr;

		maple::Pipeline::Ptr getPipeline()
		{
			maple::PipelineInfo info;

			int32_t srcBlend = rw::GetRenderState(rw::SRCBLEND);
			int32_t dstBlend = rw::GetRenderState(rw::DESTBLEND);
			int32_t zTest = rw::GetRenderState(rw::ZTESTENABLE);
			int32_t zWritable = rw::GetRenderState(rw::ZWRITEENABLE);
			int32_t cullMode = rw::GetRenderState(rw::CULLMODE);

			info.shader = getShader(currentShader->shaderId);
			info.depthTarget = vkGlobals.currentDepth;
			info.colorTargets[0] = vkGlobals.colorTarget;
			info.depthFunc = zTest ? maple::StencilType::LessOrEqual : maple::StencilType::Always;
			info.swapChainTarget = info.colorTargets[0] == nullptr;

			if (cullMode == rw::CULLNONE) 
			{
				info.cullMode = maple::CullMode::None;
			}
			else if (cullMode == rw::CULLBACK) 
			{
				info.cullMode = maple::CullMode::Back;
			}
			else
			{
				MAPLE_ASSERT(false, "TODO.");
			}

			if (srcBlend == rw::BLENDSRCALPHA && dstBlend == BLENDINVSRCALPHA)
			{
				info.blendMode = maple::BlendMode::SrcAlphaOneMinusSrcAlpha;
			}
			else
			{
				MAPLE_ASSERT(false, "TODO.");
			}
			return maple::Pipeline::get(info);
		}

		void drawInst_simple(InstanceDataHeader *header, InstanceData *inst)
		{
			flushCache();
			auto pipeline = getPipeline();
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			
			if (pipeline != currentPipeline)
			{
				if (currentPipeline != nullptr) 
				{
					currentPipeline->end(cmdBuffer);
					currentPipeline = pipeline;
				}
				pipeline->bind(cmdBuffer);
			}
		}

		// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
		void drawInst_GSemu(InstanceDataHeader *header, InstanceData *inst)
		{
			uint32 hasAlpha;
			int    alphafunc, alpharef, gsalpharef;
			int    zwrite;
			hasAlpha = getAlphaBlend();
			if (hasAlpha)
			{
				zwrite    = rw::GetRenderState(rw::ZWRITEENABLE);
				alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
				if (zwrite)
				{
					alpharef   = rw::GetRenderState(rw::ALPHATESTREF);
					gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
					SetRenderState(rw::ALPHATESTREF, gsalpharef);
					drawInst_simple(header, inst);
					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
					SetRenderState(rw::ZWRITEENABLE, 0);
					drawInst_simple(header, inst);
					SetRenderState(rw::ZWRITEENABLE, 1);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
					SetRenderState(rw::ALPHATESTREF, alpharef);
				}
				else
				{
					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
					drawInst_simple(header, inst);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
				}
			}
			else
				drawInst_simple(header, inst);
		}

		void drawInst(InstanceDataHeader *header, InstanceData *inst)
		{
			if (rw::GetRenderState(rw::GSALPHATEST))
				drawInst_GSemu(header, inst);
			else
				drawInst_simple(header, inst);
		}

		
		int32 lightingCB(Atomic *atomic)
		{
			WorldLights lightData;
			Light *     directionals[8];
			Light *     locals[8];
			lightData.directionals    = directionals;
			lightData.numDirectionals = 8;
			lightData.locals          = locals;
			lightData.numLocals       = 8;

			if (atomic->geometry->flags & rw::Geometry::LIGHT)
				((World *) engine->currentWorld)->enumerateLights(atomic, &lightData);
			else
				memset(&lightData, 0, sizeof(lightData));
			return setLights(&lightData);
		}

		int32 lightingCB(void)
		{
			WorldLights lightData;
			Light *     directionals[8];
			Light *     locals[8];
			lightData.directionals    = directionals;
			lightData.numDirectionals = 8;
			lightData.locals          = locals;
			lightData.numLocals       = 8;

			((World *) engine->currentWorld)->enumerateLights(&lightData);
			return setLights(&lightData);
		}

		void defaultRenderCB(Atomic *atomic, InstanceDataHeader *header)
		{
			Material *m;

			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			InstanceData *inst = header->inst;
			int32         n    = header->numMeshes;

			while (n--) 
			{
				m = inst->material;
				auto set = getMaterialDescriptorSet(m);
				setMaterial(set, m->color, m->surfaceProps);
				setTexture(set, 0, m->texture);

				rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);

				if ((vsBits & VSLIGHT_MASK) == 0) {
					if (getAlphaTest())
						defaultShader->use();
					else
						defaultShader_noAT->use();
				}
				else {
					if (getAlphaTest())
						defaultShader_fullLight->use();
					else
						defaultShader_fullLight_noAT->use();
				}

				drawInst(header, inst);
				inst++;
			}

			if (currentPipeline != nullptr) 
			{
				currentPipeline->end(maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer());
				currentPipeline = nullptr;
			}
		}
	}        // namespace vulkan
}        // namespace rw

#endif
