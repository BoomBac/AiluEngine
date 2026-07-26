#include "UI/DrawerBlock.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        DrawerBlock::DrawerBlock(Ref<Render::Material> mat, u32 vert_num) : _max_vert_num(vert_num), _mat(mat)
        {
            Vector<VertexBufferLayoutDesc> desc_list;
            desc_list.emplace_back(RenderConstants::kSemanticPosition, EShaderDateType::kFloat3, 0);
            desc_list.emplace_back(RenderConstants::kSemanticTexcoord, EShaderDateType::kFloat2, 1);
            desc_list.emplace_back(RenderConstants::kSemanticColor, EShaderDateType::kFloat4, 2);
            desc_list.emplace_back(RenderConstants::kSemanticTexcoord, EShaderDateType::kFloat4, 3, 1);
            desc_list.emplace_back(RenderConstants::kSemanticTexcoord, EShaderDateType::kFloat4, 4, 2);
            _vbuf = VertexBuffer::Create(desc_list, std::format("block({})_vbuf", s_id_gen));
            _ibuf = IndexBuffer::Create(nullptr, vert_num, std::format("block({})_ibuf", s_id_gen), true);
            _obj_cb = ConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector3f), 0, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector2f), 1, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector4f), 2, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector4f), 3, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector4f), 4, true);
            //_ibuf->SetData((u8*)indices,6);
            _pos_buf.resize(vert_num);
            _uv_buf.resize(vert_num);
            _color_buf.resize(vert_num);
            _rect_buf.resize(vert_num);
            _corner_radius_buf.resize(vert_num);
            _index_buf.resize(vert_num);
            GraphicsContext::Get().CreateResourceSync(_vbuf);
            GraphicsContext::Get().CreateResourceSync(_ibuf);
            ++s_id_gen;
        }
        DrawerBlock::~DrawerBlock()
        {
            --s_id_gen;
            delete _vbuf; _vbuf = nullptr;
            delete _ibuf; _ibuf = nullptr;
        }

        void DrawerBlock::CopyBuildDataFrom(const DrawerBlock &other)
        {
            AL_ASSERT(_max_vert_num >= other._cur_vert_num && _max_vert_num >= other._cur_index_num);
            _cur_vert_num = other._cur_vert_num;
            _cur_index_num = other._cur_index_num;
            _nodes = other._nodes;
            if (_cur_vert_num > 0u)
            {
                memcpy(_pos_buf.data(), other._pos_buf.data(), _cur_vert_num * sizeof(Vector3f));
                memcpy(_uv_buf.data(), other._uv_buf.data(), _cur_vert_num * sizeof(Vector2f));
                memcpy(_color_buf.data(), other._color_buf.data(), _cur_vert_num * sizeof(Color));
                memcpy(_rect_buf.data(), other._rect_buf.data(), _cur_vert_num * sizeof(Vector4f));
                memcpy(_corner_radius_buf.data(), other._corner_radius_buf.data(), _cur_vert_num * sizeof(Vector4f));
            }
            if (_cur_index_num > 0u)
                memcpy(_index_buf.data(), other._index_buf.data(), _cur_index_num * sizeof(u32));
            _gpu_dirty = true;
        }

        u64 DrawerBlock::SubmitVertexData()
        {
            if (!_gpu_dirty)
                return 0u;
            if (!IsReady())
                return 0u;
            const u64 pos_bytes = _cur_vert_num * sizeof(Vector3f);
            const u64 uv_bytes = _cur_vert_num * sizeof(Vector2f);
            const u64 color_bytes = _cur_vert_num * sizeof(Vector4f);
            const u64 rect_bytes = _cur_vert_num * sizeof(Vector4f);
            const u64 corner_radius_bytes = _cur_vert_num * sizeof(Vector4f);
            const u64 index_bytes = _cur_index_num * sizeof(u32);
            _vbuf->SetData((u8 *)_pos_buf.data(), static_cast<u32>(pos_bytes), 0u, 0u);
            _vbuf->SetData((u8 *)_uv_buf.data(), static_cast<u32>(uv_bytes), 1u, 0u);
            _vbuf->SetData((u8 *)_color_buf.data(), static_cast<u32>(color_bytes), 2u, 0u);
            _vbuf->SetData((u8 *)_rect_buf.data(), static_cast<u32>(rect_bytes), 3u, 0u);
            _vbuf->SetData((u8 *)_corner_radius_buf.data(), static_cast<u32>(corner_radius_bytes), 4u, 0u);
            _ibuf->SetData((u8 *)_index_buf.data(), static_cast<u32>(index_bytes));
            _gpu_dirty = false;
            return pos_bytes + uv_bytes + color_bytes + rect_bytes + corner_radius_bytes + index_bytes;
        }

        DrawerBlock::DrawerBlock(DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
        }
        DrawerBlock &DrawerBlock::operator=(DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
            return *this;
        }
    }
}
