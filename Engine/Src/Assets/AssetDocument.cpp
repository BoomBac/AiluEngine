#include "Assets/AssetDocument.h"

#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"

#include <Ext/rapidjson/inc/filereadstream.h>
#include <Ext/rapidjson/inc/reader.h>

namespace Ailu
{
    namespace
    {
        class AssetDocumentHeaderHandler final : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, AssetDocumentHeaderHandler>
        {
        public:
            explicit AssetDocumentHeaderHandler(AssetDocumentHeader &header) : _header(header) {}

            bool StartObject()
            {
                ++_object_depth;
                if (_object_depth == 2u && _key == "_header")
                    _in_header = true;
                else if (_in_dependencies && _array_depth == 1u && _object_depth == 3u)
                {
                    _in_dependency = true;
                    _dependency = {};
                }
                return true;
            }

            bool EndObject(rapidjson::SizeType = 0u)
            {
                if (_in_dependency && _object_depth == 3u)
                {
                    if (!_dependency._guid.IsEmpty())
                        _header._dependencies.emplace_back(std::move(_dependency));
                    _in_dependency = false;
                }
                if (_in_header && _object_depth == 2u)
                {
                    _finished = true;
                    return false;
                }
                --_object_depth;
                return true;
            }

            bool StartArray()
            {
                ++_array_depth;
                if (_in_header && !_in_dependency && _key == "_dependencies")
                    _in_dependencies = true;
                return true;
            }

            bool EndArray(rapidjson::SizeType = 0u)
            {
                if (_in_dependencies && _array_depth == 1u)
                    _in_dependencies = false;
                --_array_depth;
                return true;
            }

            bool Key(const char *key, rapidjson::SizeType length, bool)
            {
                _key.assign(key, length);
                return true;
            }

            bool String(const char *value, rapidjson::SizeType length, bool)
            {
                const Ailu::String text(value, length);
                if (_in_dependency)
                {
                    if (_key == "_guid")
                        _dependency._guid = Guid(text);
                    else if (_key == "_type")
                        _dependency._type = ParseDependencyType(text);
                }
                else if (_in_header)
                {
                    if (_key == "_guid")
                        _header._guid = text;
                    else if (_key == "_asset_type")
                        _header._asset_type = text;
                    else if (_key == "_asset_name")
                        _header._asset_name = text;
                }
                return true;
            }

            bool Uint(unsigned value) { return SetFormatVersion(value); }
            bool Uint64(uint64_t value) { return SetFormatVersion(static_cast<unsigned>(value)); }
            bool Int(int value) { return SetFormatVersion(static_cast<unsigned>(value)); }
            bool Int64(int64_t value) { return SetFormatVersion(static_cast<unsigned>(value)); }
            bool Double(double) { return true; }
            bool Bool(bool) { return true; }
            bool Null() { return true; }

            bool Finished() const { return _finished; }

        private:
            static EAssetDependencyType ParseDependencyType(const Ailu::String &name)
            {
                if (name == "kSoft")
                    return EAssetDependencyType::kSoft;
                if (name == "kEditorOnly")
                    return EAssetDependencyType::kEditorOnly;
                return EAssetDependencyType::kHard;
            }

            bool SetFormatVersion(unsigned value)
            {
                if (_in_header && !_in_dependency && _key == "_format_version")
                    _header._format_version = value;
                return true;
            }

            AssetDocumentHeader &_header;
            AssetDependency _dependency;
            Ailu::String _key;
            u32 _object_depth = 0u;
            u32 _array_depth = 0u;
            bool _in_header = false;
            bool _in_dependencies = false;
            bool _in_dependency = false;
            bool _finished = false;
        };
    }

    bool LoadAssetDocumentHeader(const WString &sys_path, AssetDocumentHeader &header)
    {
        FILE *file = nullptr;
        if (_wfopen_s(&file, sys_path.c_str(), L"rb") != 0 || file == nullptr)
        {
            LOG_ERROR(L"Load asset document header failed: cannot open file {}", sys_path);
            return false;
        }

        char *buffer = AL_ALLOC_TAG(EMemoryTag::kTemporary, char, 65536);
        rapidjson::FileReadStream stream(file, buffer, 65536);
        AssetDocumentHeader parsed_header;
        AssetDocumentHeaderHandler handler(parsed_header);
        rapidjson::Reader reader;
        reader.Parse(stream, handler);
        fclose(file);
        AL_FREE(buffer);

        if (!handler.Finished() || parsed_header._guid.empty() || parsed_header._asset_type.empty())
            return false;
        if (parsed_header._format_version != kSerializedAssetDocumentVersion)
        {
            LOG_ERROR(L"Unsupported asset document version {} in {}", parsed_header._format_version, sys_path);
            return false;
        }
        header = std::move(parsed_header);
        return true;
    }
}
