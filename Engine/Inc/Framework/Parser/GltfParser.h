#pragma once
#ifndef __GLTF_PARSER_H__
#define __GLTF_PARSER_H__

#include "Framework/Interface/IParser.h"
#include <mutex>

namespace Ailu
{
    using Render::Mesh;

    class GltfParser : public IMeshParser
    {
    public:
        GltfParser() = default;
        ~GltfParser() override = default;

        void Parser(const WString &sys_path, const MeshImportSetting &import_setting) final;
        const List<Ref<AnimationClip>> &GetAnimationClips() const final { return _loaded_anims; }
        void GetMeshes(List<Ref<Mesh>> &out_mesh) final
        {
            out_mesh = std::move(_loaded_meshes);
        }

        static Vector<String> CollectExternalDependencyUris(const WString &sys_path);

    private:
        void ParserImpl(const WString &sys_path);
        bool CalculateTangant(Mesh *mesh);
        bool CombineLoadedMeshes();

    private:
        std::mutex _parser_lock;
        MeshImportSetting _import_setting;
        WString _cur_file_sys_path;
        List<Ref<AnimationClip>> _loaded_anims;
        List<Ref<Mesh>> _loaded_meshes;
    };
}

#endif// !__GLTF_PARSER_H__