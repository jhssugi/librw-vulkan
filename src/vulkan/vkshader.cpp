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

#ifdef RW_VULKAN
#	include "rwvk.h"
#	include "rwvkshader.h"
#	include "ShaderCompiler.h"
#	include "Shader.h"

namespace rw
{
	namespace vulkan
	{
#	include "shaders/header_fs.inc"
#	include "shaders/header_vs.inc"

		std::vector<maple::Shader::Ptr> shaders;

		UniformRegistry uniformRegistry;
		static char     nameBuffer[(MAX_UNIFORMS + MAX_BLOCKS) * 32];        // static because memory system isn't up yet when we register
		static uint32   nameBufPtr;
		static float    uniformData[512 * 4];        // seems enough
		static uint32   dataPtr;

		static int uniformTypesize[] = {
			0, 4, 4, 16 };

		static char*
			shader_strdup(const char* name)
		{
			size_t len = strlen(name) + 1;
			char* s = &nameBuffer[nameBufPtr];
			nameBufPtr += len;
			assert(nameBufPtr <= nelem(nameBuffer));
			memcpy(s, name, len);
			return s;
		}

		std::shared_ptr<maple::Shader> getShader(int32_t shader)
		{
			return shaders[shader];
		}

		int32
			registerUniform(const char* name, UniformType type, int32 num)
		{
			int i;
			i = findUniform(name);
			if (type == UNIFORM_NA)
				num = 0;
			if (i >= 0)
			{
				Uniform* u = &uniformRegistry.uniforms[i];
				assert(u->type == type);
				assert(u->num == num);
				return i;
			}
			// TODO: print error
			if (uniformRegistry.numUniforms + 1 >= MAX_UNIFORMS)
			{
				assert(0 && "no space for uniform");
				return -1;
			}
			Uniform* u = &uniformRegistry.uniforms[uniformRegistry.numUniforms];
			u->name = shader_strdup(name);
			u->type = type;
			u->serialNum = 0;
			if (type == UNIFORM_NA)
			{
				u->num = 0;
				u->data = nil;
			}
			else
			{
				u->num = num;
				u->data = &uniformData[dataPtr];
				dataPtr += uniformTypesize[type] * num;
				assert(dataPtr <= nelem(uniformData));
			}

			return uniformRegistry.numUniforms++;
		}

		int32
			findUniform(const char* name)
		{
			int i;
			for (i = 0; i < uniformRegistry.numUniforms; i++)
				if (strcmp(name, uniformRegistry.uniforms[i].name) == 0)
					return i;
			return -1;
		}

		int32
			registerBlock(const char* name)
		{
			int i;
			i = findBlock(name);
			if (i >= 0)
				return i;
			// TODO: print error
			if (uniformRegistry.numBlocks + 1 >= MAX_BLOCKS)
				return -1;
			uniformRegistry.blockNames[uniformRegistry.numBlocks] = shader_strdup(name);
			return uniformRegistry.numBlocks++;
		}

		int32 findBlock(const char* name)
		{
			int i;
			for (i = 0; i < uniformRegistry.numBlocks; i++)
				if (strcmp(name, uniformRegistry.blockNames[i]) == 0)
					return i;
			return -1;
		}

		void setUniform(int32 id, void* data)
		{

		}

		void flushUniforms(void)
		{

		}

		Shader* currentShader;

		Shader* Shader::create(const std::string& vert,
			const char* userDefine,
			const std::string& frag,
			const char* fragUserDefine)
		{
			Shader* sh = rwNewT(Shader, 1, MEMDUR_EVENT | ID_DRIVER);        // or global?
			std::vector<uint32_t> vertexShader;
			std::vector<uint32_t> fragShader;
			maple::ShaderCompiler::complie(maple::ShaderType::Vertex, vert.c_str(), vertexShader, userDefine);
			maple::ShaderCompiler::complie(maple::ShaderType::Fragment, frag.c_str(), fragShader, fragUserDefine);
			auto shaderPtr = maple::Shader::create(vertexShader, fragShader);
			sh->shaderId = shaders.size();
			shaders.push_back(shaderPtr);
			return sh;
		}

		void Shader::use(void)
		{
			if (currentShader != this)
			{
				currentShader = this;
			}
		}

		void Shader::destroy(void)
		{
			rwFree(this->uniformLocations);
			rwFree(this->serialNums);
			rwFree(this);
		}
	}        // namespace vulkan
}        // namespace rw

#endif
