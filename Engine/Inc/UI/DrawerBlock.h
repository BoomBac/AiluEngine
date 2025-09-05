#ifndef __DRAWER_BLOCK_H__
#define __DRAWER_BLOCK_H__

#include "Render/Buffer.h"
#include "Render/Material.h"

namespace Ailu
{
    namespace UI
    {
        struct DrawerBlock
        {
        public:
            friend class UIRenderer;
            inline static u32 kMaxVertNum = 1200u;
        public:
            DISALLOW_COPY_AND_ASSIGN(DrawerBlock)
            DrawerBlock(DrawerBlock &&other) noexcept;
            DrawerBlock &operator=(DrawerBlock &&other) noexcept;
            DrawerBlock(Ref<Render::Material> mat,u32 vert_num = kMaxVertNum);
            ~DrawerBlock();
            bool CanAppend(u32 vert_num, u32 index_num) const {return _cur_vert_num + vert_num < _max_vert_num && _cur_index_num + index_num < _max_vert_num;};
            void Flush()
            {
                _cur_vert_num = 0u;
                _cur_index_num = 0u;
                _nodes.clear();
            }

            u32 CurrentVertNum() const { return _cur_vert_num; };
            u32 CurrentIndexNum() const { return _cur_index_num; };
            void SubmitVertexData();

        private:
            void AppendNode(u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex = nullptr, Rect scissor = {})
            {
                if (vert_num == 0u || index_num == 0u)
                    return;
                bool is_custom_scissor = scissor.width != 0u;
                if (_nodes.empty())
                    _nodes.emplace_back(DrawNode{_cur_vert_num, vert_num, _cur_index_num, index_num, mat, tex, is_custom_scissor, scissor});
                else
                {
                    auto &pre_node = _nodes.back();
                    if (pre_node._mat == mat && pre_node._main_tex == tex && pre_node._is_custom_scissor == is_custom_scissor && pre_node._scissor == scissor)
                    {
                        pre_node._vert_num += vert_num;
                        pre_node._index_num += index_num;
                    }
                    else
                        _nodes.emplace_back(DrawNode{_cur_vert_num, vert_num, _cur_index_num, index_num, mat, tex, is_custom_scissor, scissor});
                }
                _cur_vert_num += vert_num;
                _cur_index_num += index_num;
            }
        public:
            Ref<Render::Material> _mat;
            Render::VertexBuffer *_vbuf;
            Render::IndexBuffer *_ibuf;
            Render::ConstantBuffer *_obj_cb;
            Vector<Vector3f> _pos_buf;
            Vector<Vector2f> _uv_buf;
            Vector<Color> _color_buf;
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
            };
            Vector<DrawNode> _nodes;
        private:
            inline static u32 s_id_gen = 0u;
            u32 _cur_vert_num = 0u;
            u32 _cur_index_num = 0u;
        };
    }
}
#endif