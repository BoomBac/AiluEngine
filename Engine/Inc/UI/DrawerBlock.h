#ifndef __DRAWER_BLOCK_H__
#define __DRAWER_BLOCK_H__

#include "Framework/Common/NonCopyable.h"
#include "Render/Buffer.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class Texture;
    }
}

namespace Ailu
{
    namespace UI
    {
        struct DrawerBlock : public NonCopyable
        {
        public:
            friend class UIRenderer;
            inline static u32 kMaxVertNum = 1200u;
        public:
            DrawerBlock(DrawerBlock &&other) noexcept;
            DrawerBlock &operator=(DrawerBlock &&other) noexcept;
            DrawerBlock(Ref<Render::Material> mat,u32 vert_num = kMaxVertNum);
            ~DrawerBlock();
            bool CanAppend(u32 vert_num, u32 index_num) const {return _cur_vert_num + vert_num < _max_vert_num && _cur_index_num + index_num < _max_vert_num;};
            void ResetBuildData()
            {
                _cur_vert_num = 0u;
                _cur_index_num = 0u;
                _nodes.clear();
                _gpu_dirty = true;
            }

            u32 CurrentVertNum() const { return _cur_vert_num; };
            u32 CurrentIndexNum() const { return _cur_index_num; };
            bool IsGpuDirty() const { return _gpu_dirty; }
            bool IsReady() { return _vbuf != nullptr && _ibuf != nullptr && _vbuf->IsReady() && _ibuf->IsReady(); }
            void CopyBuildDataFrom(const DrawerBlock &other);
            u64 SubmitVertexData();

        private:
            void AppendNode(u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex = nullptr, Rect scissor = {},
                            f32 msdf_px_range = 0.0f, bool is_backdrop_blur = false)
            {
                if (vert_num == 0u || index_num == 0u)
                    return;
                bool is_custom_scissor = scissor.width != 0u;
                if (_nodes.empty())
                    _nodes.emplace_back(DrawNode{_cur_vert_num, vert_num, _cur_index_num, index_num, mat, tex, is_custom_scissor, scissor, msdf_px_range, is_backdrop_blur});
                else
                {
                    auto &pre_node = _nodes.back();
                    if (pre_node._mat == mat && pre_node._main_tex == tex && pre_node._is_custom_scissor == is_custom_scissor &&
                        pre_node._scissor == scissor && pre_node._msdf_px_range == msdf_px_range && pre_node._is_backdrop_blur == is_backdrop_blur)
                    {
                        pre_node._vert_num += vert_num;
                        pre_node._index_num += index_num;
                    }
                    else
                        _nodes.emplace_back(DrawNode{_cur_vert_num, vert_num, _cur_index_num, index_num, mat, tex, is_custom_scissor, scissor, msdf_px_range, is_backdrop_blur});
                }
                _cur_vert_num += vert_num;
                _cur_index_num += index_num;
                _gpu_dirty = true;
            }
        public:
            Ref<Render::Material> _mat;
            Ref<Render::VertexBuffer> _vbuf;
            Ref<Render::IndexBuffer> _ibuf;
            Ref<Render::ConstantBuffer> _obj_cb;
            Vector<Vector3f> _pos_buf;
            Vector<Vector2f> _uv_buf;
            Vector<Color> _color_buf;
            Vector<Vector4f> _rect_buf;
            Vector<Vector4f> _corner_radius_buf;
            Vector<Vector4f> _border_thickness_buf;
            Vector<u32> _index_buf;
            u32 _max_vert_num = 0u;
            struct DrawNode
            {
                u32 _vert_offset;
                u32 _vert_num;
                u32 _index_offset;
                u32 _index_num;
                Render::Material *_mat;
                Render::Texture *_main_tex;
                bool _is_custom_scissor;
                Rect _scissor;
                f32 _msdf_px_range;
                bool _is_backdrop_blur;
            };
            Vector<DrawNode> _nodes;
        private:
            inline static u32 s_id_gen = 0u;
            u32 _cur_vert_num = 0u;
            u32 _cur_index_num = 0u;
            bool _gpu_dirty = true;
        };
    }
}
#endif
