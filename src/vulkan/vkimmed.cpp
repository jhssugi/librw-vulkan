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

		struct ImObject
		{
			//using Ptr = std::shared_ptr<ImObject>;
			maple::VertexBuffer::Ptr vertexBuffer;
			maple::IndexBuffer::Ptr indexBuffer;

			maple::DescriptorSet::Ptr objectSet;
			maple::DescriptorSet::Ptr lightSet;
		};

		std::vector<ImObject> imObjects;


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

			auto & currentObject = imObjects.emplace_back();

			currentObject.vertexBuffer = maple::VertexBuffer::create(vertices, numVertices * sizeof(Im2DVertex));
			currentObject.objectSet = maple::DescriptorSet::create({ 0,getShader(im2dShader->shaderId).get() });

			im2DSetXform(currentObject.objectSet);
			flushFog(currentObject.objectSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			currentObject.vertexBuffer->bind(cmdBuffer, nullptr);
			currentObject.objectSet->update(cmdBuffer);
			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { currentObject.objectSet });
			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], numVertices);
			pipeline->end(cmdBuffer);
		}

		void im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices,
			void* indices, int32 numIndices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], im2dShader->shaderId);

			auto& currentObject = imObjects.emplace_back();


			currentObject.vertexBuffer = maple::VertexBuffer::create(vertices, numVertices * sizeof(Im2DVertex));
			currentObject.indexBuffer = maple::IndexBuffer::create((uint16_t*)indices, numIndices);
			currentObject.objectSet = maple::DescriptorSet::create({ 0,getShader(im2dShader->shaderId).get() });

			im2DSetXform(currentObject.objectSet);
			flushFog(currentObject.objectSet);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
		
			currentObject.vertexBuffer->bind(cmdBuffer, nullptr);
			currentObject.indexBuffer->bind(cmdBuffer);
			currentObject.objectSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { currentObject.objectSet });
			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices);
			pipeline->end(cmdBuffer);
		}

		void openIm3D(void)
		{
#include "vkshaders/im3d.shader.h"
			const std::string defaultTxt{ (char*)__im3d_shader, __im3d_shader_len };
			im3dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
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
				int32 vsBits = lightingCB();
				defaultShader_fullLight->use();
			}
			else
			{
				im3dShader->use();
			}

			auto &currentObject = imObjects.emplace_back();
			currentObject.vertexBuffer = maple::VertexBuffer::create(vertices, numVertices * sizeof(Im3DVertex));
			currentObject.objectSet = maple::DescriptorSet::create({ 0,getShader(currentShader->shaderId).get() });
			rw::Raster *rast = (rw::Raster*)rw::GetRenderStatePtr(rw::TEXTURERASTER);

			if (rast != nullptr) 
			{
				auto vkRst = GET_VULKAN_RASTEREXT(rast);
				currentObject.objectSet->setTexture("tex0", getTexture(vkRst->textureId));
			}
			else
			{
				currentObject.objectSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
			}

			if ((flags & im3d::VERTEXUV) == 0)
			{
				currentObject.objectSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
			}
			g_numVertices = numVertices;
		}

		void im3DRenderPrimitive(PrimitiveType primType)
		{
			auto pipeline = createPipeline(primTypeMap[primType], currentShader->shaderId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			auto& currentObject = imObjects.back();
			currentObject.vertexBuffer->bind(cmdBuffer, nullptr);

			flushCache(getShader(currentShader->shaderId), currentObject.lightSet);
			flushFog(currentObject.objectSet);
			
			currentObject.objectSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
				MAPLE_ASSERT(false, "Not support now");
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { currentObject.objectSet });
			}

			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], g_numVertices);

			pipeline->end(cmdBuffer);
		}

		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices)
		{
			auto pipeline = createPipeline(primTypeMap[primType], currentShader->shaderId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			auto& currentObject = imObjects.back();

			if (currentObject.indexBuffer== nullptr) 
			{
				currentObject.indexBuffer = maple::IndexBuffer::create((uint16_t*)indices, numIndices);
			}

			currentObject.indexBuffer->bind(cmdBuffer);
			currentObject.vertexBuffer->bind(cmdBuffer, nullptr);

			flushCache(getShader(currentShader->shaderId), currentObject.lightSet);
			flushFog(currentObject.objectSet);
			currentObject.objectSet->update(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { currentObject.objectSet });
			}

			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices);
			pipeline->end(cmdBuffer);
		}

		void im3DEnd(void)
		{
		}

		void imFlush()
		{
			imObjects.clear();
		}

	}        // namespace vulkan
}        // namespace rw

#endif
