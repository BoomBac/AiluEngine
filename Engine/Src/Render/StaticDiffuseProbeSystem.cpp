#include "pch.h"

#include "Render/StaticDiffuseProbeSystem.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/RenderConstants.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
    namespace
    {
        Vector3f ClampGridCoordinate(const Vector3f &value, const Vector3Int &count)
        {
            return Vector3f(
                std::clamp(value.x, 0.0f, static_cast<f32>(std::max(0, count.x - 1))),
                std::clamp(value.y, 0.0f, static_cast<f32>(std::max(0, count.y - 1))),
                std::clamp(value.z, 0.0f, static_cast<f32>(std::max(0, count.z - 1))));
        }

        void AccumulateProbe(const StaticDiffuseProbeData &data, i32 x, i32 y, i32 z, f32 weight,
                             SphericalHarmonicsL2 &out_sh)
        {
            const auto &count = data._grid._probe_count;
            x = std::clamp(x, 0, count.x - 1);
            y = std::clamp(y, 0, count.y - 1);
            z = std::clamp(z, 0, count.z - 1);
            const u32 index = data._grid.FlattenIndex(x, y, z);
            if (index < data._probes.size())
                Accumulate(out_sh, data._probes[index], weight);
        }
    }

    void StaticDiffuseProbeSystem::Update(const SceneManagement::Scene &scene)
    {
        _groups.clear();
        u64 revision = scene.StructureRevision();
        const auto &registry = scene.GetRegister();
        for (u32 component_index = 0u; component_index < registry.View<ECS::CLightProbeGroup>().size();
             ++component_index)
        {
            const auto &component = registry.View<ECS::CLightProbeGroup>()[component_index];
            const ECS::Entity entity = registry.GetEntity<ECS::CLightProbeGroup>(component_index);
            if (!scene.IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::CLightProbeGroup>(entity) ||
                component._baked_data.IsEmpty())
                continue;
            ResourceMgr::Get().Load<StaticDiffuseProbeData>(component._baked_data);
            auto data = ResourceMgr::Get().GetRef<StaticDiffuseProbeData>(component._baked_data);
            const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
            if (data == nullptr || transform == nullptr || data->_grid.ProbeCount() == 0u)
                continue;
            const Vector3f render_position = transform->GetRenderPosition();
            const auto mix_revision = [&revision](f32 value)
            {
                revision ^= std::hash<f32>{}(value) + 0x9e3779b97f4a7c15ull +
                            (revision << 6u) + (revision >> 2u);
            };
            mix_revision(render_position.x);
            mix_revision(render_position.y);
            mix_revision(render_position.z);
            revision ^= std::hash<f32>{}(component._size.x) + std::hash<f32>{}(component._size.y) +
                        std::hash<f32>{}(component._size.z) + data->_revision;
            revision ^= std::hash<f32>{}(data->_grid._spacing.x) + std::hash<f32>{}(data->_grid._spacing.y) +
                        std::hash<f32>{}(data->_grid._spacing.z);
            revision ^= static_cast<u64>(data->_grid._probe_count.x) * 0x9e3779b97f4a7c15ull;
            revision ^= static_cast<u64>(data->_grid._probe_count.y) * 0xbf58476d1ce4e5b9ull;
            revision ^= static_cast<u64>(data->_grid._probe_count.z) * 0x94d049bb133111ebull;
            _groups.emplace_back(Group{render_position, component._size, std::move(data)});
        }
        _revision = revision;
    }

    bool StaticDiffuseProbeSystem::Sample(const Vector3f &world_position, SphericalHarmonicsL2 &out_sh,
                                          f32 &out_weight) const
    {
        const Group *selected = nullptr;
        f32 selected_volume = std::numeric_limits<f32>::max();
        for (const Group &group : _groups)
        {
            const Vector3f local = world_position - group._position;
            const Vector3f half_size = group._size * 0.5f;
            if (std::abs(local.x) > half_size.x || std::abs(local.y) > half_size.y || std::abs(local.z) > half_size.z)
                continue;
            const f32 volume = group._size.x * group._size.y * group._size.z;
            if (volume < selected_volume)
            {
                selected = &group;
                selected_volume = volume;
            }
        }
        if (selected == nullptr)
        {
            out_sh.Clear();
            out_weight = 0.0f;
            return false;
        }

        const auto &grid = selected->_data->_grid;
        Vector3f grid_coordinate = Vector3f::kZero;
        if (std::abs(grid._spacing.x) > kFloatEpsilon)
            grid_coordinate.x = (world_position.x - selected->_position.x - grid._origin.x) / grid._spacing.x;
        if (std::abs(grid._spacing.y) > kFloatEpsilon)
            grid_coordinate.y = (world_position.y - selected->_position.y - grid._origin.y) / grid._spacing.y;
        if (std::abs(grid._spacing.z) > kFloatEpsilon)
            grid_coordinate.z = (world_position.z - selected->_position.z - grid._origin.z) / grid._spacing.z;
        const Vector3f grid_position = ClampGridCoordinate(grid_coordinate, grid._probe_count);
        const Vector3Int base(static_cast<i32>(std::floor(grid_position.x)), static_cast<i32>(std::floor(grid_position.y)),
                              static_cast<i32>(std::floor(grid_position.z)));
        const Vector3f fraction(grid_position.x - base.x, grid_position.y - base.y, grid_position.z - base.z);
        out_sh.Clear();
        for (i32 z = 0; z <= 1; ++z)
            for (i32 y = 0; y <= 1; ++y)
                for (i32 x = 0; x <= 1; ++x)
                {
                    const f32 weight = (x ? fraction.x : 1.0f - fraction.x) *
                                       (y ? fraction.y : 1.0f - fraction.y) *
                                       (z ? fraction.z : 1.0f - fraction.z);
                    AccumulateProbe(*selected->_data, base.x + x, base.y + y, base.z + z, weight, out_sh);
                }
        out_weight = 1.0f;
        return true;
    }

    void StaticDiffuseProbeSystem::BuildGPUData(Vector<StaticDiffuseProbeGroupData> &out_groups,
                                                 Vector<StaticDiffuseProbeSHData> &out_probes) const
    {
        out_groups.clear();
        out_probes.clear();
        // GPU 侧缓冲区容量是硬上限。某个探针组的 probe_offset 一旦越过它，
        // 着色器就会按越界索引读 SH，直接触发 device removal。
        // 放不下的组按顺序丢弃并清零，由 deferred_lighting 的探测循环跳过。
        u32 total_probe_count = 0u;
        for (const Group &group : _groups)
            total_probe_count += group._data->_grid.ProbeCount();

        u32 skipped_probe_count = 0u;
        out_groups.reserve(_groups.size());
        for (const Group &group : _groups)
        {
            const auto &grid = group._data->_grid;
            const u32 probe_count = grid.ProbeCount();
            StaticDiffuseProbeGroupData group_data{};
            if (out_probes.size() + probe_count <= RenderConstants::kMaxStaticDiffuseProbeCount)
            {
                group_data._position = group._position;
                group_data._probe_offset = static_cast<u32>(out_probes.size());
                group_data._size = group._size;
                group_data._probe_count_x = static_cast<u32>(grid._probe_count.x);
                group_data._origin = grid._origin;
                group_data._probe_count_y = static_cast<u32>(grid._probe_count.y);
                group_data._spacing = grid._spacing;
                group_data._probe_count_z = static_cast<u32>(grid._probe_count.z);
                for (u32 probe_index = 0u; probe_index < probe_count; ++probe_index)
                {
                    StaticDiffuseProbeSHData probe_data{};
                    if (probe_index < group._data->_probes.size())
                    {
                        const SphericalHarmonicsL2 &sh = group._data->_probes[probe_index];
                        for (u32 coefficient = 0u; coefficient < SphericalHarmonicsL2::kCoefficientCount; ++coefficient)
                            probe_data._sh[coefficient] = sh._coefficients[coefficient];
                    }
                    out_probes.emplace_back(probe_data);
                }
            }
            else
            {
                // 探针数置零，着色器会跳过该组（deferred_lighting 的探测循环显式判断）。
                skipped_probe_count += probe_count;
            }
            out_groups.emplace_back(group_data);
        }
        // 补齐到容量上限，使着色器 GetDimensions 返回的组数与实际分配一致。
        out_groups.resize(RenderConstants::kMaxStaticDiffuseProbeGroupCount);
        out_probes.resize(RenderConstants::kMaxStaticDiffuseProbeCount);
        if (skipped_probe_count > 0u)
            LOG_WARNING("Static diffuse probe GPU data trimmed: {} of {} probes exceed the {}-probe GPU budget; "
                        "lower the probe group counts",
                        skipped_probe_count, total_probe_count, RenderConstants::kMaxStaticDiffuseProbeCount);
    }
}
