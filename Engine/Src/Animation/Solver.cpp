//
// Created by 22292 on 2024/10/16.
//
#include "Animation/Solver.h"
#include "Render/Gizmo.h"
#include "pch.h"

namespace Ailu
{
    BallSocketConstraint::BallSocketConstraint(f32 angle) : _limit_min(-angle), _limit_max(angle)
    {
    }
    BallSocketConstraint::BallSocketConstraint(f32 angle_min, f32 angle_max) : _limit_min(angle_min), _limit_max(angle_max)
    {
    }
    void BallSocketConstraint::SetAngle(f32 angle_min, f32 angle_max)
    {
        _limit_min = angle_min;
        _limit_max = angle_max;
    }
    void BallSocketConstraint::Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out)
    {
        //Quaternion parent_rot = parent_gt._rotation;
        //Quaternion current_rot = cur_gt._rotation;
        //Vector3f parent_dir = parent_rot * Vector3f::kForward;
        //Vector3f current_dir = current_rot * Vector3f::kForward;
        //f32 r = Math::Radian(parent_dir, current_dir);
        //if (r > _limit * k2Radius)
        //{
        //    Vector3f correction = Normalize(CrossProduct(parent_dir, current_dir));
        //    Quaternion world_rot = parent_rot * Quaternion::AngleAxis(_limit * k2Radius, correction);
        //    out._rotation = world_rot * Quaternion::Inverse(parent_rot);
        //}
        Vector3f rot_axis;
        f32 rot_angle;
        Vector3f loc_pos = cur_gt._rotation * cur_gt._position;
        rot_axis = Quaternion::GetAxis(cur_gt._rotation);
        rot_angle = Quaternion::GetAngle(cur_gt._rotation) * k2Angle;
        if (DotProduct(loc_pos, Vector3f::kForward) <= 0)
            rot_angle = -rot_angle;
        if (rot_angle > _limit_max)
        {
            rot_angle = _limit_max;
            out._rotation = Quaternion::AngleAxis(rot_angle, rot_axis);
            return;
        }
        if (rot_angle < _limit_min)
        {
            rot_angle = _limit_min;
            out._rotation = Quaternion::AngleAxis(abs(rot_angle), rot_axis);
        }
    }

    HingeConstraint::HingeConstraint(Vector3f axis) : _axis(axis)
    {

    }
    void HingeConstraint::SetAxis(const Vector3f &axis)
    {
        _axis = axis;
    }
    void HingeConstraint::Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out)
    {
        //Vector3f currentHinge = cur_gt._rotation * _axis;
        //Vector3f desiredHinge = parent_gt._rotation * _axis;
        //
        //out._rotation = out._rotation * Quaternion::FromTo(currentHinge, desiredHinge);
        Vector3f euler = Quaternion::EulerAngles(cur_gt._rotation);
        euler.y = 0;
        euler.z = 0;
        out._rotation = Quaternion::EulerAngles(euler);
    }

    IKConstraint::IKConstraint(Vector2f x_limit, Vector2f y_limit, Vector2f z_limit) : _x_limit(x_limit), _y_limit(y_limit), _z_limit(z_limit)
    {
    }
    void IKConstraint::Apply(const Transform &cur_gt, const Transform &parent_gt, Transform &out)
    {
        Vector3f euler = Quaternion::EulerAngles(cur_gt._rotation);
        euler.x = std::clamp(euler.x, _x_limit.x, _x_limit.y);
        euler.y = std::clamp(euler.y, _y_limit.x, _y_limit.y);
        euler.z = std::clamp(euler.z, _z_limit.x, _z_limit.y);
        out._rotation = Quaternion::EulerAngles(euler);
    }
    void IKConstraint::SetLimit(Vector2f x_limit, Vector2f y_limit, Vector2f z_limit)
    {
        x_limit.x = std::min(x_limit.x, x_limit.y - 0.1f);
        y_limit.x = std::min(y_limit.x, y_limit.y - 0.1f);
        z_limit.x = std::min(z_limit.x, z_limit.y - 0.1f);
        _x_limit = x_limit;
        _y_limit = y_limit;
        _z_limit = z_limit;
    }
    void IKConstraint::GetLimit(Vector2f &x_limit, Vector2f &y_limit, Vector2f &z_limit) const
    {
        x_limit = _x_limit;
        y_limit = _y_limit;
        z_limit = _z_limit;
    }
    //-----------------------------------------------------------------------------------------------------------------------
    Solver::Solver() : _step_num(15), _threshold(0.01f), _is_apply_constraint(false)
    {
    }
    Solver::~Solver()
    {
        for (auto& c: _constraints)
        {
            for (auto p : c)
                DESTORY_PTR(p);
        }
    }
    u16 Solver::Size() const
    {
        return (u16) _ik_chains.size();
    }
    void Solver::Resize(u16 new_size)
    {
        _ik_chains.resize(new_size);
        _constraints.resize(new_size);
    }
    void Solver::Clear()
    {
        _ik_chains.clear();
        for (auto &c: _constraints)
        {
            for (auto p: c)
                DESTORY_PTR(p);
        }
        _constraints.clear();
    }
    void Solver::AddConstraint(u32 index, IConstraint *c)
    {
        AL_ASSERT(index < _ik_chains.size());
        _constraints[index].emplace_back(c);
    }
    Vector<Vector<IConstraint *>> &Solver::GetConstraints()
    {
        return _constraints;
    }
    Transform &Solver::operator[](u32 index)
    {
        AL_ASSERT(index < _ik_chains.size());
        return _ik_chains[index];
    }
    Transform Solver::GetGlobalTransform(u32 index) const
    {
        AL_ASSERT(index < _ik_chains.size());
        u16 size = (u16) _ik_chains.size();
        Transform world = _ik_chains[index];
        for (i32 i = index - 1; i >= 0; --i)
        {
            world = Transform::Combine(_ik_chains[i], world);
        }
        return world;
    }
    u16 Solver::StepNum() const
    {
        return _step_num;
    }
    void Solver::StepNum(u16 new_step_num)
    {
        _step_num = new_step_num;
    }
    f32 Solver::Threshold() const
    {
        return _threshold;
    }
    void Solver::Threshold(f32 threshold)
    {
        _threshold = threshold;
    }
    bool Solver::Solve(const Transform &target)
    {
        AL_ASSERT_MSG(true,"Not impl");
        return true;
    }
    void Solver::IsApplyConstraint(bool is_apply)
    {
        _is_apply_constraint = is_apply;
    }
    bool Solver::IsApplyConstraint() const
    {
        return _is_apply_constraint;
    }
    //------------------------------------------------------------------------------------------------
    CCDSolver::CCDSolver() : Solver()
    {
    }
    bool CCDSolver::Solve(const Transform &target)
    {
        if (_ik_chains.empty())
            return false;

        bool apply = _is_apply_constraint && s_is_apply_constraint;
        u16 size = static_cast<u16>(_ik_chains.size());
        u16 last = size - 1;
        f32 threshold_sq = _threshold * _threshold;
        Vector3f goal = target._position;
        u16 iter_count = 0u;
        for (u16 step = 0; step < _step_num; ++step)
        {
            Vector3f effector = GetGlobalTransform(last)._position;
            if (SqrMagnitude(effector - goal) < threshold_sq)
                return true;

            // 从倒数第二个开始，旋转最后一个对其他关节没有影响
            for (i32 j = size - 2; j >= 0; --j)
            {
                if (iter_count >= s_global_step_num)
                    break;
                effector = GetGlobalTransform(last)._position;// 重新计算 effector
                Transform world = GetGlobalTransform(j);
                Vector3f pos = world._position;
                Quaternion rot = world._rotation;
                Vector3f to_effector = effector - pos;
                Vector3f to_goal = goal - pos;

                Quaternion effector_to_goal = Quaternion::FromTo(to_effector, to_goal);
                Quaternion world_rot = rot * effector_to_goal;
                Quaternion local_rot = world_rot * Quaternion::Inverse(rot);
                _ik_chains[j]._rotation = local_rot * _ik_chains[j]._rotation;
                _ik_chains[j]._rotation.NormalizeQ();// 归一化旋转

                if (apply)
                {
                    for (auto &c: _constraints[j])
                    {
                        Transform cur_lt = _ik_chains[j];
                        Transform parent_lt = (j == 0) ? Transform() : _ik_chains[j - 1];
                        c->Apply(cur_lt, parent_lt, _ik_chains[j]);
                    }
                }

                effector = GetGlobalTransform(last)._position;// 再次计算 effector
                if (SqrMagnitude(goal - effector) < threshold_sq)
                    return true;
                ++iter_count;
            }
        }
        return false;
    }


    //------------------------------------------------------------------------------------------------
    FABRIKSolver::FABRIKSolver() : Solver()
    {
    }
    void FABRIKSolver::Resize(u16 new_size)
    {
        Solver::Resize(new_size);
        _world_chains.resize(new_size);
        _lengths.resize(new_size);
    }
    void FABRIKSolver::Clear()
    {
        _world_chains.clear();
        _lengths.clear();
    }

    Transform FABRIKSolver::GetLocalTransform(u32 index) const
    {
        AL_ASSERT(index < _ik_chains.size());
        return _ik_chains[index];
    }
    void FABRIKSolver::SetLocalTransform(u32 index, const Transform &local)
    {
        AL_ASSERT(index < _ik_chains.size());
        _ik_chains[index] = local;
    }

    bool FABRIKSolver::Solve(const Transform &target)
    {
        if (_ik_chains.empty())
            return false;
        u16 size = (u16) _ik_chains.size();
        u16 last = size - 1;
        f32 threshold_sq = _threshold * _threshold;
        IKChainToWorld();
        Vector3f goal = target._position;
        Vector3f base = _world_chains[0];
        for (u16 i = 0; i < std::min(_step_num, s_global_step_num); ++i)
        {
            Vector3f effector = _world_chains[last];
            f32 sqr_len = SqrMagnitude(effector - goal);
            if (sqr_len < threshold_sq)
                break;
            IterateBackward(goal);
            IterateForward(base);
            WorldToIKChain();
            if (_is_apply_constraint)
                ConstrainIKChains();
            IKChainToWorld();
        }
        WorldToIKChain();
        if (_is_apply_constraint)
            ConstrainIKChains();
        Vector3f effector = GetGlobalTransform(last)._position;
        if (SqrMagnitude(effector - goal) < threshold_sq)
            return true;
        return false;
    }
    void FABRIKSolver::IterateForward(const Vector3f &base)
    {
        u16 size = (u16) _ik_chains.size();
        if (size > 0)
            _world_chains[0] = base;
        for (u16 i = 1; i < size; ++i)
        {
            Vector3f dir = Normalize(_world_chains[i] - _world_chains[i-1]);
            Vector3f offset = dir * _lengths[i];
            _world_chains[i] = _world_chains[i-1] + offset;
        }
    }
    void FABRIKSolver::IterateBackward(const Vector3f &goal)
    {
        u16 size = (u16) _ik_chains.size();
        if (size > 0)
            _world_chains[size-1] = goal;
        for (i32 i = size - 2; i >= 0;--i)
        {
            Vector3f dir = Normalize(_world_chains[i] - _world_chains[i+1]);
            Vector3f offset = dir * _lengths[i+1];
            _world_chains[i] = _world_chains[i+1] + offset;
        }
    }
    void FABRIKSolver::ConstrainIKChains()
    {
        bool apply = _is_apply_constraint && s_is_apply_constraint;
        if (!apply)
            return;
        u16 size = (u16)_ik_chains.size();
        for (i32 j = size - 1; j >= 0; --j)
        {
            for (auto &c: _constraints[j])
            {
                Transform cur_lt = _ik_chains[j];
                Transform parent_lt = j == 0 ? Transform() : _ik_chains[j - 1];
                c->Apply(cur_lt, parent_lt, _ik_chains[j]);
            }
        }
    }
    void FABRIKSolver::IKChainToWorld()
    {
        u16 size = (u16)_ik_chains.size();
        for (u16 i = 0; i < size; ++i)
        {
            _world_chains[i] = GetGlobalTransform(i)._position;
            if (i >= 1)
                _lengths[i] = Magnitude(_world_chains[i] - _world_chains[i - 1]);
        }
        if (size > 0)
            _lengths[0] = 0.0f;
    }
    void FABRIKSolver::WorldToIKChain()
    {
        if (_ik_chains.empty())
            return;
        u16 size = (u16)_ik_chains.size();
        for (u16 i = 0; i < size - 1; ++i)
        {
            Transform world = GetGlobalTransform(i);
            Transform next_world = GetGlobalTransform(i + 1);
            Vector3f pos = world._position;
            Quaternion rot = world._rotation;
            Vector3f to_next = next_world._position - pos;
            to_next = Quaternion::Inverse(rot) * to_next;
            Vector3f to_desired = _world_chains[i + 1] - pos;
            to_desired = Quaternion::Inverse(rot) * to_desired;
            _ik_chains[i]._rotation = Quaternion::FromTo(to_next, to_desired) * _ik_chains[i]._rotation;
        }
    }
}// namespace Ailu