#pragma once
#ifndef __EDITOR_DEBUG_FEATURE_H__
#define __EDITOR_DEBUG_FEATURE_H__

#include "Assets/AssetRef.h"
#include "Render/Features/RenderFeature.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class RenderTexture;
        class Shader;

        class EditorDebugPass final : public RenderPass
        {
        public:
            EditorDebugPass();

            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;

        private:
            void DrawLightProbeGroups(const RenderingData &rendering_data);
            void DrawDDGIVolumes(const RenderingData &rendering_data);

        private:
            AssetRef<Shader> _light_probe_preview_shader;
            Ref<Material> _light_probe_preview_empty_material;
            Ref<Material> _ddgi_probe_preview_material;
            Vector<Ref<Material>> _light_probe_preview_materials;
            Vector<Ref<Material>> _light_probe_cubemap_preview_materials;
        };

        class EditorDebugFeature final : public RenderFeature
        {
        public:
            EditorDebugFeature();

            void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;

        private:
            EditorDebugPass _debug_pass;
        };
    }
}

#endif// !__EDITOR_DEBUG_FEATURE_H__
