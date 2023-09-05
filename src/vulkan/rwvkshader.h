#ifdef RW_VULKAN

#include <string>
#include <vector>
#include <unordered_set>

namespace maple 
{
	class Shader;
}

namespace rw
{
	namespace vulkan
	{
		// TODO: make this dynamic
		enum
		{
			MAX_UNIFORMS = 40,
			MAX_BLOCKS = 20
		};

		enum UniformType
		{
			UNIFORM_NA,        // managed by the user
			UNIFORM_VEC4,
			UNIFORM_IVEC4,
			UNIFORM_MAT4
		};

		struct Uniform
		{
			char* name;
			UniformType type;
			//bool dirty;
			uint32 serialNum;
			int32  num;
			void* data;
		};

		struct UniformRegistry
		{
			int32   numUniforms;
			Uniform uniforms[MAX_UNIFORMS];

			int32 numBlocks;
			char* blockNames[MAX_BLOCKS];
		};

		int32 registerUniform(const char* name, UniformType type = UNIFORM_NA, int32 num = 1);
		int32 findUniform(const char* name);
		int32 registerBlock(const char* name);
		int32 findBlock(const char* name);

		void setUniform(int32 id, void* data);
		void flushUniforms(void);

		std::shared_ptr<maple::Shader> getShader(int32_t shader);

		extern UniformRegistry uniformRegistry;

		struct Shader
		{
			int32_t shaderId;

			static Shader* create(const std::string & vert,
				const char* userDefine,
				const std::string& frag,
				const char* fragUserDefine, const std::unordered_set<std::string>& dynamics = {});

			void use(void);
			void destroy(void);
		};

		extern Shader* currentShader;
	}        // namespace vulkan
}        // namespace rw

#endif
