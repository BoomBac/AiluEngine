#ifndef __DRAGDROP_H__
#define __DRAGDROP_H__
#include "GlobalMarco.h"
#include <functional>
#include "generated/DragDrop.gen.h"

namespace Ailu
{
    namespace Render
    {
        class Texture;
    }
    namespace UI
    {
        AENUM()
        enum class EDragType : u32
        {
            kNone = 0,
            kMesh,
            kTexture,
            kMaterial,
            kPrefab,
            kScene,
            kEntity,
            kAudio,
            kScript,
            kShader,
            kFile,  // 通用文件
            kFolder,// 文件夹
            kUIWidget,
            kAnimation,
            kPhysicsAsset,
            kBlueprint,
            kCustomUserData,// 可扩展类型（调试等）
        };


        struct AILU_API DragPayload
        {
            EDragType _type;
            void* _data = nullptr;
        };

        using DropCallback = std::function<void(const DragPayload &, f32 x, f32 y)>;
        using CanDropCallback = std::function<bool(const DragPayload &)>;

        struct AILU_API DropHandler
        {
            CanDropCallback _can_drop;
            DropCallback _on_drop;
        };

        class AILU_API DragTypeRegistry
        {
        public:
            static EDragType Register(const String &name)
            {
                auto hash = std::hash<String>{}(name);
                if (auto it = _map.find(hash); it != _map.end())
                    return it->second;
                EDragType type = (EDragType) (s_next++);
                _map[hash] = type;
                return type;
            }

        private:
            static inline HashMap<u64, EDragType> _map;
            static inline u32 s_next = (u32) EDragType::kCustomUserData + 1;
        };


        class AILU_API DragDropManager
        {
        public:
            static DragDropManager &Get();
            DragDropManager();
            void BeginDrag(const DragPayload &payload, String display_name, Render::Texture *preview_tex = nullptr);
            void EndDrag();
            void Update();
            bool IsDragging(EDragType type) const { return _payload.has_value()? _payload->_type == type : false; }
            const std::optional<DragPayload> &GetPayload() const { return _payload; }
        private:
            std::optional<DragPayload> _payload;
            String _display_name;
            Render::Texture *_preview_tex = nullptr;
            DropHandler *_hover_target = nullptr;
            f32 mx = 0.0f, my = 0.0f;
        };

    }// namespace UI
}// namespace Ailu
#endif// !__DRAGDROP_H__
