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
		std::vector<maple::Shader::Ptr> shaders;
	
		std::shared_ptr<maple::Shader> getShader(int32_t shader)
		{
			return shaders[shader];
		}

		Shader* currentShader;

		Shader* Shader::create(const std::string& vert,
			const char* userDefine,
			const std::string& frag,
			const char* fragUserDefine, const std::unordered_set<std::string>& dynamics)
		{
			Shader* sh = rwNewT(Shader, 1, MEMDUR_EVENT | ID_DRIVER);        // or global?
			std::vector<uint32_t> vertexShader;
			std::vector<uint32_t> fragShader;
			maple::ShaderCompiler::complie(maple::ShaderType::Vertex, vert.c_str(), vertexShader, userDefine);
			maple::ShaderCompiler::complie(maple::ShaderType::Fragment, frag.c_str(), fragShader, fragUserDefine);
			auto shaderPtr = maple::Shader::create(vertexShader, fragShader, dynamics);
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
			rwFree(this);
		}
	}        // namespace vulkan
}        // namespace rw

#endif
