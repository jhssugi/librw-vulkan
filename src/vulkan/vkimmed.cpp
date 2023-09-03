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

namespace rw
{
	namespace vulkan
	{
		
		void openIm2D(uint32_t width,uint32_t height)
		{

		}

		void closeIm2D(void)
		{
		}

		static Im2DVertex tmpprimbuf[3];

		void
			im2DRenderLine(void* vertices, int32 numVertices, int32 vert1, int32 vert2)
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

		void
			im2DSetXform(void)
		{

		}

		void im2DRenderPrimitive(PrimitiveType primType, void* vertices, int32 numVertices)
		{

		}

		void im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices,
			void* indices, int32 numIndices)
		{

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

#endif
