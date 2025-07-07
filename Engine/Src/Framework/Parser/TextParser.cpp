#include "Framework/Parser/TextParser.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/FileManager.h"
#include "pch.h"

namespace Ailu
{
    bool INIParser::Load(const WString &sys_path)
    {
        std::stringstream ss;
        if (FileManager::ReadFile(sys_path,ss))
        {
            String line,current_section;
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
        return FileManager::WriteFile(sys_path,false,ss.str());
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
    Map<String,String> INIParser::GetValues(const String& section) const
    {
        if(_data.contains(section))
            return _data.at(section);
        return {};
    }

    Map<String, String> INIParser::GetValues() const
    {
        Map<String, String> ret;
        for (auto& [section, map] : _data)
        {
            for (auto& [key, value] : map)
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
}// namespace Ailu