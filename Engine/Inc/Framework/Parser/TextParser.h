#ifndef __TEXT_PARSER_H__
#define __TEXT_PARSER_H__
#include "GlobalMarco.h"
#include "Objects/Type.h"

#pragma warning(disable : 4251)//std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告

namespace Ailu
{
    class AILU_API ITextParser 
    {
    public:
        virtual ~ITextParser() = default;
        virtual bool Load(const WString& sys_path) = 0;
        virtual bool Save(const WString& sys_path) = 0;
        virtual String GetValue(const String& section, const String& key, const String& default_value) const = 0;
        virtual Map<String,String> GetValues(const String& section) const = 0;
        virtual Map<String,String> GetValues() const = 0;
        virtual void SetValue(const String& section, const String& key, const String& value) = 0;
    };

    class AILU_API INIParser : public ITextParser
    {
    public:
        using INIMap = Map<String, Map<String, String>>;
        bool Load(const WString& sys_path) final;
        bool Save(const WString& sys_path) final;
        String GetValue(const String& section, const String& key, const String& default_value) const final;
        Map<String,String> GetValues(const String& section) const final;
        Map<String, String> GetValues() const final;
        void SetValue(const String& section, const String& key, const String& value);
    private:
        INIMap _data;
    };

    class AILU_API JSONParser : public ITextParser
    {
    public:
        JSONParser();
        ~JSONParser();

        bool Load(const WString &sys_path) final;
        bool Save(const WString &sys_path) final;
        String GetValue(const String &section, const String &key, const String &default_value) const final;
        Map<String, String> GetValues(const String &section) const final;
        Map<String, String> GetValues() const final;
        void SetValue(const String &section, const String &key, const String &value) final;

        String GetString(const String &json_path, const String &default_value = "") const;
        i32 GetInt(const String &json_path, i32 default_value = 0) const;
        bool GetBool(const String &json_path, bool default_value = false) const;
        f64 GetFloat(const String &json_path, f64 default_value = 0.0) const;

        void SetValue(const String &json_path, const String &value);
        void SetValue(const String &json_path, i32 value);
        void SetValue(const String &json_path, bool value);
        void SetValue(const String &json_path, f64 value);

    private:
        class Impl;
        Impl *_impl;
    };
}

#endif
