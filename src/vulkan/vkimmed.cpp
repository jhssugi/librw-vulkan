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
namespace rw
{
	namespace vulkan
	{


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
		//static float32 xform[4];
		static maple::IndexBuffer::Ptr indexBuffer;
		static maple::VertexBuffer::Ptr vertexBuffer;

		static std::shared_ptr <maple::DescriptorSet> imguiSet;

		maple::Pipeline::Ptr createPipeline(maple::DrawType drawType, int32_t shaderId)
		{
			maple::PipelineInfo info;
			info.shader = getShader(shaderId);
			info.drawType = drawType;
			info.depthTarget =  nullptr;
			info.colorTargets[0] = vkGlobals.colorTarget;
			info.depthFunc = maple::StencilType::Always;//zTest ? maple::StencilType::LessOrEqual : maple::StencilType::Always;
			info.swapChainTarget = info.colorTargets[0] == nullptr;
			info.depthWriteEnable = true;//zWritable
			info.cullMode = maple::CullMode::None;
			info.blendMode = maple::BlendMode::None;
			info.transparencyEnabled = false;
			info.depthTest = false;
			return maple::Pipeline::get(info);
		}


		void openIm2D(uint32_t width, uint32_t height)
		{
#include "vkshaders/im2d.shader.h"
			const std::string defaultTxt{ (char*)__im2d_shader, __im2d_shader_len };
			im2dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			indexBuffer = maple::IndexBuffer::create((uint16_t*)nullptr, STARTINDICES);
			vertexBuffer = maple::VertexBuffer::create(nullptr, STARTINDICES * sizeof(Im2DVertex));
			imguiSet = maple::DescriptorSet::create({ 0,getShader(im2dShader->shaderId).get() });
			imguiSet->setName("Texture CommonSet");
			imguiSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
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

		void
			im2DRenderTriangle(void* vertices, int32 numVertices, int32 vert1, int32 vert2, int32 vert3)
		{
			Im2DVertex* verts = (Im2DVertex*)vertices;
			tmpprimbuf[0] = verts[vert1];
			tmpprimbuf[1] = verts[vert2];
			tmpprimbuf[2] = verts[vert3];
			im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
		}

		void im2DSetXform(void)
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
		}

		void im2DRenderPrimitive(PrimitiveType primType, void* vertices, int32 numVertices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], im2dShader->shaderId);
			vertexBuffer->setData(numVertices * sizeof(Im2DVertex), vertices);
			im2DSetXform();
			flushFog(imguiSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			vertexBuffer->bind(cmdBuffer, nullptr);
			imguiSet->update(cmdBuffer);
			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { imguiSet });
			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], numVertices);
			pipeline->end(cmdBuffer);
		}

		void im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices,
			void* indices, int32 numIndices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], im2dShader->shaderId);
			vertexBuffer->setData(numVertices * sizeof(Im2DVertex), vertices);
			indexBuffer->setData((const uint16_t*)indices, numIndices);
			im2DSetXform();
			flushFog(imguiSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			vertexBuffer->bind(cmdBuffer, nullptr);
			indexBuffer->bind(cmdBuffer);
			imguiSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { imguiSet });
			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices);
			pipeline->end(cmdBuffer);
		}

		void openIm3D(void)
		{

		}

		void  closeIm3D(void)
		{

		}

		void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags)
		{

		}

		void im3DRenderPrimitive(PrimitiveType primType)
		{

		}

		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices)
		{

		}

		void im3DEnd(void)
		{
		}

	}        // namespace vulkan
}        // namespace rw

void ImGui_SetCurrentTexture(void* tex)
{
	rw::Texture* texture = (rw::Texture*)tex;
	rw::vulkan::setTexture(rw::vulkan::imguiSet, 0, texture);
}
#endif
