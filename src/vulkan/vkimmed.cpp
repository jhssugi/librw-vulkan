#include <assert.h>
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
#	include "rwvkimpl.h"
#	include "rwvkshader.h"

#include "GraphicsContext.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "Textures.h"
#include "RenderPass.h"

namespace rw
{
	namespace vulkan
	{
		//simple way to implement. still have performance issue.

		RGBA im3dMaterialColor = { 255, 255, 255, 255 };
		SurfaceProperties im3dSurfaceProps = { 1.0f, 1.0f, 1.0f };

#define STARTINDICES 10000
#define STARTVERTICES 10000

		static maple::DrawType primTypeMap[] = {
			maple::DrawType::Point,	// invalid
			maple::DrawType::Lines,
			maple::DrawType::Lines_Strip,
			maple::DrawType::Triangle,
			maple::DrawType::TriangleStrip,
			maple::DrawType::Triangle_Fan,
			maple::DrawType::Point
		};

		static Shader* im2dShader;
		static Shader* im3dShader;

		static maple::CullMode cullModes[] =
		{
			maple::CullMode::None,
			maple::CullMode::None,
			maple::CullMode::Back,
			maple::CullMode::Front
		};

		static maple::VertexBuffer::Ptr vertexBuffer;
		static maple::IndexBuffer::Ptr indexBuffer;
		static maple::DescriptorSet::Ptr objectSet;


		static maple::VertexBuffer::Ptr vertexBuffer3D;
		static maple::IndexBuffer::Ptr indexBuffer3D;
		static maple::DescriptorSet::Ptr im3dSet;


		maple::Pipeline::Ptr createPipeline(maple::DrawType drawType, int32_t shaderId)
		{
			maple::PipelineInfo info;
			info.shader = getShader(shaderId);
			info.drawType = drawType;
			info.depthTarget = vkGlobals.currentDepth;
			info.colorTargets[0] = vkGlobals.colorTarget;
			info.depthFunc = maple::StencilType::LessOrEqual;//maple::StencilType::Always;//zTest ? maple::StencilType::LessOrEqual : maple::StencilType::Always;
			info.swapChainTarget = info.colorTargets[0] == nullptr;
			info.cullMode = cullModes[rw::GetRenderState(rw::CULLMODE)];
			info.blendMode = maple::BlendMode::SrcAlphaOneMinusSrcAlpha;
			info.transparencyEnabled = true;
			info.depthTest = false;
			return maple::Pipeline::get(info);
		}


		void openIm2D(uint32_t width, uint32_t height)
		{
#include "vkshaders/im2d.shader.h"
			const std::string defaultTxt{ (char*)__im2d_shader, __im2d_shader_len };
			im2dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			vertexBuffer = maple::VertexBuffer::create(nullptr, sizeof(Im2DVertex) * STARTVERTICES);
			indexBuffer = maple::IndexBuffer::create((uint16_t*)nullptr, STARTINDICES);
			objectSet = maple::DescriptorSet::create({ 0,getShader(im2dShader->shaderId).get() });
		}

		void closeIm2D(void)
		{
			im2dShader->destroy();
			im2dShader = nil;
		}

		static Im2DVertex tmpprimbuf[3];

		void im2DRenderLine(void* vertices, int32 numVertices, int32 vert1, int32 vert2)
		{
			Im2DVertex* verts = (Im2DVertex*)vertices;
			tmpprimbuf[0] = verts[vert1];
			tmpprimbuf[1] = verts[vert2];
			im2DRenderPrimitive(PRIMTYPELINELIST, tmpprimbuf, 2);
		}

		void im2DRenderTriangle(void* vertices, int32 numVertices, int32 vert1, int32 vert2, int32 vert3)
		{
			Im2DVertex* verts = (Im2DVertex*)vertices;
			tmpprimbuf[0] = verts[vert1];
			tmpprimbuf[1] = verts[vert2];
			tmpprimbuf[2] = verts[vert3];
			im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
		}

		void im2DSetXform(maple::DescriptorSet::Ptr set2d)
		{
			float32 xform[4];

			Camera* cam;
			cam = (Camera*)engine->currentCamera;
			xform[0] = 2.0f / cam->frameBuffer->width;
			xform[1] = -2.0f / cam->frameBuffer->height;
			xform[2] = -1.0f;
			xform[3] = 1.0f;

			if (auto pushConststs = getShader(im2dShader->shaderId)->getPushConstant(0))
			{
				pushConststs->setData(xform);
			}
			rw::Raster* rast = (rw::Raster*)rw::GetRenderStatePtr(rw::TEXTURERASTER);
			if (rast != nullptr)
			{
				auto vkRst = GET_VULKAN_RASTEREXT(rast);
				set2d->setTexture("tex0", getTexture(vkRst->textureId));
			}
			else
			{
				set2d->setTexture("tex0", maple::Texture2D::getTexture1X1White());
			}
		}

		void im2DRenderPrimitive(PrimitiveType primType, void* vertices, int32 numVertices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], im2dShader->shaderId);

			vertexBuffer->setData(numVertices * sizeof(Im2DVertex), vertices);

			im2DSetXform(objectSet);
			flushFog(objectSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			vertexBuffer->bind(cmdBuffer, nullptr);
			objectSet->update(cmdBuffer);
			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { objectSet });
			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], numVertices);
			pipeline->end(cmdBuffer);
			cmdBuffer->submit();
		}

		void im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices,
			void* indices, int32 numIndices)
		{

			auto pipeline = createPipeline(primTypeMap[primType], im2dShader->shaderId);

			vertexBuffer->setData(numVertices * sizeof(Im2DVertex), vertices);
			indexBuffer->setData((uint16_t*)indices, numIndices);

			im2DSetXform(objectSet);
			flushFog(objectSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			vertexBuffer->bind(cmdBuffer, nullptr);
			indexBuffer->bind(cmdBuffer);
			objectSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { objectSet });
			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices);
			pipeline->end(cmdBuffer);
			cmdBuffer->submit();
		}

		void openIm3D(void)
		{
#include "vkshaders/im3d.shader.h"
			const std::string defaultTxt{ (char*)__im3d_shader, __im3d_shader_len };
			im3dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");

			vertexBuffer3D = maple::VertexBuffer::create(nullptr, sizeof(Im3DVertex) * STARTVERTICES);
			indexBuffer3D = maple::IndexBuffer::create((uint16_t*)nullptr, STARTINDICES);
			im3dSet = maple::DescriptorSet::create({ 0,getShader(im3dShader->shaderId).get() });
		}

		void closeIm3D(void)
		{

		}

		static int32_t g_numVertices;

		//begin
		void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags)
		{
			if (world == nil) {
				static Matrix ident;
				ident.setIdentity();
				world = &ident;
			}

			setWorldMatrix(world);

			if (flags & im3d::LIGHTING)
			{
				/*setMaterial(materialSet, im3dMaterialColor, im3dSurfaceProps);*/
				MAPLE_ASSERT(false, "TODO..");
				int32 vsBits = lightingCB();
				defaultShader_fullLight->use();
			}
			else
			{
				im3dShader->use();
			}

			vertexBuffer3D->setData(numVertices * sizeof(Im3DVertex), vertices);
			rw::Raster* rast = (rw::Raster*)rw::GetRenderStatePtr(rw::TEXTURERASTER);

			if (rast != nullptr)
			{
				auto vkRst = GET_VULKAN_RASTEREXT(rast);
				im3dSet->setTexture("tex0", getTexture(vkRst->textureId));
			}
			else
			{
				im3dSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
			}

			if ((flags & im3d::VERTEXUV) == 0)
			{
				im3dSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
			}
			g_numVertices = numVertices;
		}

		void im3DRenderPrimitive(PrimitiveType primType)
		{
			auto pipeline = createPipeline(primTypeMap[primType], currentShader->shaderId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			vertexBuffer3D->bind(cmdBuffer, nullptr);

			flushCache(getShader(currentShader->shaderId), nullptr);
			flushFog(im3dSet);

			im3dSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
				MAPLE_ASSERT(false, "Not support now");
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { im3dSet });
			}

			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], g_numVertices);

			pipeline->end(cmdBuffer);
		}

		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], currentShader->shaderId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			indexBuffer3D->setData((uint16_t*)indices, numIndices);

			indexBuffer3D->bind(cmdBuffer);
			vertexBuffer3D->bind(cmdBuffer, nullptr);

			flushCache(getShader(currentShader->shaderId), nullptr);
			flushFog(im3dSet);
			im3dSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { im3dSet });
			}

			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices);
			pipeline->end(cmdBuffer);
		}

		void im3DEnd(void)
		{
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			cmdBuffer->submit();
		}

		void imFlush()
		{
		}
	}        // namespace vulkan
}        // namespace rw

#endif
