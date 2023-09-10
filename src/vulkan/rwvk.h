
#pragma once

#ifdef RW_VULKAN


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <memory>

namespace maple
{
	enum class TextureFormat : int32_t;
	enum class DrawType : int32_t;
	class Texture;
	class VertexBuffer;
	class IndexBuffer;
	class DescriptorSet;
	class Shader;
	class Pipeline;
}

namespace rw
{

	struct EngineOpenParams
	{
		GLFWwindow** window;
		int          width, height;
		const char* windowtitle;
	};

	namespace vulkan
	{
		void registerPlatformPlugins(void);

		extern Device renderdevice;

		// arguments to glVertexAttribPointer basically
		struct AttribDesc
		{
			uint32 index;
			int32  type;
			int32  size;
			uint32 stride;
			uint32 offset;
		};

		enum AttribIndices
		{
			ATTRIB_POS = 0,
			ATTRIB_NORMAL,
			ATTRIB_COLOR,
			ATTRIB_WEIGHTS,
			ATTRIB_INDICES,
			ATTRIB_TEXCOORDS0,
			ATTRIB_TEXCOORDS1,
			ATTRIB_TEXCOORDS2,
			ATTRIB_TEXCOORDS3,
			ATTRIB_TEXCOORDS4,
			ATTRIB_TEXCOORDS5,
			ATTRIB_TEXCOORDS6,
			ATTRIB_TEXCOORDS7,
		};

		// default uniform indices
		extern int32 u_matColor;
		extern int32 u_surfProps;

		struct InstanceData
		{
			uint32    numIndex;
			uint32    minVert;            // not used for rendering
			int32     numVertices;        //
			Material* material;
			bool32    vertexAlpha;
			uint32    program;
			uint32    offset;
		};

		struct InstanceDataHeader : rw::InstanceDataHeader
		{
			uint32      serialNumber;
			uint32      numMeshes;
			uint16* indexBuffer;
			maple::DrawType primType;
			uint8* vertexBuffer;
			int32       numAttribs;
			AttribDesc* attribDesc;
			uint32      totalNumIndex;
			uint32      totalNumVertex;

			maple::VertexBuffer* vertexBufferGPU;
			maple::IndexBuffer* indexBufferGPU;

			InstanceData* inst;
		};

		struct Shader;

		extern Shader* defaultShader, * defaultShader_noAT;
		extern Shader* defaultShader_fullLight, * defaultShader_fullLight_noAT;

		struct Im3DVertex
		{
			V3d     position;
			V3d     normal;        // librw extension
			float32   r, g, b, a;
			float32 u, v;

			void setX(float32 x)
			{
				this->position.x = x;
			}
			void setY(float32 y)
			{
				this->position.y = y;
			}
			void setZ(float32 z)
			{
				this->position.z = z;
			}
			void setNormalX(float32 x)
			{
				this->normal.x = x;
			}
			void setNormalY(float32 y)
			{
				this->normal.y = y;
			}
			void setNormalZ(float32 z)
			{
				this->normal.z = z;
			}
			void setColor(uint8 r, uint8 g, uint8 b, uint8 a)
			{
				this->r = r / 255.f;
				this->g = g / 255.f;
				this->b = b / 255.f;
				this->a = a / 255.f;
			}
			void setU(float32 u)
			{
				this->u = u;
			}
			void setV(float32 v)
			{
				this->v = v;
			}

			float getX(void)
			{
				return this->position.x;
			}
			float getY(void)
			{
				return this->position.y;
			}
			float getZ(void)
			{
				return this->position.z;
			}
			float getNormalX(void)
			{
				return this->normal.x;
			}
			float getNormalY(void)
			{
				return this->normal.y;
			}
			float getNormalZ(void)
			{
				return this->normal.z;
			}
			RGBA getColor(void)
			{
				return makeRGBA(this->r, this->g, this->b, this->a);
			}
			float getU(void)
			{
				return this->u;
			}
			float getV(void)
			{
				return this->v;
			}
		};
		extern RGBA              im3dMaterialColor;
		extern SurfaceProperties im3dSurfaceProps;

		struct Im2DVertex
		{
			float32 x, y, z, w;
			float32 r, g, b, a;
			float32 u, v;

			void setScreenX(float32 x)
			{
				this->x = x;
			}
			void setScreenY(float32 y)
			{
				this->y = y;
			}
			void setScreenZ(float32 z)
			{
				this->z = z;
			}
			// This is a bit unefficient but we have to counteract GL's divide, so multiply
			void setCameraZ(float32 z)
			{
				this->w = z;
			}
			void setRecipCameraZ(float32 recipz)
			{
				this->w = 1.0f / recipz;
			}
			void setColor(uint8 r, uint8 g, uint8 b, uint8 a)
			{
				this->r = r / 255.f;
				this->g = g / 255.f;
				this->b = b / 255.f;
				this->a = a / 255.f;
			}
			void setU(float32 u, float recipz)
			{
				this->u = u;
			}
			void setV(float32 v, float recipz)
			{
				this->v = v;
			}

			float getScreenX(void)
			{
				return this->x;
			}
			float getScreenY(void)
			{
				return this->y;
			}
			float getScreenZ(void)
			{
				return this->z;
			}
			float getCameraZ(void)
			{
				return this->w;
			}
			float getRecipCameraZ(void)
			{
				return 1.0f / this->w;
			}
			RGBA getColor(void)
			{
				return makeRGBA(this->r, this->g, this->b, this->a);
			}
			float getU(void)
			{
				return this->u;
			}
			float getV(void)
			{
				return this->v;
			}
		};

		// Render state

		// Vertex shader bits
		enum
		{
			// These should be low so they could be used as indices
			VSLIGHT_DIRECT = 1,
			VSLIGHT_POINT = 2,
			VSLIGHT_SPOT = 4,
			VSLIGHT_MASK = 7,        // all the above
			// less critical
			VSLIGHT_AMBIENT = 8,
		};

		extern const char* header_vert_src;
		extern const char* header_frag_src;

		extern Shader* im2dOverrideShader;

		// per Scene
		void setProjectionMatrix(float32*);
		void setViewMatrix(float32*);

		// per Object
		void  setWorldMatrix(Matrix*);
		int32 setLights(WorldLights* lightData);


		// per Mesh
		void setTexture(std::shared_ptr<maple::DescriptorSet> sets, int32 n, Texture* tex);
		void setMaterial(std::shared_ptr<maple::DescriptorSet> sets, const RGBA& color, const SurfaceProperties& surfaceprops, float extraSurfProp = 0.0f);
		std::shared_ptr<maple::Pipeline> getPipeline(maple::DrawType drawType);

		inline void setMaterial(const std::shared_ptr<maple::DescriptorSet>& sets,uint32 flags, const RGBA& color, const SurfaceProperties& surfaceprops, float extraSurfProp = 0.0f)
		{
			static RGBA white = { 255, 255, 255, 255 };
			if (flags & Geometry::MODULATE)
				setMaterial(sets, color, surfaceprops, extraSurfProp);
			else
				setMaterial(sets, white, surfaceprops, extraSurfProp);
		}

		std::shared_ptr<maple::Texture> getTexture(int32_t textureId);
		std::shared_ptr<maple::DescriptorSet> getMaterialDescriptorSet(Material* material);

		void   setAlphaBlend(bool32 enable);
		bool32 getAlphaBlend(void);

		bool32 getAlphaTest(void);

		void   bindFramebuffer(uint32 fbo);
		uint32 bindTexture(uint32 texid);

		void flushCache(std::shared_ptr<maple::Shader> shader, std::shared_ptr<maple::DescriptorSet> dest);

		void flushFog(std::shared_ptr<maple::DescriptorSet> dest);

		class ObjPipeline : public rw::ObjPipeline
		{
		public:
			void                init(void);
			static ObjPipeline* create(void);

			void (*instanceCB)(Geometry* geo, InstanceDataHeader* header, bool32 reinstance);
			void (*uninstanceCB)(Geometry* geo, InstanceDataHeader* header);
			void (*renderCB)(Atomic* atomic, InstanceDataHeader* header);
		};

		void  defaultInstanceCB(Geometry* geo, InstanceDataHeader* header, bool32 reinstance);
		void  defaultUninstanceCB(Geometry* geo, InstanceDataHeader* header);
		void  defaultRenderCB(Atomic* atomic, InstanceDataHeader* header);
		int32 lightingCB(Atomic* atomic);
		int32 lightingCB(void);

		void drawInst_simple(InstanceDataHeader* header, InstanceData* inst);
		// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
		void drawInst_GSemu(InstanceDataHeader* header, InstanceData* inst);
		// This one switches between the above two depending on render state;
		void drawInst(InstanceDataHeader* header, InstanceData* inst);

		void* destroyNativeData(void* object, int32, int32);

		ObjPipeline* makeDefaultPipeline(void);

		// Native Texture and Raster

		struct VulkanRaster
		{
			// arguments to glTexImage2D
			maple::TextureFormat internalFormat;
			int32 bpp;        // bytes per pixel
			// texture object
			bool isCompressed;//in theory, vulkan support compress. but now, we don't use them.
			bool hasAlpha;
			bool autogenMipmap;
			int8 numLevels;
			int32_t textureId = -1;

			//cache
			uint8 filterMode;
			uint8 addressU;
			uint8 addressV;
			int32 maxAnisotropy;

			RasterLevels *backingStore; // if we can't read back GPU memory but have to
		};

		struct VulkanCaps
		{
			int   gles;
			int   glversion;
			bool  dxtSupported;
			bool  astcSupported;        // not used yet
			float maxAnisotropy;
		};

		extern VulkanCaps gl3Caps;
		// GLES can't read back textures very nicely.
		// In most cases that's not an issue, but when it is,
		// this has to be set before the texture is filled:
		extern bool32 needToReadBackTextures;

		void allocateDXT(Raster* raster, int32 dxt, int32 numLevels, bool32 hasAlpha);

		Texture* readNativeTexture(Stream* stream);
		void     writeNativeTexture(Texture* tex, Stream* stream);
		uint32   getSizeNativeTexture(Texture* tex);

		extern int32 nativeRasterOffset;
		void         registerNativeRaster(void);

#	define GET_VULKAN_RASTEREXT(raster) PLUGINOFFSET(VulkanRaster, raster, rw::vulkan::nativeRasterOffset)

	}        // namespace vulkan
}        // namespace rw

#else

namespace rw
{
	namespace vulkan
	{
		void registerPlatformPlugins(void);

		Texture* readNativeTexture(Stream* stream);

		uint32   getSizeNativeTexture(Texture* tex);

		void     writeNativeTexture(Texture* tex, Stream* stream);
	}
}

#endif // RW_VULKAN
