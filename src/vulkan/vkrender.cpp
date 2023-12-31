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
#	include "VertexBuffer.h"
#	include "IndexBuffer.h"
#	include "RenderDevice.h"
#	include "CommandBuffer.h"

namespace rw
{
	namespace vulkan
	{
#	define MAX_LIGHTS

		static maple::Pipeline::Ptr currentPipeline = nullptr;

		static std::unordered_map<InstanceData *, maple::DescriptorSet::Ptr> objectSets;

		
		static maple::CullMode cullModes[] = {maple::CullMode::Back, maple::CullMode::Back, maple::CullMode::Back, maple::CullMode::Front};

		maple::Pipeline::Ptr getPipeline(maple::DrawType drawType)
		{
			maple::PipelineInfo info;
			int32_t srcBlend = rw::GetRenderState(rw::SRCBLEND);
			int32_t dstBlend = rw::GetRenderState(rw::DESTBLEND);

			int32_t zTest = rw::GetRenderState(rw::ZTESTENABLE);
			int32_t zWritable = rw::GetRenderState(rw::ZWRITEENABLE);

			info.shader = getShader(currentShader->shaderId);
			info.drawType = drawType;
			info.depthTarget = vkGlobals.currentDepth;
			info.colorTargets[0] = vkGlobals.colorTarget;
			info.depthFunc = zWritable && !zTest ? maple::StencilType::Always : maple::StencilType::LessOrEqual;
			info.swapChainTarget = info.colorTargets[0] == nullptr;
			//info.depthWriteEnable = zWritable;
			info.depthTest = zTest;
			info.cullMode = cullModes[rw::GetRenderState(rw::CULLMODE)];
			
			info.transparencyEnabled = false;

			if (srcBlend == rw::BLENDSRCALPHA && dstBlend == BLENDINVSRCALPHA)
			{
				info.blendMode = maple::BlendMode::SrcAlphaOneMinusSrcAlpha;
			}
			else
			{
				info.blendMode = maple::BlendMode::SrcAlphaOneMinusSrcAlpha;
				//				MAPLE_ASSERT(false, "TODO.");
			}
			return maple::Pipeline::get(info);
		}

		void drawInst_simple(maple::DescriptorSet::Ptr objSet, InstanceDataHeader* header, InstanceData* inst)
		{
			auto set = getMaterialDescriptorSet(inst->material);
			auto pipeline = getPipeline(maple::DrawType::Triangle);
			flushCache(pipeline->getShader(), objSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			header->vertexBufferGPU->bind(cmdBuffer, pipeline.get());
			header->indexBufferGPU->bind(cmdBuffer);
/*

			if (pipeline != currentPipeline)
			{
				if (currentPipeline != nullptr)
				{
					currentPipeline->end(cmdBuffer);
				}
				currentPipeline = pipeline;
			}
			*/
			commonSet->update(cmdBuffer);
			set->update(cmdBuffer);
			objSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer, maple::ivec4{vkGlobals.presentOffX, vkGlobals.presentOffY, vkGlobals.presentWidth, vkGlobals.presentHeight});
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, {commonSet, set, objSet});
			pipeline->drawIndexed(cmdBuffer, inst->numIndex, 1, inst->offset, 0, 0);
			pipeline->end(cmdBuffer);
		}

		// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
		void drawInst_GSemu(maple::DescriptorSet::Ptr objSet, InstanceDataHeader* header, InstanceData* inst)
		{
			uint32 hasAlpha;
			int    alphafunc, alpharef, gsalpharef;
			int    zwrite;
			hasAlpha = getAlphaBlend();
			if (hasAlpha)
			{
				zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
				alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
				if (zwrite)
				{
					alpharef = rw::GetRenderState(rw::ALPHATESTREF);
					gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
					SetRenderState(rw::ALPHATESTREF, gsalpharef);
					drawInst_simple(objSet,header, inst);
					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
					SetRenderState(rw::ZWRITEENABLE, 0);
					drawInst_simple(objSet, header, inst);
					SetRenderState(rw::ZWRITEENABLE, 1);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
					SetRenderState(rw::ALPHATESTREF, alpharef);
				}
				else
				{
					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
					drawInst_simple(objSet, header, inst);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
				}
			}
			else
				drawInst_simple(objSet, header, inst);
		}

		void drawInst(InstanceDataHeader *header, InstanceData *inst)
		{
			if(auto iter = objectSets.find(inst); iter == objectSets.end()) {
				objectSets[inst] = maple::DescriptorSet::create({2, getShader(defaultShader->shaderId).get()});
			}

			if (rw::GetRenderState(rw::GSALPHATEST))
				drawInst_GSemu(objectSets[inst], header, inst);
			else
				drawInst_simple(objectSets[inst], header, inst);
		}


		int32 lightingCB(Atomic* atomic)
		{
			WorldLights lightData;
			Light* directionals[8];
			Light* locals[8];
			lightData.directionals = directionals;
			lightData.numDirectionals = 8;
			lightData.locals = locals;
			lightData.numLocals = 8;

			if (atomic->geometry->flags & rw::Geometry::LIGHT)
				((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
			else
				memset(&lightData, 0, sizeof(lightData));
			return setLights(&lightData);
		}

		int32 lightingCB(void)
		{
			WorldLights lightData;
			Light* directionals[8];
			Light* locals[8];
			lightData.directionals = directionals;
			lightData.numDirectionals = 8;
			lightData.locals = locals;
			lightData.numLocals = 8;

			((World*)engine->currentWorld)->enumerateLights(&lightData);
			return setLights(&lightData);
		}

		void defaultRenderCB(Atomic* atomic, InstanceDataHeader* header)
		{
			Material* m;

			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			InstanceData* inst = header->inst;
			int32         n = header->numMeshes;

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
