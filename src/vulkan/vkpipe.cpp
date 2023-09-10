#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RW_VULKAN


#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include "DescriptorSet.h"

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwerror.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "rwvk.h"
#include "rwvkshader.h"
#include <unordered_map>
namespace rw
{
	namespace vulkan
	{
		// TODO: make some of these things platform-independent

#ifdef RW_VULKAN

		std::unordered_map<Material*, maple::DescriptorSet::Ptr> descriptorSets;

		std::shared_ptr<maple::DescriptorSet> getMaterialDescriptorSet(Material* material)
		{
			return descriptorSets[material];
		}

		void freeInstanceData(Geometry* geometry)
		{
			if (geometry->instData == nil ||
				geometry->instData->platform != PLATFORM_GL3)
				return;
			InstanceDataHeader* header = (InstanceDataHeader*)geometry->instData;
			geometry->instData = nil;

			{
				auto buffer = (maple::VertexBuffer*)header->vertexBufferGPU;
				delete buffer;
				header->vertexBufferGPU = nullptr;
			}

			{
				auto buffer = (maple::IndexBuffer*)header->indexBufferGPU;
				delete buffer;
				header->indexBufferGPU = nullptr;
			}

			rwFree(header->indexBuffer);
			rwFree(header->vertexBuffer);
			rwFree(header->attribDesc);
			rwFree(header->inst);
			rwFree(header);
		}

		void* destroyNativeData(void* object, int32, int32)
		{
			freeInstanceData((Geometry*)object);
			return object;
		}

		static InstanceDataHeader* instanceMesh(rw::ObjPipeline* rwpipe, Geometry* geo)
		{
			InstanceDataHeader* header = rwNewT(InstanceDataHeader, 1, MEMDUR_EVENT | ID_GEOMETRY);
			MeshHeader* meshh = geo->meshHeader;
			geo->instData = header;
			header->platform = PLATFORM_VULKAN;

			header->serialNumber = meshh->serialNum;
			header->numMeshes = meshh->numMeshes;
			header->primType = meshh->flags == 1 ? maple::DrawType::TriangleStrip : maple::DrawType::Triangle;
			header->totalNumVertex = geo->numVertices;
			header->totalNumIndex = meshh->totalIndices;
			header->inst = rwNewT(InstanceData, header->numMeshes, MEMDUR_EVENT | ID_GEOMETRY);
			header->indexBuffer = rwNewT(uint16, header->totalNumIndex, MEMDUR_EVENT | ID_GEOMETRY);
			InstanceData* inst = header->inst;
			Mesh* mesh = meshh->getMeshes();
			uint32 offset = 0;
			for (uint32 i = 0; i < header->numMeshes; i++) {
				findMinVertAndNumVertices(mesh->indices, mesh->numIndices, &inst->minVert, &inst->numVertices);
				assert(inst->minVert != 0xFFFFFFFF);
				inst->numIndex = mesh->numIndices;
				inst->material = mesh->material;

				if (descriptorSets.find(inst->material) == descriptorSets.end())
				{
					descriptorSets[inst->material] = maple::DescriptorSet::create({ 1, getShader(defaultShader->shaderId).get() });
				}

				inst->vertexAlpha = 0;
				inst->program = 0;
				inst->offset = offset;
				memcpy(header->indexBuffer + inst->offset, mesh->indices, inst->numIndex * sizeof(uint16_t));
				offset += inst->numIndex;// *2;
				mesh++;
				inst++;
			}

			header->vertexBuffer = nullptr;
			header->numAttribs = 0;
			header->vertexBufferGPU = nullptr;
			header->attribDesc = nullptr;
			header->indexBufferGPU = maple::IndexBuffer::createRaw(header->indexBuffer, header->totalNumIndex);
			return header;
		}

		static void instance(rw::ObjPipeline* rwpipe, Atomic* atomic)
		{
			ObjPipeline* pipe = (ObjPipeline*)rwpipe;
			Geometry* geo = atomic->geometry;
			// don't try to (re)instance native data
			if (geo->flags & Geometry::NATIVE)
				return;

			InstanceDataHeader* header = (InstanceDataHeader*)geo->instData;
			if (geo->instData)
			{
				// Already have instanced data, so check if we have to reinstance
				assert(header->platform == PLATFORM_VULKAN);
				if (header->serialNumber != geo->meshHeader->serialNum)
				{
					// Mesh changed, so reinstance everything
					freeInstanceData(geo);
				}
			}

			// no instance or complete reinstance
			if (geo->instData == nil)
			{
				geo->instData = instanceMesh(rwpipe, geo);
				pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 0);
			}
			else if (geo->lockedSinceInst)
				pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 1);

			geo->lockedSinceInst = 0;
		}

		static void	uninstance(rw::ObjPipeline* rwpipe, Atomic* atomic)
		{
			assert(0 && "can't uninstance");
		}

		static void render(rw::ObjPipeline* rwpipe, Atomic* atomic)
		{
			ObjPipeline* pipe = (ObjPipeline*)rwpipe;
			Geometry* geo = atomic->geometry;
			pipe->instance(atomic);
			assert(geo->instData != nil);
			assert(geo->instData->platform == PLATFORM_VULKAN);
			if (pipe->renderCB)
				pipe->renderCB(atomic, (InstanceDataHeader*)geo->instData);
		}

		void ObjPipeline::init(void)
		{
			this->rw::ObjPipeline::init(PLATFORM_VULKAN);
			this->impl.instance = vulkan::instance;
			this->impl.uninstance = vulkan::uninstance;
			this->impl.render = vulkan::render;
			this->instanceCB = nil;
			this->uninstanceCB = nil;
			this->renderCB = nil;
		}

		ObjPipeline* ObjPipeline::create(void)
		{
			ObjPipeline* pipe = rwNewT(ObjPipeline, 1, MEMDUR_GLOBAL);
			pipe->init();
			return pipe;
		}

		bool setVertexClor(int type, uint8* dst, RGBA* src, uint32 numVertices, uint32 stride)
		{
			uint8 alpha = 0xFF;
		
			if (type == VERT_ARGB) 
			{
				for (uint32 i = 0; i < numVertices; i++) {
					float* floatDst = reinterpret_cast<float*>(dst);
					floatDst[0] = src == nullptr ? 0.f : src->blue / 255.f;
					floatDst[1] = src == nullptr ? 0.f : src->green / 255.f;
					floatDst[2] = src == nullptr ? 0.f : src->red / 255.f;
					floatDst[3] = src == nullptr ? 1.f : src->alpha / 255.f;

					alpha &= src == nullptr ? 0xFF : src->alpha;
					dst += stride;
					if (src != nullptr) 
						src++;
				}
			}
			else if (type == VERT_RGBA)
			{
				for (uint32 i = 0; i < numVertices; i++)
				{
					float* floatDst = reinterpret_cast<float*>(dst);
					floatDst[0] = src == nullptr ? 0.f : src->blue / 255.f;
					floatDst[1] = src == nullptr ? 0.f : src->green / 255.f;
					floatDst[2] = src == nullptr ? 0.f : src->red / 255.f;
					floatDst[3] = src == nullptr ? 1.f : src->alpha / 255.f;

					alpha &= src == nullptr ? 0xFF : src->alpha;
					dst += stride;

					if (src != nullptr)
						src++;
				}
			}
			else
				assert(0 && "unsupported color type");
			return alpha != 0xFF;
		}


		void defaultInstanceCB(Geometry* geo, InstanceDataHeader* header, bool32 reinstance)
		{
			AttribDesc* attribs, * a;

			bool isPrelit = true;//!!(geo->flags & Geometry::PRELIT);
			bool hasNormals = !!(geo->flags & Geometry::NORMALS);

			if (!reinstance)
			{
				AttribDesc tmpAttribs[12];
				uint32     stride;

				//
				// Create attribute descriptions
				//
				a = tmpAttribs;
				stride = 0;

				// Positions
				a->index = ATTRIB_POS;
				a->size = 3;
				a->type = GL_FLOAT;
				a->offset = stride;
				stride += 12;
				a++;

				// Normals
				// TODO: compress
				//if (hasNormals)
				{
					a->index = ATTRIB_NORMAL;
					a->size = 3;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 12;
					a++;
				}

				// Prelighting
				if (isPrelit)
				{
					a->index = ATTRIB_COLOR;
					a->size = 4;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 16;
					a++;
				}

				geo->numTexCoordSets = std::max(geo->numTexCoordSets, 1);

				// Texture coordinates
				for (int32 n = 0; n < geo->numTexCoordSets; n++)
				{
					a->index = ATTRIB_TEXCOORDS0 + n;
					a->size = 2;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 8;
					a++;
				}

				header->numAttribs = a - tmpAttribs;
				for (a = tmpAttribs; a != &tmpAttribs[header->numAttribs]; a++)
					a->stride = stride;

				header->attribDesc = rwNewT(AttribDesc, header->numAttribs, MEMDUR_EVENT | ID_GEOMETRY);
				memcpy(header->attribDesc, tmpAttribs, header->numAttribs * sizeof(AttribDesc));
				//
				// Allocate vertex buffer
				//
				header->vertexBuffer = rwNewT(uint8, header->totalNumVertex * stride, MEMDUR_EVENT | ID_GEOMETRY);
				assert(header->vertexBufferGPU == nullptr);
			}

			attribs = header->attribDesc;

			assert(attribs->stride == 48);

			uint8* verts = header->vertexBuffer;

			struct Vertex 
			{
				V3d pos;
				V3d normal;
				V4d color;
				V2d tex0;
			};

			// Positions
			if (!reinstance || geo->lockedSinceInst & Geometry::LOCKVERTICES)
			{
				for (a = attribs; a->index != ATTRIB_POS; a++)
					;
				instV3d(VERT_FLOAT3, verts + a->offset,
					geo->morphTargets[0].vertices,
					header->totalNumVertex, a->stride);
			}

			// Normals
			if ((!reinstance || geo->lockedSinceInst & Geometry::LOCKNORMALS))
			{
				for (a = attribs; a->index != ATTRIB_NORMAL; a++)
					;
				instV3d(VERT_FLOAT3, verts + a->offset, geo->morphTargets[0].normals, header->totalNumVertex, a->stride);
				if(!hasNormals) 
				{
					if(header->totalNumIndex > 0) {
						auto vertexBuff = reinterpret_cast<Vertex *>(verts);
						for(uint32_t i = 0; i < header->totalNumIndex; i += 3) {
							const auto a = header->indexBuffer[i];
							const auto b = header->indexBuffer[i + 1];
							const auto c = header->indexBuffer[i + 2];
							const auto normal = rw::cross(rw::sub(vertexBuff[b].pos, vertexBuff[a].pos),
							                              rw::sub(vertexBuff[c].pos, vertexBuff[a].pos));
							vertexBuff[a].normal = rw::add(vertexBuff[a].normal, normal);
							vertexBuff[b].normal = rw::add(vertexBuff[b].normal, normal);
							vertexBuff[c].normal = rw::add(vertexBuff[c].normal, normal);
						}

						for(uint32_t i = 0; i < header->totalNumVertex; ++i) 
						{ 
							vertexBuff[i].normal = normalize(vertexBuff[i].normal);
						}
					} 
					else 
					{
						assert(0 && "Todo.........");
					}
				}
			}

			// Prelighting
			if (isPrelit && (!reinstance || geo->lockedSinceInst & Geometry::LOCKPRELIGHT))
			{
				for (a = attribs; a->index != ATTRIB_COLOR; a++)
					;
				int           n = header->numMeshes;
				InstanceData* inst = header->inst;
				while (n--)
				{
					assert(inst->minVert != 0xFFFFFFFF);
					inst->vertexAlpha = setVertexClor(VERT_RGBA, 
						verts + a->offset + a->stride * inst->minVert, 
						geo->colors == nullptr ? nullptr : geo->colors + inst->minVert, 
						inst->numVertices,
						a->stride);
					inst++;
				}
			}

			// Texture coordinates
			for (int32 n = 0; n < geo->numTexCoordSets; n++)
			{
				if (!reinstance || geo->lockedSinceInst & (Geometry::LOCKTEXCOORDS << n))
				{
					for (a = attribs; a->index != ATTRIB_TEXCOORDS0 + n; a++)
						;
					instTexCoords(VERT_FLOAT2, verts + a->offset,
						geo->texCoords[n],
						header->totalNumVertex, a->stride);
				}
			}

			header->vertexBufferGPU = maple::VertexBuffer::createRaw(
				header->vertexBuffer,
				header->totalNumVertex * attribs[0].stride);
		}

		void  defaultUninstanceCB(Geometry* geo, InstanceDataHeader* header)
		{
			assert(0 && "can't uninstance");
		}

		ObjPipeline* makeDefaultPipeline(void)
		{
			ObjPipeline* pipe = ObjPipeline::create();
			pipe->instanceCB = defaultInstanceCB;
			pipe->uninstanceCB = defaultUninstanceCB;
			pipe->renderCB = defaultRenderCB;
			return pipe;
		}
#endif
	}        // namespace vulkan
}        // namespace rw

#endif // RW_VULKAN
