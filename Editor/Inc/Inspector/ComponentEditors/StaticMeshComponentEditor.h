#ifndef INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"
#include "Framework/Core/Types.h"
#include "Render/CoreType.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class Shader;
    }
    namespace UI
    {
        class Button;
        class Dropdown;
        class InputBlock;
        class ObjectAssetDropdown;
        class Slider;
    }
    namespace Editor
    {
        struct MaterialPropertyBinding
        {
            Render::Material *_material = nullptr;
            Render::ShaderPropertyId _property_id = Render::kInvalidShaderPropertyId;
            UI::InputBlock *_input = nullptr;
            UI::Slider *_slider = nullptr;
            UI::Button *_color_button = nullptr;
            UI::Dropdown *_dropdown = nullptr;
            UI::ObjectAssetDropdown *_texture_dropdown = nullptr;
            u32 _subscription = 0u;
        };

        class StaticMeshComponentEditor final : public IComponentEditor
        {
        public:
            ~StaticMeshComponentEditor() override;
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
            bool NeedsRebuild(const ComponentEditorContext &context) const override;

        private:
            void ClearMaterialBindings();

            Render::Material *_cached_material = nullptr;
            Render::Shader *_cached_shader = nullptr;
            Vector<MaterialPropertyBinding> _material_bindings;
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
