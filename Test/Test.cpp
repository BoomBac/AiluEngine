#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <string>
#include <list>
#include <set>

using namespace std;
namespace fs = std::filesystem;

using String = std::string;

enum class EBindResDescType : uint8_t
{
	kConstBuffer = 0, kTexture2D, kCubeMap, kTexture2DArray, kSampler, kCBufferAttribute
};

struct ShaderPropertyType
{
	inline static std::string Vector = "Vector";
	inline static std::string Float = "Float";
	inline static std::string Color = "Color";
	inline static std::string Texture2D = "Texture2D";
	inline static std::string CubeMap = "CubeMap";
};

struct ShaderBindResourceInfo
{
	inline const static std::set<std::string> s_reversed_res_name
	{
		"SceneObjectBuffer",
		"_MatrixWorld",
		"SceneMaterialBuffer",
		"SceneStatetBuffer",
		"_MatrixV",
		"_MatrixP",
		"_MatrixVP",
		"_CameraPos",
		"_DirectionalLights",
		"_PointLights",
		"_SpotLights",
		"padding",
		"padding0"
	};
	inline static uint8_t GetVariableSize(const ShaderBindResourceInfo& info) { return info._cbuf_member_offset & 0XFF; }
	inline static uint8_t GetVariableOffset(const ShaderBindResourceInfo& info) { return info._cbuf_member_offset >> 8; }
	EBindResDescType _res_type;
	union
	{
		uint16_t _res_slot;
		uint16_t _cbuf_member_offset;
	};
	uint8_t _bind_slot;
	std::string _name;
	ShaderBindResourceInfo() = default;
	ShaderBindResourceInfo(EBindResDescType res_type, uint16_t slot_or_offset, uint8_t bind_slot, const std::string& name)
		: _res_type(res_type), _bind_slot(bind_slot), _name(name)
	{
		if (res_type == EBindResDescType::kCBufferAttribute) _cbuf_member_offset = slot_or_offset;
		else _res_slot = slot_or_offset;
	}
	bool operator==(const ShaderBindResourceInfo& other) const
	{
		return _name == other._name;
	}
};

static bool BeginWith(const String& s,const String& prefix)
{
    if (s.length() < prefix.length()) return false;
    return s.substr(0, prefix.length()) == prefix;
}

//[begin,end]
static inline String SubStrRange(const String& s,const size_t& begin, const size_t& end)
{
	if (end < begin) return s;
	return s.substr(begin, end - begin + 1);
}


void LoadAdditionalShaderRelfection(const std::string& sys_path)
{
    ifstream src(sys_path,ios::in);
    string line;
    vector<string> lines;
    list<string> header_files;
    while (getline(src,line))
    {
		if (BeginWith(line, "#include"))
		{
			int path_start = line.find_first_of("\"");
			int path_end = line.find_last_of("\"");
            header_files.emplace_back(SubStrRange(line, path_start + 1, path_end - 1));
			cout << header_files.back() << endl;
		}
		else if (BeginWith(line, "Texture2D"))
		{
			int name_begin = line.find_first_of("D") + 1;
			int name_end = line.find_first_of(":") -1;
			String tex_name = line.substr(name_begin,name_end - name_begin);
			cout << tex_name << endl;
		}
		else if (BeginWith(line, "TextureCube"))
		{
			int name_begin = line.find_last_of("e") + 1;
			int name_end = line.find_first_of(":") - 1;
			String tex_name = line.substr(name_begin, name_end - name_begin);
			cout << tex_name << endl;
		}
        lines.emplace_back(line);
    }
	src.close();
	fs::path src_path(sys_path);
	fs::path pwd = src_path.parent_path();
	for (auto& head_file : header_files)
	{
		fs::path temp = pwd;
		temp.append(head_file);
		cout << temp.string() << endl;
		LoadAdditionalShaderRelfection(temp.string());
	}
}

int main()
{
	LoadAdditionalShaderRelfection("C:/AiluEngine/Engine/Res/Shaders/shaders.hlsl");
}
