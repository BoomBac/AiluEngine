#ifndef __BVH_BUILDER_H__
#define __BVH_BUILDER_H__
#include "Geometry.h"
namespace Ailu
{
    struct BVHNode
    {
        AABB _aabb;
        //叶子节点时，对象索引；否则为左节点索引
        i32 _child_index_or_first;
        i32 _count_or_flag;//if >0，对象数量，否则为右节点索引的负值
        bool IsLeaf() const { return _count_or_flag > 0; }
    };

    struct TriRef
    {
        i32 _tri_index;
        Vector3f _centroid;
        AABB _aabb;
    };

    class BVHBuilder
    {
    public:
        struct Result
        {
            Vector<BVHNode> _nodes;
            Vector<u32> _reordered_indices;
        };
        BVHBuilder(const Vector<AABB> &tri_aabbs) : _tri_aabbs(tri_aabbs)
        {
            i32 n = (i32) tri_aabbs.size();
            _refs.resize(n);
            for (i32 i = 0; i < n; ++i)
            {
                _refs[i]._tri_index = i;
                _refs[i]._aabb = tri_aabbs[i];
                // centroid
                for (i32 k = 0; k < 3; ++k) 
                    _refs[i]._centroid[k] = 0.5f * (tri_aabbs[i]._min[k] + tri_aabbs[i]._max[k]);
            }
            _nodes.reserve(n * 2);
        }

        Result Build(i32 max_leaf_size = 4, i32 bin_count = 16)
        {
            _max_leaf_size = max_leaf_size;
            _bin_count = bin_count;
            BuildRange(0, (i32) _refs.size());
            Vector<u32> indices(_refs.size());
            for (i32 i = 0; i < _refs.size(); ++i)
                indices[i] = _refs[i]._tri_index;
            return {_nodes, indices};
        }

    private:
        const Vector<AABB> &_tri_aabbs;
        Vector<TriRef> _refs;
        Vector<BVHNode> _nodes;
        i32 _max_leaf_size = 4;
        i32 _bin_count = 16;

        i32 BuildRange(i32 begin, i32 end);

        i32 PartitionByBin(i32 begin, i32 end, i32 axis, f32 cmin, f32 cmax, i32 split_bin);
    };
}// namespace Ailu
#endif