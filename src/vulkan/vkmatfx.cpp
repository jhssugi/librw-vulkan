#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

#include "rwvkshader.h"

#include "rwvkplg.h"

#include "rwvkimpl.h"

namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		static Shader* envShader, * envShader_noAT;
		static Shader* envShader_fullLight, * envShader_fullLight_noAT;
		static int32 u_texMatrix;
		static int32 u_fxparams;
		static int32 u_colorClamp;
		static int32 u_envColor;

		void matfxDefaultRender(InstanceDataHeader* header, InstanceData* inst, int32 vsBits, uint32 flags)
		{
			Material* m;
			m = inst->material;
			auto sets = getMaterialDescriptorSet(m);
			setMaterial(sets, flags, m->color, m->surfaceProps);

			setTexture(sets, 0, m->texture);

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
		}

		static Frame* lastEnvFrame;

		static RawMatrix normal2texcoord = {
			{ 0.5f,  0.0f, 0.0f }, 0.0f,
			{ 0.0f, -0.5f, 0.0f }, 0.0f,
			{ 0.0f,  0.0f, 1.0f }, 0.0f,
			{ 0.5f,  0.5f, 0.0f }, 1.0f
		};

		void uploadEnvMatrix(Frame* frame)
		{
			Matrix invMat;
			if (frame == nil)
				frame = engine->currentCamera->getFrame();

			// cache the matrix across multiple meshes
			static RawMatrix envMtx;
			// can't do it, frame matrix may change
			//	if(frame != lastEnvFrame){
			//		lastEnvFrame = frame;
			{

				RawMatrix invMtx;
				Matrix::invert(&invMat, frame->getLTM());
				convMatrix(&invMtx, &invMat);
				invMtx.pos.set(0.0f, 0.0f, 0.0f);
				float uscale = fabs(normal2texcoord.right.x);
				normal2texcoord.right.x = MatFX::envMapFlipU ? -uscale : uscale;
				RawMatrix::mult(&envMtx, &invMtx, &normal2texcoord);
			}
			//setUniform(u_texMatrix, &envMtx);
		}

		void matfxEnvRender(InstanceDataHeader* header, InstanceData* inst, int32 vsBits, uint32 flags, MatFX::Env* env)
		{
			Material* m;
			m = inst->material;

			if (env->tex == nil || env->coefficient == 0.0f) {
				matfxDefaultRender(header, inst, vsBits, flags);
				return;
			}
			auto sets = getMaterialDescriptorSet(m);

			setTexture(sets, 0, m->texture);
			setTexture(sets, 1, env->tex);
			uploadEnvMatrix(env->frame);

			setMaterial(sets,flags, m->color, m->surfaceProps);

			float fxparams[4];
			fxparams[0] = env->coefficient;
			fxparams[1] = env->fbAlpha ? 0.0f : 1.0f;
			fxparams[2] = fxparams[3] = 0.0f;

			//setUniform(u_fxparams, fxparams);
			static float zero[4];
			static float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			// This clamps the vertex color below. With it we can achieve both PC and PS2 style matfx
			/*if (MatFX::envMapApplyLight)
				setUniform(u_colorClamp, zero);
			else
				setUniform(u_colorClamp, one);*/
			RGBAf envcol[4];
			if (MatFX::envMapUseMatColor)
				convColor(envcol, &m->color);
			else
				convColor(envcol, &MatFX::envMapColor);
			//setUniform(u_envColor, envcol);

			rw::SetRenderState(VERTEXALPHA, 1);
			rw::SetRenderState(SRCBLEND, BLENDONE);

			if ((vsBits & VSLIGHT_MASK) == 0) {
				if (getAlphaTest())
					envShader->use();
				else
					envShader_noAT->use();
			}
			else {
				if (getAlphaTest())
					envShader_fullLight->use();
				else
					envShader_fullLight_noAT->use();
			}

			drawInst(header, inst);

			rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);
		}

		void matfxRenderCB(Atomic* atomic, InstanceDataHeader* header)
		{
			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);


			lastEnvFrame = nil;

			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			while (n--) {
				MatFX* matfx = MatFX::get(inst->material);

				if (matfx == nil)
					matfxDefaultRender(header, inst, vsBits, flags);
				else switch (matfx->type) {
				case MatFX::ENVMAP:
					matfxEnvRender(header, inst, vsBits, flags, &matfx->fx[0].env);
					break;
				default:
					matfxDefaultRender(header, inst, vsBits, flags);
					break;
				}
				inst++;
			}
		}

		ObjPipeline*
			makeMatFXPipeline(void)
		{
			ObjPipeline* pipe = ObjPipeline::create();
			pipe->instanceCB = defaultInstanceCB;
			pipe->uninstanceCB = defaultUninstanceCB;
			pipe->renderCB = matfxRenderCB;
			pipe->pluginID = ID_MATFX;
			pipe->pluginData = 0;
			return pipe;
		}

		static void* matfxOpen(void* o, int32, int32)
		{
			matFXGlobals.pipelines[PLATFORM_VULKAN] = makeMatFXPipeline();

#include "vkshaders/matfx.shader.h"

			const std::string defaultTxt = {(char *)__matfx_shader, __matfx_shader_len};

			envShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			assert(envShader);
			envShader_noAT = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n");
			assert(envShader_noAT);

			envShader_fullLight =
			    Shader::create(defaultTxt, "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
			                   "#define FRAGMENT_SHADER\n");
			assert(envShader_fullLight);
			envShader_fullLight_noAT =
			    Shader::create(defaultTxt, "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
			                   "#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n");
			assert(envShader_fullLight_noAT);

			return o;
		}

		static void* matfxClose(void* o, int32, int32)
		{
			((ObjPipeline*)matFXGlobals.pipelines[PLATFORM_VULKAN])->destroy();
			matFXGlobals.pipelines[PLATFORM_VULKAN] = nil;

			envShader->destroy();
			envShader = nil;
			envShader_noAT->destroy();
			envShader_noAT = nil;
			envShader_fullLight->destroy();
			envShader_fullLight = nil;
			envShader_fullLight_noAT->destroy();
			envShader_fullLight_noAT = nil;

			return o;
		}

		void initMatFX(void)
		{
			/*u_texMatrix = registerUniform("u_texMatrix", UNIFORM_MAT4);
			u_fxparams = registerUniform("u_fxparams", UNIFORM_VEC4);
			u_colorClamp = registerUniform("u_colorClamp", UNIFORM_VEC4);
			u_envColor = registerUniform("u_envColor", UNIFORM_VEC4);*/
			//Driver::registerPlugin(PLATFORM_VULKAN, 0, ID_MATFX, matfxOpen, matfxClose);
		}
#else
		void initMatFX(void) {}
#endif

	}
}

