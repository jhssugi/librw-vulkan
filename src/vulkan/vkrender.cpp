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

namespace rw
{
	namespace vulkan
	{
#	define MAX_LIGHTS

		void drawInst_simple(InstanceDataHeader *header, InstanceData *inst)
		{
			flushCache();
		}

		// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
		void
		    drawInst_GSemu(InstanceDataHeader *header, InstanceData *inst)
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

		void
		    drawInst(InstanceDataHeader *header, InstanceData *inst)
		{
			if (rw::GetRenderState(rw::GSALPHATEST))
				drawInst_GSemu(header, inst);
			else
				drawInst_simple(header, inst);
		}

		void
		    setAttribPointers(AttribDesc *attribDescs, int32 numAttribs)
		{
			AttribDesc *a;
			for (a = attribDescs; a != &attribDescs[numAttribs]; a++)
			{
			}
		}

		void
		    disableAttribPointers(AttribDesc *attribDescs, int32 numAttribs)
		{
			AttribDesc *a;
		}

		void
		    setupVertexInput(InstanceDataHeader *header)
		{
		}

		void
		    teardownVertexInput(InstanceDataHeader *header)
		{
		}

		int32
		    lightingCB(Atomic *atomic)
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

		int32
		    lightingCB(void)
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

		void
		    defaultRenderCB(Atomic *atomic, InstanceDataHeader *header)
		{
			Material *m;

			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			setupVertexInput(header);

			InstanceData *inst = header->inst;
			int32         n    = header->numMeshes;
		}
	}        // namespace vulkan
}        // namespace rw

#endif
