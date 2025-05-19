#include "Animation/AnimationSystem.h"
#include "Animation/CrossFade.h"
#include "Animation/Solver.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ThreadPool.h"
#include "Scene/Component.h"
#include "pch.h"
#include <Render/Gizmo.h>


namespace Ailu
{
    using namespace Render;
    namespace
    {
        static void SkinTask(SkeletonMesh *skined_mesh, const Vector<Matrix4x4f> &pose_palette, Vector3f *vert, Vector3f *norm, u32 begin, u32 end)
        {
            auto bone_weights = skined_mesh->GetBoneWeights();
            auto bone_indices = skined_mesh->GetBoneIndices();
            auto vertices = skined_mesh->GetVertices();
            auto normals = skined_mesh->GetNormals();
            auto &sk = skined_mesh->GetSkeleton();
            for (u32 i = begin; i < end; i++)
            {
                Vector3f cur_pos = vertices[i], cur_normal = normals[i];
                auto &cur_weight = bone_weights[i];
                auto &cur_indices = bone_indices[i];
                Vector3f new_pos, new_normal;
                for (u16 j = 0; j < 4; j++)
                {
                    const auto &joint = sk[cur_indices[j]];
                    new_pos += cur_weight[j] * TransformCoord(pose_palette[cur_indices[j]], cur_pos);
                    //new_normal += cur_weight[j] * TransformNormal(pose_palette[cur_indices[j]], cur_normal);
                }
                vert[i] = new_pos;
                //norm[i] = new_normal;
            }
        }
    }// namespace
    namespace ECS
    {
        static Map<u32, Pose> s_cur_pose;
        static Map<u32, Vector<Matrix4x4f>> s_mat_palette;
        static Map<u32, CrossFadeController> s_cross_fade_controller;
        static Map<u32, CCDSolver> s_solver;
        //static Map<u32, f32> s_anim_playtime;
        Vector<std::future<void>> skin_tasks;
        void AnimationSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU(AnimationSystem_Update)
            skin_tasks.clear();
            for (auto e: _entities)
            {
                CSkeletonMesh* c = r.GetComponent<CSkeletonMesh>(e);
                if (!c->_p_mesh || !c->_anim_clip)
                    continue;
                u32 mesh_id = c->_p_mesh->ID();
                if (!s_cur_pose.contains(mesh_id))
                {
                    c->_anim_clip->IsLooping(true);
                    s_cur_pose[mesh_id] = c->_p_mesh->GetSkeleton().GetBindPose();
                    s_mat_palette[mesh_id].resize(c->_p_mesh->GetSkeleton().JointNum());
                    s_cross_fade_controller[mesh_id] = CrossFadeController(c->_p_mesh->GetSkeleton());
                    s_cross_fade_controller[mesh_id].Play(c->_anim_clip.get());
                    c->_blend_space = BlendSpace(&c->_p_mesh->GetSkeleton(), Vector2f(0.f,90.f), Vector2f(0.f, 90.f));
                    c->_blend_space.AddClip(c->_anim_clip.get(), 0.f, 0.f);
                    //CCDSolver *ccd = new CCDSolver();
                    //ccd->Resize(3);
                    //ccd->IsApplyConstraint(true);
                    //ccd->AddConstraint(0, new IKConstraint({-90.f, 120.f}, {-5.f, 5.f}, {-180.f, 180.f}));
                    //ccd->AddConstraint(1, new IKConstraint({-120.f, -5.f}, {-5.f, 5.f}, {-5.f, 5.f}));
                    //ccd->AddConstraint(2, new IKConstraint({-120.f, 120.f}, {-5.f, 5.f}, {-5.f, 5.f}));
                    //c->_p_mesh->GetSkeleton().AddSolver("left_leg", ccd);
                }
                if (c->_anim_clip.get() != s_cross_fade_controller[mesh_id].GetcurrentClip())
                    s_cross_fade_controller[mesh_id].Play(c->_anim_clip.get());
                u32 vert_count = c->_p_mesh->_vertex_count;
                f32 frame_duration = c->_anim_clip->Duration() / (f32) c->_anim_clip->FrameCount();
                f32 dt = delta_time * 0.001f * TimeMgr::s_time_scale;
                _anim_playtime[e] += dt;
                if (c->_blend_anim_clip)
                    s_cross_fade_controller[mesh_id].FadeTo(c->_blend_anim_clip.get(), 0.5f);
                f32 current_frame = c->_anim_time < 0.f ? _anim_playtime[e] : c->_anim_time;
                static f32 s_pre_skin_frame = current_frame;
                auto &sk = c->_p_mesh->GetSkeleton();
                auto transf = r.GetComponent<TransformComponent>(e);
                Matrix4x4f sk_to_world = sk[0]._node_inv_world_mat * transf->_transform._world_matrix;
                Matrix4x4f world_to_sk = MatrixInverse(sk_to_world);
                if (true)
                {
                    bool _is_do_skin = true;
                    {
                        PROFILE_BLOCK_CPU(AnimClipBake)
                        bool use_crossfade = false;
                        if (use_crossfade)
                        {
                            s_cross_fade_controller[mesh_id].Update(dt);
                            auto &cur_pose = s_cross_fade_controller[mesh_id].GetCurrentPose();
                            cur_pose.GetMatrixPalette(s_mat_palette[mesh_id]);
                        }
                        else
                        {
                            if (c->_anim_type == 0)
                            {
                                auto &cur_pose = s_cur_pose[mesh_id];
                                c->_anim_clip->Sample(cur_pose, current_frame);
                                cur_pose.GetMatrixPalette(s_mat_palette[mesh_id]);
                            }
                            else if (c->_anim_type == 1)
                            {
                                c->_blend_space.Update(dt);
                                c->_blend_space.GetCurrentPose().GetMatrixPalette(s_mat_palette[mesh_id]);
                                _is_do_skin = !s_mat_palette[mesh_id].empty();
                            }
                            else {};
                            //auto &cur_solver = *sk.GetSolvers().begin()->second;
                            //cur_solver[0] = cur_pose.GetGlobalTransform(66);
                            //cur_solver[1] = cur_pose.GetLocalTransform(67);
                            //cur_solver[2] = cur_pose.GetLocalTransform(68);
                            //Transform t = Solver::s_global_goal;
                            //Gizmo::DrawLine(TransformCoord(sk_to_world, cur_solver.GetGlobalTransform(cur_solver.Size() - 1)._position), t._position, Colors::kRed);
                            //t._position = TransformCoord(world_to_sk, t._position);
                            //cur_solver.Solve(t);
                            //{
                            //    auto parent_transf = cur_pose[cur_pose.GetParent(66)];
                            //    auto to_local = Transform::Inverse(parent_transf);
                            //    cur_pose.SetLocalTransform(66, Transform::Combine(to_local, cur_solver[0]));
                            //    cur_pose.SetLocalTransform(67, cur_solver[1]);
                            //    cur_pose.SetLocalTransform(68, cur_solver[2]);
                            //}

                        }
                        if (_is_do_skin)
                        {
                            for (auto &it: sk)
                                s_mat_palette[mesh_id][it._self] = it._inv_bind_pos * s_mat_palette[mesh_id][it._self] * it._node_inv_world_mat;
                        }

                    }
                    if (_is_do_skin)
                    {
                        auto vert = reinterpret_cast<Vector3f *>(c->_p_mesh->GetVertexBuffer()->GetStream(0));
                        auto normal = reinterpret_cast<Vector3f *>(c->_p_mesh->GetVertexBuffer()->GetStream(1));
                        PROFILE_BLOCK_CPU(Skin)
                        u16 batch_num = vert_count / s_vertex_num_per_skin_task + 1;
                        for (u16 i = 0; i < batch_num; i++)
                        {
                            u32 start_idx = i * s_vertex_num_per_skin_task;
                            u32 end_idx = std::min<u32>(start_idx + s_vertex_num_per_skin_task, vert_count);
                            skin_tasks.emplace_back(g_pThreadTool->Enqueue("AnimationSystem::Skin", SkinTask, c->_p_mesh.get(), s_mat_palette[mesh_id], vert, normal, start_idx, end_idx));
                        }
                    }
                }
                s_pre_skin_frame = current_frame;
                //debug
                auto &pose = s_cur_pose[mesh_id];
                for (u16 i = 0; i < pose.Size(); i++)
                {
                    u16 parent = pose.GetParent(i);
                    if (parent != Joint::kInvalidJointIndex)
                    {
                        Vector3f from = TransformCoord(sk_to_world, pose[i]._position);
                        Vector3f to = TransformCoord(sk_to_world, pose[parent]._position);
                        Gizmo::DrawLine(from, to, Random::RandomColor(i));
                    }
                }
            }
        }

        void AnimationSystem::OnPushEntity(Entity entity)
        {
            _anim_playtime[entity] = 0.f;
        }

        void AnimationSystem::WaitFor() const
        {
            u32 index = 0u;
            for (auto &future: skin_tasks)
            {
                CPUProfileBlock block(std::format("SkinTask_{}", index++));
                future.wait();
            }
        }

    }// namespace ECS
}// namespace Ailu
