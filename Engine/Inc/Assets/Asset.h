#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "Framework/Math/Guid.h"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Objects/Type.h"
#include "AssetCommon.h"
//#include "generated/Asset.gen.h"

namespace Ailu
{
    class AILU_API Asset : public Object
    {
    public:
        Asset() = default;
        Asset(const Type *type, const WString &asset_path);
        Asset(Guid guid, const Type *type, const WString &asset_path);
        Asset(const Asset &other) = delete;
        Asset &operator=(const Asset &other) = delete;
        void CopyFrom(const Asset &other);
        //Asset(Asset&& other);
        //Asset& operator=(Asset&& other);
        virtual ~Asset();
        bool operator<(const Asset &other) const { return _p_obj->ID() < _p_obj->ID(); }

        void AssignGuid(const Guid &guid);
        const Guid &GetGuid() const { return _guid; };
        template<typename T>
        T *As() const
        {
            return dynamic_cast<T *>(_p_obj.get());
        }
        template<typename T>
        Ref<T> AsRef()
        {
            return std::dynamic_pointer_cast<T>(_p_obj);
        }

    public:
        EAssetDomain _domain = EAssetDomain::kProject;
        WString _asset_path;
        //使用额外的信息来定位资源对象，对于对于fbx文件，使用资源路径和文件内对象的名称来确定一个mesh，对于shader，目前使用vs/ps的入口。
        WString _addi_info;
        WString _external_asset_path;
        const Type *_asset_type = nullptr;
        Ref<Object> _p_obj;
        struct RuntimeDependency
        {
            Guid _guid = Guid::EmptyGuid();
            EAssetDependencyType _type = EAssetDependencyType::kHard;
            Asset *_asset = nullptr;
        };

        Vector<RuntimeDependency> _dependencies;
    private:
        Guid _guid;
    };
}// namespace Ailu

#endif// !ASSET_H__
