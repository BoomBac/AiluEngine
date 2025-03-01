#include "Framework/Math/Random.hpp"
#include "pch.h"

namespace Ailu::Random
{
    #pragma region Random
    Color RandomColor(u32 seed, bool ignore_alpha)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float random_r = dist(rng);
        float random_g = dist(rng);
        float random_b = dist(rng);
        return Color(random_r, random_g, random_b, ignore_alpha ? 1.0f : dist(rng));
    }

    Color32 RandomColor32(u32 seed, bool ignore_alpha)
    {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<i32> dist(0, 255);
        u8 random_r = dist(rng);
        u8 random_g = dist(rng);
        u8 random_b = dist(rng);
        return Color32(random_r, random_g, random_b, ignore_alpha ? 255u : dist(rng));
    }
    #pragma endregion

    #pragma region Noise
    static constexpr i32 kMaxNoiseCellCount = 128;

    Vector2f Hash22(f32 u,f32 v)
    {
        Vector2f ret{};
        ret.x = u * 127.1f + v * 311.7f;
        ret.y = u * 269.5f + v * 183.3f;
        f32 sin0 = sinf(ret.x) * 43758.5453f;
        f32 sin1 = sinf(ret.y) * 43758.5453f;
        ret.x = (sin0 - floorf(sin0)) * 2.0f - 1.0f;
        ret.y = (sin1 - floorf(sin1)) * 2.0f - 1.0f;
        return Normalize(ret);
    }
    Vector3f Hash33(f32 u,f32 v,f32 w)
    {
        Vector3f ret{};
        ret.x = u * 127.1f + v * 311.7f + w * 74.7f;
        ret.y = u * 269.5f + v * 183.3f + w * 246.1f;
        ret.z = u * 419.2f + v * 371.9f + w * 594.3f;
        f32 sin0 = sinf(ret.x) * 43758.5453f;
        f32 sin1 = sinf(ret.y) * 43758.5453f;
        f32 sin2 = sinf(ret.z) * 43758.5453f;
        ret.x = (sin0 - floorf(sin0)) * 2.0f - 1.0f;
        ret.y = (sin1 - floorf(sin1)) * 2.0f - 1.0f;
        ret.z = (sin2 - floorf(sin2)) * 2.0f - 1.0f;
        return Normalize(ret);
    }

    NoiseGenerator::NoiseGenerator(u32 seed)
    {
        _perlin_grads = new Vector2f[kMaxNoiseCellCount * kMaxNoiseCellCount];
        _perlin_grad3ds = new Vector3f[kMaxNoiseCellCount * kMaxNoiseCellCount * kMaxNoiseCellCount];
        _voronoi_offsets = new Vector3f[kMaxNoiseCellCount * kMaxNoiseCellCount * kMaxNoiseCellCount];
        std::mt19937 rng(seed);
        std::uniform_real_distribution<f32> dist(0.0f, 1.0f);
        auto dice = std::bind(dist, rng);
        Vector3f offset = Vector3f(0.5f);
        for (i32 i = 0; i < kMaxNoiseCellCount; i++)
        {
            for (i32 j = 0; j < kMaxNoiseCellCount; j++)
            {
                f32 a = dice();
                _perlin_grads[i + j * kMaxNoiseCellCount] = Hash22(dice(), dice());//Vector2f(cos(a * 2 * kPi),sin(a*2*kPi));//
                for (i32 k = 0; k < kMaxNoiseCellCount; k++)
                {
                    _perlin_grad3ds[i + j * kMaxNoiseCellCount + k * kMaxNoiseCellCount * kMaxNoiseCellCount] = Hash33(dice(), dice(), dice());
                    _voronoi_offsets[i + j * kMaxNoiseCellCount + k * kMaxNoiseCellCount * kMaxNoiseCellCount] = Vector3f(dice(), dice(), dice());// - offset;
                }
            }
        }
    }

    NoiseGenerator::~NoiseGenerator()
    {
        delete[] _perlin_grads;
        delete[] _perlin_grad3ds;
        delete[] _voronoi_offsets;
    }

    f32 NoiseGenerator::Perlin2D(f32 u, f32 v, f32 cell_size)
    {
        u/=cell_size;
        v/=cell_size;
        Vector2f cell_pos_f = Vector2f(u, v);
        Vector2Int cell_pos = Vector2Int((i32) floorf(cell_pos_f.x), (i32) floorf(cell_pos_f.y));
        cell_pos.x %= kMaxNoiseCellCount;
        cell_pos.y %= kMaxNoiseCellCount;
        Vector2f cell_inner_pos = Fract(cell_pos_f);
        cell_pos_f = Vector2f((f32) cell_pos.x, (f32) cell_pos.y);
        const static Vector2f corners[4] = {
                {0, 0},
                {1, 0},
                {0, 1},
                {1, 1}};
        f32 weights[4];
        Vector2f period(1.0f / cell_size);
        for (u32 i = 0; i < 4; i++)
        {
            Vector2f offset = corners[i];
            Vector2f neighbor_cell = cell_pos_f + offset;
            neighbor_cell = Modulo(neighbor_cell, period);
            Vector2f gradient = _perlin_grads[static_cast<u32>(neighbor_cell.x) + static_cast<u32>(neighbor_cell.y) * kMaxNoiseCellCount];
            Vector2f dist = cell_inner_pos - offset;
            weights[i] = DotProduct(gradient, dist);
        }
        f32 u_fade = Fade(cell_inner_pos.x);
        f32 v_fade = Fade(cell_inner_pos.y);
        f32 lerp_x1 = Lerp(weights[0], weights[1], u_fade);
        f32 lerp_x2 = Lerp(weights[2], weights[3], u_fade);
        return Lerp(lerp_x1, lerp_x2, v_fade);// * 0.5f + 0.5f;
    }

    f32 NoiseGenerator::Perlin3D(f32 u, f32 v, f32 w, f32 cell_size)
    {
        u/=cell_size;
        v/=cell_size;
        w/=cell_size;
        Vector3f cell_pos_f = Vector3f(u, v, w);
        Vector3Int cell_pos = Vector3Int(
                (i32) floorf(cell_pos_f.x),
                (i32) floorf(cell_pos_f.y),
                (i32) floorf(cell_pos_f.z));
        cell_pos.x %= kMaxNoiseCellCount;
        cell_pos.y %= kMaxNoiseCellCount;
        cell_pos.z %= kMaxNoiseCellCount;

        Vector3f cell_inner_pos = Fract(cell_pos_f);
        cell_pos_f = Vector3f((f32) cell_pos.x, (f32) cell_pos.y, (f32) cell_pos.z);

        const static Vector3f corners[8] = {
                {0, 0, 0},
                {1, 0, 0},
                {0, 1, 0},
                {1, 1, 0},
                {0, 0, 1},
                {1, 0, 1},
                {0, 1, 1},
                {1, 1, 1}};
        Vector3f period(1.0f / cell_size);
        f32 weights[8];
        for (u32 i = 0; i < 8; i++)
        {
            Vector3f offset = corners[i];
            Vector3f neighbor_cell = cell_pos_f + offset;
            neighbor_cell = Modulo(neighbor_cell, period);

            Vector3f gradient = _perlin_grad3ds[static_cast<u32>(neighbor_cell.x) +
                                              static_cast<u32>(neighbor_cell.y) * kMaxNoiseCellCount +
                                              static_cast<u32>(neighbor_cell.z) * kMaxNoiseCellCount * kMaxNoiseCellCount];

            Vector3f dist = cell_inner_pos - offset;
            weights[i] = DotProduct(gradient, dist);
        }
        f32 u_fade = Fade(cell_inner_pos.x);
        f32 v_fade = Fade(cell_inner_pos.y);
        f32 w_fade = Fade(cell_inner_pos.z);
        f32 lerp_x1 = Lerp(weights[0], weights[1], u_fade);
        f32 lerp_x2 = Lerp(weights[2], weights[3], u_fade);
        f32 lerp_y1 = Lerp(lerp_x1, lerp_x2, v_fade);
        f32 lerp_x3 = Lerp(weights[4], weights[5], u_fade);
        f32 lerp_x4 = Lerp(weights[6], weights[7], u_fade);
        f32 lerp_y2 = Lerp(lerp_x3, lerp_x4, v_fade);
        return Lerp(lerp_y1, lerp_y2, w_fade);
    }

    f32 NoiseGenerator::Voronoi2D(f32 u, f32 v, f32 cell_size)
    {
        static const Vector2Int CellOffsets[] =
                {
                        Vector2Int(-1, -1),
                        Vector2Int(0, -1),
                        Vector2Int(1, -1),
                        Vector2Int(-1, 0),
                        Vector2Int(0, 0),
                        Vector2Int(1, 0),
                        Vector2Int(-1, 1),
                        Vector2Int(0, 1),
                        Vector2Int(1, 1)};
        Vector2f pos = {u, v};
        Vector2f cell_pos_f = Vector2f(u / cell_size, v / cell_size);
        Vector2Int cell_pos = Vector2Int((i32) floorf(cell_pos_f.x), (i32) floorf(cell_pos_f.y));
        Vector2f cell_inner_pos = Fract(cell_pos_f);
        cell_pos_f = Vector2f((f32) cell_pos.x, (f32) cell_pos.y);
        f32 min_dist = 1.0f;
        i32 cell_count_per_row = (i32) (1.0f / cell_size);
        for (i32 i = 0; i < 9; i++)
        {
            Vector2Int ne = cell_pos + CellOffsets[i];
            i32 x = ne.x;
            i32 y = ne.y;
            // Check if the checked cell coordinates are outside the "cell map" boundaries.
            if (x == -1 || x == cell_count_per_row || y == -1 || y == cell_count_per_row)
            {
                // Wrap around the cell grid to find the distance to a feature point in a cell on the opposite side.
                Vector2Int wrappedCellCoordinate = ne;
                wrappedCellCoordinate.x += cell_count_per_row;
                wrappedCellCoordinate.y += cell_count_per_row;
                wrappedCellCoordinate.x = wrappedCellCoordinate.x % cell_count_per_row;
                wrappedCellCoordinate.y = wrappedCellCoordinate.y % cell_count_per_row;
                i32 wrapped_cell_index = wrappedCellCoordinate.x + wrappedCellCoordinate.y * cell_count_per_row;
                Vector2f random_offset = wrapped_cell_index < 0 ? Vector2f::kZero : _voronoi_offsets[wrapped_cell_index].xy;
                Vector2f featurePointOffset = Vector2f((f32) ne.x, (f32) ne.y) + random_offset + 0.5f;
                min_dist = std::min<f32>(min_dist, Magnitude(cell_pos_f + cell_inner_pos - featurePointOffset));
            }
            else
            {
                // The checked cell is inside the "cell map" boundaries. Check the distance to the feature point.
                int cellIndex = x + y * cell_count_per_row;
                cellIndex = cellIndex < 0 ? 0 : cellIndex;
                Vector2f random_offset = cellIndex < 0 ? Vector2f::kZero : _voronoi_offsets[cellIndex].xy;
                Vector2f featurePointOffset = Vector2f((f32) ne.x, (f32) ne.y) + random_offset + 0.5f;
                min_dist = std::min<f32>(min_dist, Magnitude(cell_pos_f + cell_inner_pos - featurePointOffset));
            }
        }
        return 1.0f - min_dist;
    }

    f32 NoiseGenerator::Voronoi3D(f32 u, f32 v, f32 w, f32 cell_size)
    {
        static const Vector3Int CellOffsets[] =
                {
                        Vector3Int(-1, -1, -1),
                        Vector3Int(0, -1, -1),
                        Vector3Int(1, -1, -1),
                        Vector3Int(-1, -1, 0),
                        Vector3Int(0, -1, 0),
                        Vector3Int(1, -1, 0),
                        Vector3Int(-1, -1, 1),
                        Vector3Int(0, -1, 1),
                        Vector3Int(1, -1, 1),
                        Vector3Int(-1, 0, -1),
                        Vector3Int(0, 0, -1),
                        Vector3Int(1, 0, -1),
                        Vector3Int(-1, 0, 0),
                        Vector3Int(0, 0, 0),
                        Vector3Int(1, 0, 0),
                        Vector3Int(-1, 0, 1),
                        Vector3Int(0, 0, 1),
                        Vector3Int(1, 0, 1),
                        Vector3Int(-1, 1, -1),
                        Vector3Int(0, 1, -1),
                        Vector3Int(1, 1, -1),
                        Vector3Int(-1, 1, 0),
                        Vector3Int(0, 1, 0),
                        Vector3Int(1, 1, 0),
                        Vector3Int(-1, 1, 1),
                        Vector3Int(0, 1, 1),
                        Vector3Int(1, 1, 1),
                };
        Vector3f pos = {u, v, w};
        Vector3f cell_pos_f = Vector3f(u / cell_size, v / cell_size, w / cell_size);
        Vector3Int cell_pos = Vector3Int((i32) floorf(cell_pos_f.x), (i32) floorf(cell_pos_f.y), (i32) floorf(cell_pos_f.z));
        Vector3f cell_inner_pos = Fract(cell_pos_f);
        cell_pos_f = Vector3f((f32) cell_pos.x, (f32) cell_pos.y, (f32) cell_pos.z);
        f32 min_dist = 1.0f;
        i32 cell_count_per_row = (i32) (1.0f / cell_size);
        for (i32 i = 0; i < 27; i++)
        {
            Vector3Int ne = cell_pos + CellOffsets[i];
            i32 x = ne.x;
            i32 y = ne.y;
            i32 z = ne.z;
            // Check if the checked cell coordinates are outside the "cell map" boundaries.
            if (x == -1 || x == cell_count_per_row || y == -1 || y == cell_count_per_row || z == -1 || z == cell_count_per_row)
            {
                // Wrap around the cell grid to find the distance to a feature point in a cell on the opposite side.
                Vector3Int wrappedCellCoordinate = ne;
                wrappedCellCoordinate.x += cell_count_per_row;
                wrappedCellCoordinate.y += cell_count_per_row;
                wrappedCellCoordinate.z += cell_count_per_row;
                wrappedCellCoordinate.x = wrappedCellCoordinate.x % cell_count_per_row;
                wrappedCellCoordinate.y = wrappedCellCoordinate.y % cell_count_per_row;
                wrappedCellCoordinate.z = wrappedCellCoordinate.z % cell_count_per_row;
                i32 wrapped_cell_index = wrappedCellCoordinate.x + cell_count_per_row * (wrappedCellCoordinate.y + wrappedCellCoordinate.z * cell_count_per_row);
                wrapped_cell_index = wrapped_cell_index < 0 ? 0 : wrapped_cell_index;
                Vector3f random_offset = wrapped_cell_index < 0 ? Vector3f::kZero : _voronoi_offsets[wrapped_cell_index];
                Vector3f featurePointOffset = Vector3f((f32) ne.x, (f32) ne.y, (f32) ne.z) + random_offset;
                min_dist = std::min<f32>(min_dist, Magnitude(cell_pos_f + cell_inner_pos - featurePointOffset));
            }
            else
            {
                // The checked cell is inside the "cell map" boundaries. Check the distance to the feature point.
                i32 cellIndex = x + cell_count_per_row * (y + z * cell_count_per_row);
                cellIndex = cellIndex < 0 ? 0 : cellIndex;
                Vector3f random_offset = cellIndex < 0 ? Vector3f::kZero : _voronoi_offsets[cellIndex];
                Vector3f featurePointOffset = Vector3f((f32) ne.x, (f32) ne.y, (f32) ne.z) + random_offset;
                min_dist = std::min<f32>(min_dist, Magnitude(cell_pos_f + cell_inner_pos - featurePointOffset));
            }
        }
        return 1.0f - min_dist;
    }


    f32 NoiseGenerator::FBM(f32 u, f32 v, u16 octaves, f32 lacunarity, f32 gain, f32 period, std::function<f32(NoiseGenerator*,f32, f32, f32)> noise_func)
    {
        f32 sum = 0.0f;
        f32 amplitude = 1.0f;
        f32 frequency = 1.0f;
        f32 max_value = 0.0f;// 用于归一化最终值
        for (u16 i = 0; i < octaves; i++)
        {
            sum += noise_func(this,u * frequency, v * frequency, period) * amplitude;
            max_value += amplitude;
            frequency *= lacunarity;
            amplitude *= gain;
        }
        return sum / max_value;
    }
    f32 NoiseGenerator::FBM(f32 u, f32 v, f32 w, u16 octaves, f32 lacunarity, f32 gain, f32 period, std::function<f32(NoiseGenerator*,f32, f32, f32, f32)> noise_func)
    {
        f32 sum = 0.0f;
        f32 amplitude = 1.0f;
        f32 frequency = 1.0f;
        f32 max_value = 0.0f;// 用于归一化最终值
        for (u16 i = 0; i < octaves; i++)
        {
            sum += noise_func(this,u * frequency, v * frequency, w * frequency, period) * amplitude;
            max_value += amplitude;
            frequency *= lacunarity;
            amplitude *= gain;
        }
        return sum / max_value;
    }
    #pragma endregion
}// namespace Ailu::Random