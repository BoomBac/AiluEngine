#include "Framework/Parser/TextParser.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Utils.h"

#include "pch.h"
#include <Ext/rapidjson/inc/document.h>
#include <Ext/rapidjson/inc/filereadstream.h>
#include <Ext/rapidjson/inc/filewritestream.h>
#include <Ext/rapidjson/inc/prettywriter.h>
#include <Ext/rapidjson/inc/stringbuffer.h>
#include <cstdio>

namespace Ailu
{
#pragma region INIParser
    bool INIParser::Load(const WString &sys_path)
    {
        std::stringstream ss;
        if (FileManager::ReadFile(sys_path, ss))
        {
            String line, current_section;
            while (std::getline(ss, line))
            {
                line = su::Trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;

                if (line.front() == '[' && line.back() == ']')
                {
                    current_section = line.substr(1, line.size() - 2);
                }
                else if (!current_section.empty() && line.find('=') != String::npos)
                {
                    size_t pos = line.find('=');
                    String key = su::Trim(line.substr(0, pos));
                    String value = su::Trim(line.substr(pos + 1));
                    _data[current_section][key] = value;
                }
            }
            return true;
        }
        return false;
    }

    bool INIParser::Save(const WString &sys_path)
    {
        std::stringstream ss;
        for (const auto &section: _data)
        {
            ss << "[" << section.first << "]\n";
            for (const auto &pair: section.second)
            {
                ss << pair.first << " = " << pair.second << "\n";
            }
        }
        return FileManager::WriteFile(sys_path, false, ss.str());
    }

    String INIParser::GetValue(const String &section, const String &key, const String &default_value = "") const
    {
        auto sec_it = _data.find(section);
        if (sec_it != _data.end())
        {
            auto key_it = sec_it->second.find(key);
            if (key_it != sec_it->second.end())
            {
                return key_it->second;
            }
        }
        return default_value;
    }

    Map<String, String> INIParser::GetValues(const String &section) const
    {
        if (_data.contains(section))
            return _data.at(section);
        return {};
    }

    Map<String, String> INIParser::GetValues() const
    {
        Map<String, String> ret;
        for (auto &[section, map]: _data)
        {
            for (auto &[key, value]: map)
            {
                ret[key] = value;
            }
        }
        return ret;
    }

    void INIParser::SetValue(const String &section, const String &key, const String &value)
    {
        _data[section][key] = value;
    }
#pragma endregion

#pragma region JSONParser
    // 工具函数：拆分路径 "a.b.c" -> {"a", "b", "c"}
    static Vector<String> SplitPath(const String &path)
    {
        Vector<String> result;
        size_t start = 0, end;
        while ((end = path.find('.', start)) != String::npos)
        {
            result.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        result.push_back(path.substr(start));
        return result;
    }

    // ========== Impl ==========
    class JSONParser::Impl
    {
    public:
        rapidjson::Document doc;

        Impl() { doc.SetObject(); }

        bool Load(const WString &path)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"rb") != 0)
            {
                LOG_ERROR(L"JSONParser::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;

            char *buffer = new char[65536];
            rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));
            doc.ParseStream(is);
            fclose(fp);
            delete[] buffer;
            return !doc.HasParseError();
        }

        bool Save(const WString &path)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"wb"))
            {
                LOG_ERROR(L"JSONParser::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;

            char *buffer = new char[65536];
            rapidjson::FileWriteStream os(fp, buffer, sizeof(buffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
            delete[] buffer;
            return true;
        }

        const rapidjson::Value *GetNode(const String &path) const
        {
            const rapidjson::Value *node = &doc;

            for (const auto &key: SplitPath(path))
            {
                if (node->IsObject())
                {
                    if (!node->HasMember(key.c_str()))
                        return nullptr;
                    node = &(*node)[key.c_str()];
                }
                else if (node->IsArray())
                {
                    // 检查key是否是数字（数组下标）
                    char *end = nullptr;
                    u32 index = (u32) strtol(key.c_str(), &end, 10);
                    if (*end != '\0')// 不是纯数字
                        return nullptr;
                    if (index < 0 || index >= (u32)node->Size())
                        return nullptr;
                    node = &(*node)[static_cast<rapidjson::SizeType>(index)];
                }
                else
                {
                    return nullptr;
                }
            }

            return node;
        }


        rapidjson::Value *EnsureNode(const String &path)
        {
            rapidjson::Value *node = &doc;
            for (const auto &key: SplitPath(path))
            {
                if (!node->IsObject()) node->SetObject();
                if (!node->HasMember(key.c_str()))
                {
                    rapidjson::Value k(key.c_str(), doc.GetAllocator());
                    rapidjson::Value v;
                    v.SetObject();// 默认空对象
                    node->AddMember(k, v, doc.GetAllocator());
                }
                node = &(*node)[key.c_str()];
            }
            return node;
        }

        // 读取
        String GetString(const String &path, const String &def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsString()) ? node->GetString() : def;
        }

        i32 GetInt(const String &path, i32 def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsInt()) ? node->GetInt() : def;
        }

        bool GetBool(const String &path, bool def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsBool()) ? node->GetBool() : def;
        }

        f64 GetFloat(const String &path, f64 def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsNumber()) ? node->GetDouble() : def;
        }

        // 写入
        void SetString(const String &path, const String &val)
        {
            auto *node = EnsureNode(path);
            node->SetString(val.c_str(), doc.GetAllocator());
        }

        void SetInt(const String &path, i32 val)
        {
            auto *node = EnsureNode(path);
            node->SetInt(val);
        }

        void SetBool(const String &path, bool val)
        {
            auto *node = EnsureNode(path);
            node->SetBool(val);
        }

        void SetFloat(const String &path, f64 val)
        {
            auto *node = EnsureNode(path);
            node->SetDouble(val);
        }
    };

    // ========== JSONParser 实现 ==========

    JSONParser::JSONParser() : _impl(new Impl()) {}
    JSONParser::~JSONParser() { delete _impl; }

    bool JSONParser::Load(const WString &sys_path)
    {
        return _impl->Load(sys_path);
    }

    bool JSONParser::Save(const WString &sys_path)
    {
        return _impl->Save(sys_path);
    }

    String JSONParser::GetValue(const String &section, const String &key, const String &def) const
    {
        return _impl->GetString(section + "." + key, def);
    }

    Map<String, String> JSONParser::GetValues(const String &section) const
    {
        Map<String, String> result;
        const rapidjson::Value *node = _impl->GetNode(section);
        if (!node || !node->IsObject())
        {
            LOG_WARNING("JSONParser::GetValues section '{}' not found or not an object", section)
            return result;
        }

        for (auto it = node->MemberBegin(); it != node->MemberEnd(); ++it)
        {
            const auto &name = it->name.GetString();
            const auto &val = it->value;

            if (val.IsString())
                result[name] = val.GetString();
            else if (val.IsBool())
                result[name] = val.GetBool() ? "true" : "false";
            else if (val.IsInt())
                result[name] = std::to_string(val.GetInt());
            else if (val.IsDouble())
                result[name] = std::to_string(val.GetDouble());
            else if (val.IsNull())
                result[name] = "null";
            else
            {
                // 对于数组或对象，序列化为字符串
                rapidjson::StringBuffer sb;
                rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                val.Accept(writer);
                result[name] = sb.GetString();
            }
        }
        return result;
    }


    Map<String, String> JSONParser::GetValues() const
    {
        Map<String, String> result;
        const rapidjson::Value &root = _impl->doc;
        if (!root.IsObject()) return result;

        for (auto it = root.MemberBegin(); it != root.MemberEnd(); ++it)
        {
            const auto &name = it->name.GetString();
            const auto &val = it->value;

            if (val.IsString())
                result[name] = val.GetString();
            else if (val.IsBool())
                result[name] = val.GetBool() ? "true" : "false";
            else if (val.IsInt())
                result[name] = std::to_string(val.GetInt());
            else if (val.IsDouble())
                result[name] = std::to_string(val.GetDouble());
            else if (val.IsNull())
                result[name] = "null";
            else
            {
                // 复杂类型 → JSON字符串
                rapidjson::StringBuffer sb;
                rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                val.Accept(writer);
                result[name] = sb.GetString();
            }
        }
        return result;
    }


    void JSONParser::SetValue(const String &section, const String &key, const String &value)
    {
        _impl->SetString(section + "." + key, value);
    }

    u32 JSONParser::GetArraySize(const String &json_path) const
    {
        auto node = _impl->GetNode(json_path);
        if (!node || !node->IsArray())
            return 0;
        return static_cast<u32>(node->Size());
    }


    String JSONParser::GetString(const String &path, const String &def) const
    {
        return _impl->GetString(path, def);
    }

    i32 JSONParser::GetInt(const String &path, i32 def) const
    {
        return _impl->GetInt(path, def);
    }

    bool JSONParser::GetBool(const String &path, bool def) const
    {
        return _impl->GetBool(path, def);
    }

    f64 JSONParser::GetFloat(const String &path, f64 def) const
    {
        return _impl->GetFloat(path, def);
    }

    void JSONParser::SetValue(const String &path, const String &val)
    {
        _impl->SetString(path, val);
    }

    void JSONParser::SetValue(const String &path, i32 val)
    {
        _impl->SetInt(path, val);
    }

    void JSONParser::SetValue(const String &path, bool val)
    {
        _impl->SetBool(path, val);
    }

    void JSONParser::SetValue(const String &path, f64 val)
    {
        _impl->SetFloat(path, val);
    }
#pragma endregion
}// namespace Ailu