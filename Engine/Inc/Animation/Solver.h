//
// Created by 22292 on 2024/10/16.
//

#ifndef AILU_SOLVER_H
#define AILU_SOLVER_H
#include "Framework/Math/Transform.h"
#include "GlobalMarco.h"
/*
 * 逆向动力学
 *
 *
 * */

namespace Ailu
{
    class IConstraint
    {
    public:
        virtual ~IConstraint() = default;
        //current and parent's world transform,ik_chain[current]
        virtual void Apply(const Transform& cur_gt,const Transform& parent_gt,Transform& out) = 0;
    };
    class EmptyConstraint : public IConstraint
    {
    public:
        void Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out) final {};
    };
    class AILU_API BallSocketConstraint : public IConstraint
    {
    public:
        BallSocketConstraint(f32 angle);
        BallSocketConstraint(f32 angle_min, f32 angle_max);
        void Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out) final;
        void SetAngle(f32 angle_min, f32 angle_max);
    private:
        f32 _limit_min,_limit_max;
    };
    class AILU_API HingeConstraint : public IConstraint
    {
    public:
        HingeConstraint(Vector3f axis);
        void Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out) final;
        void SetAxis(const Vector3f &axis);
    private:
        Vector3f _axis;
    };

    class AILU_API IKConstraint : public IConstraint
    {
    public:
        IKConstraint(Vector2f x_limit, Vector2f y_limit, Vector2f z_limit);
        void Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out) final;
        void SetLimit(Vector2f x_limit, Vector2f y_limit, Vector2f z_limit);
        void GetLimit(Vector2f& x_limit, Vector2f& y_limit, Vector2f& z_limit) const;
    private:
        Vector2f _x_limit, _y_limit, _z_limit;
    };


    class AILU_API Solver
    {
    public:
        inline static u16 s_global_step_num = 15u;
        //temp
        inline static Transform s_global_goal;
        inline static bool s_is_apply_constraint = true;
        Solver();
        ~Solver();
        virtual bool Solve(const Transform &target);
        [[nodiscard]] u16 Size() const;
        virtual void Resize(u16 new_size);
        virtual void Clear();
        //赋值时0为根节点，传入世界变换，其他的传入局部变换
        Transform &operator[](u32 index);
        //ownship of constraint move to solver
        void AddConstraint(u32 index, IConstraint *c);
        Vector<Vector<IConstraint *>> &GetConstraints();
        [[nodiscard]] Transform GetGlobalTransform(u32 index) const;
        [[nodiscard]] u16 StepNum() const;
        void StepNum(u16 new_step_num);
        [[nodiscard]] f32 Threshold() const;
        void Threshold(f32 threshold);
        void IsApplyConstraint(bool is_apply);
        [[nodiscard]] bool IsApplyConstraint() const;
    protected:
        Vector<Transform> _ik_chains;
        Vector<Vector<IConstraint *>> _constraints;
        u16 _step_num;
        f32 _threshold;
        bool _is_apply_constraint;
    };
    /*
     * 循环坐标下降
     * */
    class AILU_API CCDSolver : public Solver
    {
    public:
        CCDSolver();
        bool Solve(const Transform &target) final;
    private:
    };

    /*
    * 向前向后到达
    * */
    class AILU_API FABRIKSolver : public Solver
    {
    public:
        FABRIKSolver();
        [[nodiscard]] Transform GetLocalTransform(u32 index) const;
        void SetLocalTransform(u32 index, const Transform &local);
        bool Solve(const Transform &target) final;
        void Resize(u16 new_size) final;
        void Clear() final;
    private:
        void IterateForward(const Vector3f &base);
        void IterateBackward(const Vector3f &goal);
        //将ik链的信息填充到 pos和length两个数组中，length[i]存储关节i到其父关节j-1的长度
        void IKChainToWorld();
        void WorldToIKChain();
        void ConstrainIKChains();
    private:
        Vector<f32> _lengths;
        Vector<Vector3f> _world_chains;
    };
}// namespace Ailu
#endif//AILU_SOLVER_H
