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
		std::shared_ptr<maple::Shader> getShader(int32_t shader);

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
