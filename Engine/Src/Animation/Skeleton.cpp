#include "Animation/Skeleton.h"
#include "pch.h"

namespace Ailu
{
    // 构造函数
    Skeleton::Skeleton(const String &name) : _name(name) {};
    Skeleton::Skeleton() : Skeleton("Skeleton") {}
    Skeleton::~Skeleton()
    {
        for (auto &it: _solvers)
            DESTORY_PTR(it.second);
    }
    Map<String, Solver *> &Skeleton::GetSolvers()
    {
        return _solvers;
    }
    void Skeleton::AddSolver(const String &name, Solver *solver)
    {
        if (!_solvers.contains(name))
            _solvers[name] = solver;
    }
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
        _joints.push_back(joint);
        if (_bind_pose.Size() != _joints.size())
            _bind_pose.Resize(_joints.size());
        _bind_pose.SetParent(joint._self, joint._parent);
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
        ar.InsertIndent();
        ar << "_node_inv_world_mat:" << c._node_inv_world_mat.ToString();
        ar.DecreaseIndent();
        //ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, Joint &c)
    {
        Array<String, 5> bufs;
        Map<String, String> kvs;
        for (u16 i = 0; i < bufs.size(); i++)
        {
            ar >> bufs[i];
            auto str_list = su::Split(bufs[i], ":");
            kvs[str_list[0]] = str_list[1];
        }
        AL_ASSERT(su::BeginWith(bufs[0], "_name"));
        c._name = kvs["_name"];
        c._parent = std::stoul(kvs["_parent"]);
        c._self = std::stoul(kvs["_self"]);
        c._inv_bind_pos.FromString(kvs["_inv_bind_pos"]);
        c._node_inv_world_mat.FromString(kvs["_node_inv_world_mat"]);
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
        for (u16 i = 0; i < sk._joints.size() - 1; i++)
        {
            ar << sk._joints[i];
            ar.NewLine();
        }
        ar << sk._joints.back();
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
        return ar;
    }

}// namespace Ailu
