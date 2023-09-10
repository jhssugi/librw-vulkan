#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "../rwbase.h"

#include "../rwerror.h"

#include "../rwplg.h"

#include "../rwrender.h"

#include "../rwengine.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwanim.h"

#include "../rwplugins.h"

#include "rwvk.h"
#include "rwvkplg.h"
#include "rwvkshader.h"

#include "rwvkimpl.h"


namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		static Shader *skinShader, *skinShader_noAT;
		static Shader *skinShader_fullLight, *skinShader_fullLight_noAT;
		static int32   u_boneMatrices;

		void skinInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
		{ 
		}

		void
		    skinUninstanceCB(Geometry *geo, InstanceDataHeader *header)
		{
			assert(0 && "can't uninstance");
		}

		static float skinMatrices[64 * 16];

		void uploadSkinMatrices(Atomic *a)
		{
		}

		void skinRenderCB(Atomic *atomic, InstanceDataHeader *header)
		{
		}

		static void *skinOpen(void *o, int32, int32)
		{
			return nullptr;
		}

		static void *skinClose(void *o, int32, int32)
		{
			((ObjPipeline *) skinGlobals.pipelines[PLATFORM_VULKAN])->destroy();
			skinGlobals.pipelines[PLATFORM_VULKAN] = nil;

			skinShader->destroy();
			skinShader = nil;
			skinShader_noAT->destroy();
			skinShader_noAT = nil;
			skinShader_fullLight->destroy();
			skinShader_fullLight = nil;
			skinShader_fullLight_noAT->destroy();
			skinShader_fullLight_noAT = nil;

			return o;
		}

		void initSkin(void)
		{
		}

		ObjPipeline *makeSkinPipeline(void)
		{
			return nullptr;
		}

#else
		void initSkin(void)
		{}
#endif

	}        // namespace vulkan
}        // namespace rw
