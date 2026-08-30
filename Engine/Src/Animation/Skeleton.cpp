#include "Animation/Skeleton.h"
#include "pch.h"

namespace Ailu
{
    // 构造函数
    Skeleton::Skeleton(const String &name) : _name(name) {};
    Skeleton::Skeleton() : Skeleton("Skeleton") {}
    Skeleton::~Skeleton() = default;
    // 获取关节索引
    i32 Skeleton::GetJointIndexByName(const Skeleton &sk, const String &name)
    {
        auto it = std::find_if(sk._joints.begin(), sk._joints.end(), [&](const auto &joint)
                               { return joint._name == name; });
        if (it != sk._joints.end())
            return static_cast<i32>(std::distance(sk._joints.begin(), it));
        else
            return -1;
    }

    // 返回关节数量
    const u32 Skeleton::JointNum() const
    {
        return static_cast<u32>(_joints.size());
    }

    // 添加关节
    void Skeleton::AddJoint(const Joint &joint)
    {
        Joint new_joint = joint;
        new_joint._self = static_cast<u16>(_joints.size());
        _joints.push_back(std::move(new_joint));
        if (_bind_pose.Size() != _joints.size())
            _bind_pose.Resize((u32)_joints.size());
        _bind_pose.SetParent(_joints.back()._self, _joints.back()._parent);
    }

    void Skeleton::SetBindPoseLocalTransform(u32 index, const Transform &transform)
    {
        if (index >= _bind_pose.Size())
            return;
        _bind_pose.SetLocalTransform(index, transform);
    }

    void Skeleton::GetBindMatrixPalette(Vector<Matrix4x4f> &out) const
    {
        out.resize(_joints.size());
        for (const Joint &joint : _joints)
            out[joint._self] = Math::MatrixInverse(joint._inv_bind_pos);
    }

    // 根据索引获取关节
    Joint &Skeleton::GetJoint(u32 index)
    {
        return _joints[index];
    }

    // 根据名字获取关节
    Joint &Skeleton::GetJoint(const String &name)
    {
        for (auto &joint: _joints)
        {
            if (joint._name == name)
                return joint;
        }
        return _joints[0];
    }

    // 清空关节列表
    void Skeleton::Clear()
    {
        _joints.clear();
        _bind_pose.Resize(0u);
    }
    void Skeleton::Rebuild()
    {
        _bind_pose.Resize(static_cast<u32>(_joints.size()));
        for (u16 index = 0u; index < _joints.size(); ++index)
        {
            Joint &joint = _joints[index];
            joint._self = index;
            if (joint._parent >= _joints.size())
                joint._parent = Joint::kInvalidJointIndex;
            joint._children.clear();
            _bind_pose.SetParent(index, joint._parent);
        }
        for (const Joint &joint : _joints)
        {
            if (joint._parent < _joints.size())
                _joints[joint._parent]._children.emplace_back(joint._self);
        }
    }
    const Pose &Skeleton::GetBindPose() const
    {
        return _bind_pose;
    }
    const Pose &Skeleton::GetRestPose() const
    {
        return _bind_pose;
    }

    // 重载比较运算符
    const bool Skeleton::operator==(const Skeleton &other) const
    {
        if (_joints.size() != other._joints.size())
            return false;
        for (size_t i = 0; i < _joints.size(); ++i)
        {
            const Joint &joint1 = _joints[i];
            const Joint &joint2 = other._joints[i];

            if (joint1._name != joint2._name || joint1._parent != joint2._parent)
            {
                return false;
            }
        }
        return true;
    }

    // 重载下标运算符
    Joint &Skeleton::operator[](u32 index)
    {
        return _joints[index];
    }

    const Joint &Skeleton::operator[](u32 index) const
    {
        return _joints[index];
    }

    Archive &operator<<(Archive &ar, const Joint &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_name:" << c._name << std::endl;
        ar.InsertIndent();
        ar << "_parent:" << c._parent << std::endl;
        ar.InsertIndent();
        ar << "_self:" << c._self << std::endl;
        ar.InsertIndent();
        ar << "_inv_bind_pos:" << c._inv_bind_pos.ToString() << std::endl;
        ar.DecreaseIndent();
        //ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, Joint &c)
    {
        Array<String, 4> bufs;
        Map<String, String> kvs;
        for (u16 i = 0; i < bufs.size(); i++)
        {
            ar >> bufs[i];
            auto str_list = su::Split(bufs[i], ":");
            kvs[str_list[0]] = str_list[1];
        }
        AL_ASSERT(su::BeginWith(bufs[0], "_name"));
        c._name = kvs["_name"];
        c._parent = (u16)std::stoul(kvs["_parent"]);
        c._self = (u16)std::stoul(kvs["_self"]);
        c._inv_bind_pos.FromString(kvs["_inv_bind_pos"]);
        return ar;
    }
    Archive &operator<<(Archive &ar, const Skeleton &sk)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_name:" << sk._name << std::endl;
        ar.InsertIndent();
        ar << "_joint_num:" << sk.JointNum() << std::endl;
        ar.InsertIndent();
        ar << "_joints:" << std::endl;
        for (u16 i = 0; i < sk._joints.size(); ++i)
        {
            ar << sk._joints[i];
            if (i + 1u < sk._joints.size())
                ar.NewLine();
        }
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, Skeleton &sk)
    {
        Array<String, 3> bufs;
        Map<String, String> kvs;
        for (u16 i = 0; i < bufs.size(); i++)
        {
            ar >> bufs[i];
            auto str_list = su::Split(bufs[i], ":");
            kvs[str_list[0]] = str_list[1];
        }
        AL_ASSERT(su::BeginWith(bufs[0], "_name"));
        sk._name = kvs["_name"];
        sk._joints.resize(std::stoul(kvs["_joint_num"]));
        AL_ASSERT(su::BeginWith(bufs[2], "_joints"));
        for (auto &j: sk._joints)
            ar >> j;
        sk.Rebuild();
        return ar;
    }

}// namespace Ailu
