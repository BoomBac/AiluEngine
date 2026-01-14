#include "Framework/Math/BVHBuilder.h"

namespace Ailu
{
    i32 BVHBuilder::BuildRange(i32 begin, i32 end)
    {
        // create node index
        i32 node_index = (i32) _nodes.size();
        _nodes.push_back(BVHNode());
        BVHNode &node = _nodes.back();

        // compute bounds and centroid bounds
        AABB node_aabb = AABB::Infinity();
        AABB centroid_aabb = node_aabb;
        for (i32 i = begin; i < end; ++i)
        {
            AABB::Encapsulate(node_aabb, _refs[i]._aabb);
            AABB::Encapsulate(centroid_aabb, _refs[i]._centroid);
        }
        node._aabb = node_aabb;
        i32 n = end - begin;
        if (n <= _max_leaf_size)
        {
            // make leaf
            node._child_index_or_first = begin;
            node._count_or_flag = n;// >0 means leaf
            return node_index;
        }

        // pick axis by centroid extent
        i32 axis = centroid_aabb.LongestAxis();
        f32 cmin = centroid_aabb._min[axis];
        f32 cmax = centroid_aabb._max[axis];
        if (cmax <= cmin)//所有物体重心在一起
        {
            node._child_index_or_first = begin;
            node._count_or_flag = n;
            return node_index;
        }

        // binning
        struct Bin
        {
            AABB aabb;
            i32 count = 0;
        };
        std::vector<Bin> bins(_bin_count);
        // init bins
        for (auto &b: bins)
        {
            b.aabb = AABB::Infinity();
            b.count = 0;
        }
        // fill bins
        f32 inv_range = 1.0f / (cmax - cmin);
        for (i32 i = begin; i < end; ++i)
        {
            i32 bidx = (i32) (((_refs[i]._centroid[axis] - cmin) * inv_range) * _bin_count);
            if (bidx == _bin_count) 
                bidx = _bin_count - 1;
            bins[bidx].count++;
            AABB::Encapsulate(bins[bidx].aabb, _refs[i]._aabb);
        }
        // prefix/suffix
        std::vector<AABB> left_aabb(_bin_count - 1);
        std::vector<i32> left_count(_bin_count - 1);
        AABB acc = AABB::Infinity();
        i32 acc_count = 0;
        for (i32 i = 0; i < _bin_count - 1; ++i)
        {
            acc_count += bins[i].count;
            AABB::Encapsulate(acc, bins[i].aabb);
            left_aabb[i] = acc;
            left_count[i] = acc_count;
        }
        std::vector<AABB> right_aabb(_bin_count - 1);
        std::vector<i32> right_count(_bin_count - 1);
        acc = AABB::Infinity();
        acc_count = 0;
        for (i32 i = _bin_count - 1; i > 0; --i)
        {
            acc_count += bins[i].count;
            AABB::Encapsulate(acc, bins[i].aabb);
            right_aabb[i - 1] = acc;
            right_count[i - 1] = acc_count;
        }
        // evaluate best split
        f32 best_cost = std::numeric_limits<f32>::infinity();
        i32 best_split = -1;
        f32 parent_area = node_aabb.Area();
        for (i32 i = 0; i < _bin_count - 1; ++i)
        {
            if (left_count[i] == 0 || right_count[i] == 0) continue;
            f32 cost = left_aabb[i].Area() * left_count[i] + right_aabb[i].Area() * right_count[i];
            if (cost < best_cost)
            {
                best_cost = cost;
                best_split = i;
            }
        }

        if (best_split == -1)
        {
            // fallback: median by centroid
            i32 mid = (begin + end) >> 1;
            std::nth_element(_refs.begin() + begin, _refs.begin() + mid, _refs.begin() + end, [axis](const TriRef &a, const TriRef &b)
                             { return a._centroid[axis] < b._centroid[axis]; });
            i32 left_child = BuildRange(begin, mid);
            i32 right_child = BuildRange(mid, end);
            // set node children
            node._child_index_or_first = left_child;
            node._count_or_flag = -right_child;// negative indicates internal and stores right child as -value
            return node_index;
        }

        // partition refs according to best_split
        i32 mid = PartitionByBin(begin, end, axis, cmin, cmax, best_split);
        if (mid == begin || mid == end)
        {
            // degenerate -> make leaf
            node._child_index_or_first = begin;
            node._count_or_flag = n;
            return node_index;
        }

        // recurse
        i32 left_child = BuildRange(begin, mid);
        i32 right_child = BuildRange(mid, end);
        node._child_index_or_first = left_child;
        node._count_or_flag = -right_child;
        return node_index;
    }
    
    i32 BVHBuilder::PartitionByBin(i32 begin, i32 end, i32 axis, f32 cmin, f32 cmax, i32 split_bin)
    {
        f32 inv_range = 1.0f / (cmax - cmin);
        i32 i = begin, j = end;
        while (i < j)
        {
            f32 v = (_refs[i]._centroid[axis] - cmin) * inv_range;
            i32 bidx = (i32) (v * _bin_count);
            if (bidx == _bin_count) bidx = _bin_count - 1;
            if (bidx <= split_bin)
            {
                ++i;
            }
            else
            {
                --j;
                std::swap(_refs[i], _refs[j]);
            }
        }
        return i;
    }
}// namespace Ailu
