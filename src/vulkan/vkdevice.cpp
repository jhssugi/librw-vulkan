#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwerror.h"
#include "../rwrender.h"

#ifdef RW_VULKAN

#	include "rwvk.h"

#	include "rwvkimpl.h"

#	include "rwvkshader.h"

#	define PLUGIN_ID 0


#include "GraphicsContext.h"
#include "RenderDevice.h"
#include "ImGuiRenderer.h"
#include "ShaderCompiler.h"
#include "UniformBuffer.h"
#include "DescriptorSet.h"
#include "Textures.h"

namespace rw
{
	namespace vulkan
	{
		static maple::ImGuiRenderer::Ptr imRender;

		VkGlobals vkGlobals;
		// terrible hack for GLES
		bool32 needToReadBackTextures;

		int32   alphaFunc;
		float32 alphaRef;

		struct UniformState
		{
			float32 alphaRefLow;
			float32 alphaRefHigh;
			int32   pad[2];

			float32 fogStart;
			float32 fogEnd;
			float32 fogRange;
			float32 fogDisable;

			RGBAf fogColor;
		};

		struct UniformScene
		{
			float32 proj[16];
			float32 view[16];
		};

#	define MAX_LIGHTS 8

		struct UniformObject
		{
			RawMatrix world;
			RGBAf     ambLight;
			struct
			{
				float type;
				float radius;
				float minusCosAngle;
				float hardSpot;
			} lightParams[MAX_LIGHTS];
			V4d   lightPosition[MAX_LIGHTS];
			V4d   lightDirection[MAX_LIGHTS];
			RGBAf lightColor[MAX_LIGHTS];
		};


		static maple::UniformBuffer::Ptr ubo_state, ubo_scene, ubo_object;

		static maple::DescriptorSet::Ptr commonSet;

		static GLuint        whitetex;
		static UniformState  uniformState;
		static UniformScene  uniformScene;
		static UniformObject uniformObject;


		Shader* defaultShader, * defaultShader_noAT;
		Shader* defaultShader_fullLight, * defaultShader_fullLight_noAT;

		static bool32 stateDirty = 1;
		static bool32 sceneDirty = 1;
		static bool32 objectDirty = 1;

		struct RwRasterStateCache
		{
			Raster* raster;
			Texture::Addressing addressingU;
			Texture::Addressing addressingV;
			Texture::FilterMode filter;
		};

#	define MAXNUMSTAGES 8

		// cached RW render states
		struct RwStateCache
		{
			bool32  vertexAlpha;
			uint32  alphaTestEnable;
			uint32  alphaFunc;
			bool32  textureAlpha;
			bool32  blendEnable;
			uint32  srcblend, destblend;
			uint32  zwrite;
			uint32  ztest;
			uint32  cullmode;
			uint32  stencilenable;
			uint32  stencilpass;
			uint32  stencilfail;
			uint32  stencilzfail;
			uint32  stencilfunc;
			uint32  stencilref;
			uint32  stencilmask;
			uint32  stencilwritemask;
			uint32  fogEnable;
			float32 fogStart;
			float32 fogEnd;

			// emulation of PS2 GS
			bool32 gsalpha;
			uint32 gsalpharef;

			RwRasterStateCache texstage[MAXNUMSTAGES];
		};
		static RwStateCache rwStateCache;

		enum
		{
			// actual gl states
			RWGL_BLEND,
			RWGL_SRCBLEND,
			RWGL_DESTBLEND,
			RWGL_DEPTHTEST,
			RWGL_DEPTHFUNC,
			RWGL_DEPTHMASK,
			RWGL_CULL,
			RWGL_CULLFACE,
			RWGL_STENCIL,
			RWGL_STENCILFUNC,
			RWGL_STENCILFAIL,
			RWGL_STENCILZFAIL,
			RWGL_STENCILPASS,
			RWGL_STENCILREF,
			RWGL_STENCILMASK,
			RWGL_STENCILWRITEMASK,

			// uniforms
			RWGL_ALPHAFUNC,
			RWGL_ALPHAREF,
			RWGL_FOG,
			RWGL_FOGSTART,
			RWGL_FOGEND,
			RWGL_FOGCOLOR,

			RWGL_NUM_STATES
		};
		static bool uniformStateDirty[RWGL_NUM_STATES];

		struct GlState
		{
			bool32 blendEnable;
			uint32 srcblend, destblend;

			bool32 depthTest;
			uint32 depthFunc;

			uint32 depthMask;

			bool32 cullEnable;
			uint32 cullFace;

			bool32 stencilEnable;
			// glStencilFunc
			uint32 stencilFunc;
			uint32 stencilRef;
			uint32 stencilMask;
			// glStencilOp
			uint32 stencilPass;
			uint32 stencilFail;
			uint32 stencilZFail;
			// glStencilMask
			uint32 stencilWriteMask;
		};
		static GlState curGlState, oldGlState;

		static int32  activeTexture;
		static uint32 boundTexture[MAXNUMSTAGES];

		static uint32 currentFramebuffer;

		static uint32 blendMap[] = {
			0 };

		static uint32 stencilOpMap[] = {
			0 };

		static uint32 stencilFuncMap[] = { 0 };

		static float maxAnisotropy;

		/*
 * GL state cache
 */

		void setGlRenderState(uint32 state, uint32 value)
		{
			switch (state)
			{
			case RWGL_BLEND:
				curGlState.blendEnable = value;
				break;
			case RWGL_SRCBLEND:
				curGlState.srcblend = value;
				break;
			case RWGL_DESTBLEND:
				curGlState.destblend = value;
				break;
			case RWGL_DEPTHTEST:
				curGlState.depthTest = value;
				break;
			case RWGL_DEPTHFUNC:
				curGlState.depthFunc = value;
				break;
			case RWGL_DEPTHMASK:
				curGlState.depthMask = value;
				break;
			case RWGL_CULL:
				curGlState.cullEnable = value;
				break;
			case RWGL_CULLFACE:
				curGlState.cullFace = value;
				break;
			case RWGL_STENCIL:
				curGlState.stencilEnable = value;
				break;
			case RWGL_STENCILFUNC:
				curGlState.stencilFunc = value;
				break;
			case RWGL_STENCILFAIL:
				curGlState.stencilFail = value;
				break;
			case RWGL_STENCILZFAIL:
				curGlState.stencilZFail = value;
				break;
			case RWGL_STENCILPASS:
				curGlState.stencilPass = value;
				break;
			case RWGL_STENCILREF:
				curGlState.stencilRef = value;
				break;
			case RWGL_STENCILMASK:
				curGlState.stencilMask = value;
				break;
			case RWGL_STENCILWRITEMASK:
				curGlState.stencilWriteMask = value;
				break;
			}
		}

		void setAlphaBlend(bool32 enable)
		{
			if (rwStateCache.blendEnable != enable)
			{
				rwStateCache.blendEnable = enable;
				setGlRenderState(RWGL_BLEND, enable);
			}
		}

		bool32
			getAlphaBlend(void)
		{
			return rwStateCache.blendEnable;
		}

		bool32 getAlphaTest(void)
		{
			return rwStateCache.alphaTestEnable;
		}

		static void setDepthTest(bool32 enable)
		{
		}

		static void setDepthWrite(bool32 enable)
		{
		}

		static void
			setAlphaTest(bool32 enable)
		{
			uint32 shaderfunc;
			if (rwStateCache.alphaTestEnable != enable)
			{
				rwStateCache.alphaTestEnable = enable;
				shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
				if (alphaFunc != shaderfunc)
				{
					alphaFunc = shaderfunc;
					uniformStateDirty[RWGL_ALPHAFUNC] = true;
					stateDirty = 1;
				}
			}
		}

		static void
			setAlphaTestFunction(uint32 function)
		{
			uint32 shaderfunc;
			if (rwStateCache.alphaFunc != function)
			{
				rwStateCache.alphaFunc = function;
				shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
				if (alphaFunc != shaderfunc)
				{
					alphaFunc = shaderfunc;
					uniformStateDirty[RWGL_ALPHAFUNC] = true;
					stateDirty = 1;
				}
			}
		}

		static void
			setVertexAlpha(bool32 enable)
		{
			if (rwStateCache.vertexAlpha != enable)
			{
				if (!rwStateCache.textureAlpha)
				{
					setAlphaBlend(enable);
					setAlphaTest(enable);
				}
				rwStateCache.vertexAlpha = enable;
			}
		}

		static void
			setActiveTexture(int32 n)
		{
			if (activeTexture != n)
			{
				activeTexture = n;
			}
		}

		uint32
			bindTexture(uint32 texid)
		{
			uint32 prev = boundTexture[activeTexture];
			return prev;
		}

		void bindFramebuffer(uint32 fbo)
		{
		}

		static void setFilterMode(uint32 stage, int32 filter, int32 maxAniso = 1)
		{
		}

		static void setAddressU(uint32 stage, int32 addressing)
		{
		}

		static void setAddressV(uint32 stage, int32 addressing)
		{
		}

		static void setRasterStageOnly(uint32 stage, Raster* raster)
		{
		}

		static void setRasterStage(uint32 stage, Raster* raster)
		{
		}

		void evictRaster(Raster* raster)
		{
			int i;
			for (i = 0; i < MAXNUMSTAGES; i++)
			{
				//assert(rwStateCache.texstage[i].raster != raster);
				if (rwStateCache.texstage[i].raster != raster)
					continue;
				setRasterStage(i, nil);
			}
		}

		void setTexture(std::shared_ptr<maple::DescriptorSet> sets, int32 stage, Texture* tex)
		{
			auto texture = maple::Texture2D::getTexture1X1White();
			VulkanRaster* natras = nullptr;
			if (tex != nullptr)
			{
				natras = PLUGINOFFSET(VulkanRaster, tex->raster, nativeRasterOffset);
			}
			sets->setTexture("tex" + std::to_string(stage), tex == nullptr ? texture : getTexture(natras->textureId));
		}

		static void setRenderState(int32 state, void* pvalue)
		{
			uint32 value = (uint32)(uintptr)pvalue;
			switch (state)
			{
			case TEXTURERASTER:
				setRasterStage(0, (Raster*)pvalue);
				break;
			case TEXTUREADDRESS:
				setAddressU(0, value);
				setAddressV(0, value);
				break;
			case TEXTUREADDRESSU:
				setAddressU(0, value);
				break;
			case TEXTUREADDRESSV:
				setAddressV(0, value);
				break;
			case TEXTUREFILTER:
				setFilterMode(0, value);
				break;
			case VERTEXALPHA:
				setVertexAlpha(value);
				break;
			case SRCBLEND:
				if (rwStateCache.srcblend != value)
				{
					rwStateCache.srcblend = value;
					setGlRenderState(RWGL_SRCBLEND, blendMap[rwStateCache.srcblend]);
				}
				break;
			case DESTBLEND:
				if (rwStateCache.destblend != value)
				{
					rwStateCache.destblend = value;
					setGlRenderState(RWGL_DESTBLEND, blendMap[rwStateCache.destblend]);
				}
				break;
			case ZTESTENABLE:
				setDepthTest(value);
				break;
			case ZWRITEENABLE:
				setDepthWrite(value);
				break;
			case FOGENABLE:
				if (rwStateCache.fogEnable != value)
				{
					rwStateCache.fogEnable = value;
					uniformStateDirty[RWGL_FOG] = true;
					stateDirty = 1;
				}
				break;
			case FOGCOLOR:
				// no cache check here...too lazy
				RGBA c;
				c.red = value;
				c.green = value >> 8;
				c.blue = value >> 16;
				c.alpha = value >> 24;
				convColor(&uniformState.fogColor, &c);
				uniformStateDirty[RWGL_FOGCOLOR] = true;
				stateDirty = 1;
				break;
			case CULLMODE:
				if (rwStateCache.cullmode != value)
				{
					rwStateCache.cullmode = value;
					if (rwStateCache.cullmode == CULLNONE)
						setGlRenderState(RWGL_CULL, false);
					else
					{
						setGlRenderState(RWGL_CULL, true);
						setGlRenderState(RWGL_CULLFACE, rwStateCache.cullmode == CULLBACK ? GL_BACK : GL_FRONT);
					}
				}
				break;

			case STENCILENABLE:
				if (rwStateCache.stencilenable != value)
				{
					rwStateCache.stencilenable = value;
					setGlRenderState(RWGL_STENCIL, value);
				}
				break;
			case STENCILFAIL:
				if (rwStateCache.stencilfail != value)
				{
					rwStateCache.stencilfail = value;
					setGlRenderState(RWGL_STENCILFAIL, stencilOpMap[value]);
				}
				break;
			case STENCILZFAIL:
				if (rwStateCache.stencilzfail != value)
				{
					rwStateCache.stencilzfail = value;
					setGlRenderState(RWGL_STENCILZFAIL, stencilOpMap[value]);
				}
				break;
			case STENCILPASS:
				if (rwStateCache.stencilpass != value)
				{
					rwStateCache.stencilpass = value;
					setGlRenderState(RWGL_STENCILPASS, stencilOpMap[value]);
				}
				break;
			case STENCILFUNCTION:
				if (rwStateCache.stencilfunc != value)
				{
					rwStateCache.stencilfunc = value;
					setGlRenderState(RWGL_STENCILFUNC, stencilFuncMap[value]);
				}
				break;
			case STENCILFUNCTIONREF:
				if (rwStateCache.stencilref != value)
				{
					rwStateCache.stencilref = value;
					setGlRenderState(RWGL_STENCILREF, value);
				}
				break;
			case STENCILFUNCTIONMASK:
				if (rwStateCache.stencilmask != value)
				{
					rwStateCache.stencilmask = value;
					setGlRenderState(RWGL_STENCILMASK, value);
				}
				break;
			case STENCILFUNCTIONWRITEMASK:
				if (rwStateCache.stencilwritemask != value)
				{
					rwStateCache.stencilwritemask = value;
					setGlRenderState(RWGL_STENCILWRITEMASK, value);
				}
				break;

			case ALPHATESTFUNC:
				setAlphaTestFunction(value);
				break;
			case ALPHATESTREF:
				if (alphaRef != value / 255.0f)
				{
					alphaRef = value / 255.0f;
					uniformStateDirty[RWGL_ALPHAREF] = true;
					stateDirty = 1;
				}
				break;
			case GSALPHATEST:
				rwStateCache.gsalpha = value;
				break;
			case GSALPHATESTREF:
				rwStateCache.gsalpharef = value;
			}
		}

		static void* getRenderState(int32 state)
		{
			uint32 val;
			RGBA   rgba;
			switch (state)
			{
			case TEXTURERASTER:
				return rwStateCache.texstage[0].raster;
			case TEXTUREADDRESS:
				if (rwStateCache.texstage[0].addressingU == rwStateCache.texstage[0].addressingV)
					val = rwStateCache.texstage[0].addressingU;
				else
					val = 0;        // invalid
				break;
			case TEXTUREADDRESSU:
				val = rwStateCache.texstage[0].addressingU;
				break;
			case TEXTUREADDRESSV:
				val = rwStateCache.texstage[0].addressingV;
				break;
			case TEXTUREFILTER:
				val = rwStateCache.texstage[0].filter;
				break;

			case VERTEXALPHA:
				val = rwStateCache.vertexAlpha;
				break;
			case SRCBLEND:
				val = rwStateCache.srcblend;
				break;
			case DESTBLEND:
				val = rwStateCache.destblend;
				break;
			case ZTESTENABLE:
				val = rwStateCache.ztest;
				break;
			case ZWRITEENABLE:
				val = rwStateCache.zwrite;
				break;
			case FOGENABLE:
				val = rwStateCache.fogEnable;
				break;
			case FOGCOLOR:
				convColor(&rgba, &uniformState.fogColor);
				val = RWRGBAINT(rgba.red, rgba.green, rgba.blue, rgba.alpha);
				break;
			case CULLMODE:
				val = rwStateCache.cullmode;
				break;

			case STENCILENABLE:
				val = rwStateCache.stencilenable;
				break;
			case STENCILFAIL:
				val = rwStateCache.stencilfail;
				break;
			case STENCILZFAIL:
				val = rwStateCache.stencilzfail;
				break;
			case STENCILPASS:
				val = rwStateCache.stencilpass;
				break;
			case STENCILFUNCTION:
				val = rwStateCache.stencilfunc;
				break;
			case STENCILFUNCTIONREF:
				val = rwStateCache.stencilref;
				break;
			case STENCILFUNCTIONMASK:
				val = rwStateCache.stencilmask;
				break;
			case STENCILFUNCTIONWRITEMASK:
				val = rwStateCache.stencilwritemask;
				break;

			case ALPHATESTFUNC:
				val = rwStateCache.alphaFunc;
				break;
			case ALPHATESTREF:
				val = (uint32)(alphaRef * 255.0f);
				break;
			case GSALPHATEST:
				val = rwStateCache.gsalpha;
				break;
			case GSALPHATESTREF:
				val = rwStateCache.gsalpharef;
				break;
			default:
				val = 0;
			}
			return (void*)(uintptr)val;
		}

		static void resetRenderState(void)
		{
			rwStateCache.alphaFunc = ALPHAGREATEREQUAL;
			alphaFunc = 0;
			alphaRef = 10.0f / 255.0f;
			uniformStateDirty[RWGL_ALPHAREF] = true;
			uniformState.fogDisable = 1.0f;
			uniformState.fogStart = 0.0f;
			uniformState.fogEnd = 0.0f;
			uniformState.fogRange = 0.0f;
			uniformState.fogColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			rwStateCache.gsalpha = 0;
			rwStateCache.gsalpharef = 128;
			stateDirty = 1;

			rwStateCache.vertexAlpha = 0;
			rwStateCache.textureAlpha = 0;
			rwStateCache.alphaTestEnable = 0;

			memset(&oldGlState, 0xFE, sizeof(oldGlState));

			rwStateCache.blendEnable = 0;
			setGlRenderState(RWGL_BLEND, false);
			rwStateCache.srcblend = BLENDSRCALPHA;
			rwStateCache.destblend = BLENDINVSRCALPHA;
			setGlRenderState(RWGL_SRCBLEND, blendMap[rwStateCache.srcblend]);
			setGlRenderState(RWGL_DESTBLEND, blendMap[rwStateCache.destblend]);

			rwStateCache.zwrite = GL_TRUE;
			setGlRenderState(RWGL_DEPTHMASK, rwStateCache.zwrite);

			rwStateCache.ztest = 1;
			setGlRenderState(RWGL_DEPTHTEST, true);
			setGlRenderState(RWGL_DEPTHFUNC, GL_LEQUAL);

			rwStateCache.cullmode = CULLNONE;
			setGlRenderState(RWGL_CULL, false);
			setGlRenderState(RWGL_CULLFACE, GL_BACK);

			rwStateCache.stencilenable = 0;
			setGlRenderState(RWGL_STENCIL, GL_FALSE);
			rwStateCache.stencilfail = STENCILKEEP;
			setGlRenderState(RWGL_STENCILFAIL, GL_KEEP);
			rwStateCache.stencilzfail = STENCILKEEP;
			setGlRenderState(RWGL_STENCILZFAIL, GL_KEEP);
			rwStateCache.stencilpass = STENCILKEEP;
			setGlRenderState(RWGL_STENCILPASS, GL_KEEP);
			rwStateCache.stencilfunc = STENCILALWAYS;
			setGlRenderState(RWGL_STENCILFUNC, GL_ALWAYS);
			rwStateCache.stencilref = 0;
			setGlRenderState(RWGL_STENCILREF, 0);
			rwStateCache.stencilmask = 0xFFFFFFFF;
			setGlRenderState(RWGL_STENCILMASK, 0xFFFFFFFF);
			rwStateCache.stencilwritemask = 0xFFFFFFFF;
			setGlRenderState(RWGL_STENCILWRITEMASK, 0xFFFFFFFF);

			activeTexture = -1;
			for (int i = 0; i < MAXNUMSTAGES; i++)
			{
				setActiveTexture(i);
				bindTexture(whitetex);
			}
			setActiveTexture(0);
		}

		void setWorldMatrix(Matrix* mat)
		{
			convMatrix(&uniformObject.world, mat);
			objectDirty = 1;
		}

		int32 setLights(WorldLights* lightData)
		{
			int    i, n;
			Light* l;
			int32  bits;

			uniformObject.ambLight = lightData->ambient;

			bits = 0;

			if (lightData->numAmbients)
				bits |= VSLIGHT_AMBIENT;

			n = 0;
			for (i = 0; i < lightData->numDirectionals && i < 8; i++)
			{
				l = lightData->directionals[i];
				uniformObject.lightParams[n].type = 1.0f;
				uniformObject.lightColor[n] = l->color;
				memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
				bits |= VSLIGHT_DIRECT;
				n++;
				if (n >= MAX_LIGHTS)
					goto out;
			}

			for (i = 0; i < lightData->numLocals; i++)
			{
				Light* l = lightData->locals[i];

				switch (l->getType())
				{
				case Light::POINT:
					uniformObject.lightParams[n].type = 2.0f;
					uniformObject.lightParams[n].radius = l->radius;
					uniformObject.lightColor[n] = l->color;
					memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
					bits |= VSLIGHT_POINT;
					n++;
					if (n >= MAX_LIGHTS)
						goto out;
					break;
				case Light::SPOT:
				case Light::SOFTSPOT:
					uniformObject.lightParams[n].type = 3.0f;
					uniformObject.lightParams[n].minusCosAngle = l->minusCosAngle;
					uniformObject.lightParams[n].radius = l->radius;
					uniformObject.lightColor[n] = l->color;
					memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
					memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
					// lower bound of falloff
					if (l->getType() == Light::SOFTSPOT)
						uniformObject.lightParams[n].hardSpot = 0.0f;
					else
						uniformObject.lightParams[n].hardSpot = 1.0f;
					bits |= VSLIGHT_SPOT;
					n++;
					if (n >= MAX_LIGHTS)
						goto out;
					break;
				}
			}

			uniformObject.lightParams[n].type = 0.0f;

		out:
			objectDirty = 1;
			return bits;
		}

		void setProjectionMatrix(float32* mat)
		{
			memcpy(&uniformScene.proj, mat, 64);
			sceneDirty = 1;
		}

		void setViewMatrix(float32* mat)
		{
			memcpy(&uniformScene.view, mat, 64);
			sceneDirty = 1;
		}

		Shader* lastShaderUploaded;

		void setMaterial(std::shared_ptr<maple::DescriptorSet> sets, const RGBA& color, const SurfaceProperties& surfaceprops, float extraSurfProp)
		{
			rw::RGBAf col;
			convColor(&col, &color);
			float surfProps[4];
			surfProps[0] = surfaceprops.ambient;
			surfProps[1] = surfaceprops.specular;
			surfProps[2] = surfaceprops.diffuse;
			surfProps[3] = extraSurfProp;
			sets->setUniformBufferData("Material", &surfProps);
		}

		void flushCache(void)
		{
			if (objectDirty)
			{
				ubo_object->setData(&uniformObject);
				objectDirty = 0;
			}
			if (sceneDirty)
			{
				ubo_scene->setData(&uniformScene);
				sceneDirty = 0;
			}
			if (stateDirty) {
				switch (alphaFunc) {
				case ALPHAALWAYS:
				default:
					uniformState.alphaRefLow = -1000.0f;
					uniformState.alphaRefHigh = 1000.0f;
					break;
				case ALPHAGREATEREQUAL:
					uniformState.alphaRefLow = alphaRef;
					uniformState.alphaRefHigh = 1000.0f;
					break;
				case ALPHALESS:
					uniformState.alphaRefLow = -1000.0f;
					uniformState.alphaRefHigh = alphaRef;
					break;
				}
				uniformState.fogDisable = rwStateCache.fogEnable ? 0.0f : 1.0f;
				uniformState.fogStart = rwStateCache.fogStart;
				uniformState.fogEnd = rwStateCache.fogEnd;
				uniformState.fogRange = 1.0f / (rwStateCache.fogStart - rwStateCache.fogEnd);
				ubo_state->setData(&uniformState);
				stateDirty = 0;
			}
		}

		static void setFrameBuffer(Camera* cam)
		{
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(cam->zBuffer);
			VulkanRaster* natras2 = GET_VULKAN_RASTEREXT(cam->frameBuffer);
			vkGlobals.currentDepth = getTexture(natras->textureId);
			vkGlobals.colorTarget = nullptr;
		}

		static Rect getFramebufferRect(Raster* frameBuffer)
		{
			Rect    r;
			Raster* fb = frameBuffer->parent;
			if (fb->type == Raster::CAMERA)
			{
				glfwGetFramebufferSize(vkGlobals.window, &r.w, &r.h);
			}
			else
			{
				r.w = fb->width;
				r.h = fb->height;
			}
			r.x = 0;
			r.y = 0;

			// Got a subraster
			if (frameBuffer != fb)
			{
				r.x = frameBuffer->offsetX;
				// GL y offset is from bottom
				r.y = r.h - frameBuffer->height - frameBuffer->offsetY;
				r.w = frameBuffer->width;
				r.h = frameBuffer->height;
			}

			return r;
		}

		static void setViewport(Raster* frameBuffer)
		{
			Rect r = getFramebufferRect(frameBuffer);
			if (r.w != vkGlobals.presentWidth || r.h != vkGlobals.presentHeight ||
				r.x != vkGlobals.presentOffX || r.y != vkGlobals.presentOffY)
			{
				vkGlobals.presentWidth = r.w;
				vkGlobals.presentHeight = r.h;
				vkGlobals.presentOffX = r.x;
				vkGlobals.presentOffY = r.y;
			}
		}

		static void beginUpdate(Camera* cam)
		{
			maple::RenderDevice::get()->begin();

			float view[16], proj[16];
			// View Matrix
			Matrix inv;
			Matrix::invert(&inv, cam->getFrame()->getLTM());
			// Since we're looking into positive Z,
			// flip X to ge a left handed view space.
			view[0] = -inv.right.x;
			view[1] = inv.right.y;
			view[2] = inv.right.z;
			view[3] = 0.0f;
			view[4] = -inv.up.x;
			view[5] = inv.up.y;
			view[6] = inv.up.z;
			view[7] = 0.0f;
			view[8] = -inv.at.x;
			view[9] = inv.at.y;
			view[10] = inv.at.z;
			view[11] = 0.0f;
			view[12] = -inv.pos.x;
			view[13] = inv.pos.y;
			view[14] = inv.pos.z;
			view[15] = 1.0f;
			memcpy(&cam->devView, &view, sizeof(RawMatrix));
			setViewMatrix(view);

			// Projection Matrix
			float32 invwx = 1.0f / cam->viewWindow.x;
			float32 invwy = 1.0f / cam->viewWindow.y;
			float32 invz = 1.0f / (cam->farPlane - cam->nearPlane);

			proj[0] = invwx;
			proj[1] = 0.0f;
			proj[2] = 0.0f;
			proj[3] = 0.0f;

			proj[4] = 0.0f;
			proj[5] = invwy;
			proj[6] = 0.0f;
			proj[7] = 0.0f;

			proj[8] = cam->viewOffset.x * invwx;
			proj[9] = cam->viewOffset.y * invwy;
			proj[12] = -proj[8];
			proj[13] = -proj[9];
			if (cam->projection == Camera::PERSPECTIVE)
			{
				proj[10] = (cam->farPlane + cam->nearPlane) * invz;
				proj[11] = 1.0f;

				proj[14] = -2.0f * cam->nearPlane * cam->farPlane * invz;
				proj[15] = 0.0f;
			}
			else
			{
				proj[10] = 2.0f * invz;
				proj[11] = 0.0f;

				proj[14] = -(cam->farPlane + cam->nearPlane) * invz;
				proj[15] = 1.0f;
			}
			memcpy(&cam->devProj, &proj, sizeof(RawMatrix));
			setProjectionMatrix(proj);

			if (rwStateCache.fogStart != cam->fogPlane)
			{
				rwStateCache.fogStart = cam->fogPlane;
				uniformStateDirty[RWGL_FOGSTART] = true;
				stateDirty = 1;
			}
			if (rwStateCache.fogEnd != cam->farPlane)
			{
				rwStateCache.fogEnd = cam->farPlane;
				uniformStateDirty[RWGL_FOGEND] = true;
				stateDirty = 1;
			}

			setFrameBuffer(cam);

			setViewport(cam->frameBuffer);
		}

		static void endUpdate(Camera* cam)
		{
			imRender->render();
			maple::RenderDevice::get()->presentInternal();
		}

		static void clearCamera(Camera* cam, RGBA* col, uint32 mode)
		{

		}

		static void showRaster(Raster* raster, uint32 flags)
		{

		}

		static bool32 rasterRenderFast(Raster* raster, int32 x, int32 y)
		{
			return 0;
		}

		static void addVideoMode(const GLFWvidmode* mode)
		{
			int i;

			for (i = 1; i < vkGlobals.numModes; i++)
			{
				if (vkGlobals.modes[i].mode.width == mode->width &&
					vkGlobals.modes[i].mode.height == mode->height &&
					vkGlobals.modes[i].mode.redBits == mode->redBits &&
					vkGlobals.modes[i].mode.greenBits == mode->greenBits &&
					vkGlobals.modes[i].mode.blueBits == mode->blueBits)
				{
					// had this mode already, remember highest refresh rate
					if (mode->refreshRate > vkGlobals.modes[i].mode.refreshRate)
						vkGlobals.modes[i].mode.refreshRate = mode->refreshRate;
					return;
				}
			}

			// none found, add
			vkGlobals.modes[vkGlobals.numModes].mode = *mode;
			vkGlobals.modes[vkGlobals.numModes].flags = VIDEOMODEEXCLUSIVE;
			vkGlobals.numModes++;
		}

		static void makeVideoModeList(void)
		{
			int                i, num;
			const GLFWvidmode* modes;

			modes = glfwGetVideoModes(vkGlobals.monitor, &num);
			rwFree(vkGlobals.modes);
			vkGlobals.modes = rwNewT(DisplayMode, num + 1, ID_DRIVER | MEMDUR_EVENT);

			vkGlobals.modes[0].mode = *glfwGetVideoMode(vkGlobals.monitor);
			vkGlobals.modes[0].flags = 0;
			vkGlobals.numModes = 1;

			for (i = 0; i < num; i++)
				addVideoMode(&modes[i]);

			for (i = 0; i < vkGlobals.numModes; i++)
			{
				num = vkGlobals.modes[i].mode.redBits +
					vkGlobals.modes[i].mode.greenBits +
					vkGlobals.modes[i].mode.blueBits;
				// set depth to power of two
				for (vkGlobals.modes[i].depth = 1; vkGlobals.modes[i].depth < num; vkGlobals.modes[i].depth <<= 1)
					;
			}
		}

		static int openGLFW(EngineOpenParams* openparams)
		{
			vkGlobals.winWidth = openparams->width;
			vkGlobals.winHeight = openparams->height;
			vkGlobals.winTitle = openparams->windowtitle;
			vkGlobals.pWindow = openparams->window;

			/* Init GLFW */
			if (!glfwInit())
			{
				RWERROR((ERR_GENERAL, "glfwInit() failed"));
				return 0;
			}

			vkGlobals.monitor = glfwGetMonitors(&vkGlobals.numMonitors)[0];

			makeVideoModeList();

			return 1;
		}

		static int closeGLFW(void)
		{
			glfwTerminate();
			return 1;
		}

		static void glfwerr(int error, const char* desc)
		{
			fprintf(stderr, "GLFW Error: %s\n", desc);
		}

		static int startGLFW(void)
		{
			GLFWwindow* win;
			DisplayMode* mode;

			maple::ShaderCompiler::init();

			mode = &vkGlobals.modes[vkGlobals.currentMode];

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

			glfwSetErrorCallback(glfwerr);
			glfwWindowHint(GLFW_RED_BITS, mode->mode.redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->mode.greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->mode.blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->mode.refreshRate);

			// GLX will round up to 2x or 4x if you ask for multisampling on with 1 sample
			// So only apply the SAMPLES hint if we actually want multisampling
			if (vkGlobals.numSamples > 1)
				glfwWindowHint(GLFW_SAMPLES, vkGlobals.numSamples);

			if (mode->flags & VIDEOMODEEXCLUSIVE)
				win = glfwCreateWindow(mode->mode.width, mode->mode.height, vkGlobals.winTitle, vkGlobals.monitor, nil);
			else
				win = glfwCreateWindow(vkGlobals.winWidth, vkGlobals.winHeight, vkGlobals.winTitle, nil, nil);

			if (win == nil)
			{
				RWERROR((ERR_GENERAL, "glfwCreateWindow() failed"));
				return 0;
			}

			glfwMakeContextCurrent(win);

			vkGlobals.window = win;
			*vkGlobals.pWindow = win;
			vkGlobals.presentWidth = 0;
			vkGlobals.presentHeight = 0;
			vkGlobals.presentOffX = 0;
			vkGlobals.presentOffY = 0;
			return 1;
		}

		static int stopGLFW(void)
		{
			maple::ShaderCompiler::finalize();
			glfwDestroyWindow(vkGlobals.window);
			return 1;
		}

		static int initVulkan(void)
		{
			maple::Console::init();
			maple::GraphicsContext::get()->init(vkGlobals.window, vkGlobals.winWidth, vkGlobals.winHeight);
			maple::RenderDevice::get()->init();

			imRender = maple::ImGuiRenderer::create(vkGlobals.winWidth, vkGlobals.winHeight, false);
			imRender->init();

#include "vkshaders/default.shader.h"

			const std::string defaultTxt{ (char*)__default_shader,__default_shader_len };

			defaultShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			defaultShader_noAT = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n");

			defaultShader_fullLight = Shader::create(defaultTxt, "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			defaultShader_fullLight_noAT = Shader::create(defaultTxt, "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt, "#define FRAGMENT_SHADER\n#define NO_ALPHATEST\n");

			ubo_state = maple::UniformBuffer::create(sizeof(UniformState), &uniformState);
			ubo_scene = maple::UniformBuffer::create(sizeof(UniformScene), &uniformScene);
			ubo_object = maple::UniformBuffer::create(sizeof(UniformObject), &uniformObject);

			commonSet = maple::DescriptorSet::create({ 0, getShader(defaultShader->shaderId).get() });

			openIm2D(vkGlobals.winWidth, vkGlobals.winHeight);
			openIm3D();
			return 1;
		}

		static int termVulkan(void)
		{
			closeIm3D();
			closeIm2D();

			defaultShader->destroy();
			defaultShader = nil;
			defaultShader_noAT->destroy();
			defaultShader_noAT = nil;
			defaultShader_fullLight->destroy();
			defaultShader_fullLight = nil;
			defaultShader_fullLight_noAT->destroy();
			defaultShader_fullLight_noAT = nil;

			whitetex = 0;

			return 1;
		}

		static int finalizeVulkan(void)
		{
			return 1;
		}

		static int deviceSystemGLFW(DeviceReq req, void* arg, int32 n)
		{
			GLFWmonitor** monitors;
			VideoMode* rwmode;

			switch (req) {
			case DEVICEOPEN:
				return openGLFW((EngineOpenParams*)arg);
			case DEVICECLOSE:
				return closeGLFW();

			case DEVICEINIT:
				return startGLFW() && initVulkan();
			case DEVICETERM:
				return termVulkan() && stopGLFW();

			case DEVICEFINALIZE:
				return finalizeVulkan();
			case DEVICEGETNUMSUBSYSTEMS:
				return vkGlobals.numMonitors;
			case DEVICEGETCURRENTSUBSYSTEM:
				return vkGlobals.currentMonitor;
			case DEVICESETSUBSYSTEM:
				monitors = glfwGetMonitors(&vkGlobals.numMonitors);
				if (n >= vkGlobals.numMonitors)
					return 0;
				vkGlobals.currentMonitor = n;
				vkGlobals.monitor = monitors[vkGlobals.currentMonitor];
				return 1;

			case DEVICEGETSUBSSYSTEMINFO:
				monitors = glfwGetMonitors(&vkGlobals.numMonitors);
				if (n >= vkGlobals.numMonitors)
					return 0;
				strncpy(((SubSystemInfo*)arg)->name, glfwGetMonitorName(monitors[n]), sizeof(SubSystemInfo::name));
				return 1;
			case DEVICEGETNUMVIDEOMODES:
				return vkGlobals.numModes;
			case DEVICEGETCURRENTVIDEOMODE:
				return vkGlobals.currentMode;

			case DEVICESETVIDEOMODE:
				if (n >= vkGlobals.numModes)
					return 0;
				vkGlobals.currentMode = n;
				return 1;
			case DEVICEGETVIDEOMODEINFO:
				rwmode = (VideoMode*)arg;
				rwmode->width = vkGlobals.modes[n].mode.width;
				rwmode->height = vkGlobals.modes[n].mode.height;
				rwmode->depth = vkGlobals.modes[n].depth;
				rwmode->flags = vkGlobals.modes[n].flags;
				return 1;
			case DEVICEGETMAXMULTISAMPLINGLEVELS:
			{
				return maple::GraphicsContext::get()->getCaps().maxSamples;
			}
			case DEVICEGETMULTISAMPLINGLEVELS:
				if (vkGlobals.numSamples == 0)
					return 1;
				return vkGlobals.numSamples;
			case DEVICESETMULTISAMPLINGLEVELS:
				vkGlobals.numSamples = (uint32)n;
				return 1;
			default:
				assert(0 && "not implemented");
				return 0;
			}
			return 1;
		}

		Device renderdevice = {
			-1.0f, 1.0f,
			vulkan::beginUpdate,
			vulkan::endUpdate,
			vulkan::clearCamera,
			vulkan::showRaster,
			vulkan::rasterRenderFast,
			vulkan::setRenderState,
			vulkan::getRenderState,
			vulkan::im2DRenderLine,
			vulkan::im2DRenderTriangle,
			vulkan::im2DRenderPrimitive,
			vulkan::im2DRenderIndexedPrimitive,
			vulkan::im3DTransform,
			vulkan::im3DRenderPrimitive,
			vulkan::im3DRenderIndexedPrimitive,
			vulkan::im3DEnd,
			vulkan::deviceSystemGLFW };
	}        // namespace vulkan
}        // namespace rw
#endif
