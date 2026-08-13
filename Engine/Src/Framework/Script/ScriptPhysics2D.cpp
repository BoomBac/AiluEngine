#include "Framework/Script/ScriptPhysics2D.h"

#include "Physics/2D/Physics2D.h"
#include "Scene/Scene.h"

namespace Ailu
{
    Vector2f ScriptContact2D::GetPoint() const
    {
        return _point;
    }
    Vector2f ScriptContact2D::GetNormal() const
    {
        return _normal;
    }
    u32 ScriptContact2D::GetSelfShape() const
    {
        return _self_shape;
    }
    u32 ScriptContact2D::GetOtherShape() const
    {
        return _other_shape;
    }

    namespace
    {
        ScriptRaycastHit2D MakeScriptRaycastHit(SceneManagement::Scene &scene, const RaycastHit2D &hit)
        {
            return {{&scene, hit._entity}, hit._point, hit._normal, hit._distance};
        }
    }

    bool ScriptRaycastHit2D::IsValid() const
    {
        return _entity.IsValid();
    }
    ScriptEntity ScriptRaycastHit2D::GetEntity() const
    {
        return _entity;
    }
    Vector2f ScriptRaycastHit2D::GetPoint() const
    {
        return _point;
    }
    Vector2f ScriptRaycastHit2D::GetNormal() const
    {
        return _normal;
    }
    f32 ScriptRaycastHit2D::GetDistance() const
    {
        return _distance;
    }

    std::optional<ScriptRaycastHit2D> ScriptPhysics2D::Raycast(const Vector2f &origin, const Vector2f &direction,
                                                               f32 distance, u32 layer_mask)
    {
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr)
            return std::nullopt;

        Raycast2DDesc desc;
        desc._origin = origin;
        desc._direction = direction;
        desc._distance = distance;
        desc._filter._layer_mask = layer_mask;
        RaycastHit2D hit;
        if (!Physics2D::Raycast(*scene, desc, hit))
            return std::nullopt;
        return MakeScriptRaycastHit(*scene, hit);
    }

    Vector<ScriptRaycastHit2D> ScriptPhysics2D::OverlapCircle(const Vector2f &center, f32 radius, u32 layer_mask)
    {
        Vector<ScriptRaycastHit2D> result;
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr)
            return result;

        OverlapCircle2DDesc desc;
        desc._center = center;
        desc._radius = radius;
        desc._filter._layer_mask = layer_mask;
        Vector<OverlapHit2D> hits;
        Physics2D::OverlapCircle(*scene, desc, hits);
        result.reserve(hits.size());
        for (const OverlapHit2D &hit : hits)
            result.push_back(ScriptRaycastHit2D{ScriptEntity{scene, hit._entity}});
        return result;
    }

    Vector<ScriptRaycastHit2D> ScriptPhysics2D::OverlapBox(const Vector2f &center, const Vector2f &size, f32 rotation,
                                                           u32 layer_mask)
    {
        Vector<ScriptRaycastHit2D> result;
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr)
            return result;

        OverlapBox2DDesc desc;
        desc._center = center;
        desc._size = size;
        desc._rotation = rotation;
        desc._filter._layer_mask = layer_mask;
        Vector<OverlapHit2D> hits;
        Physics2D::OverlapBox(*scene, desc, hits);
        result.reserve(hits.size());
        for (const OverlapHit2D &hit : hits)
            result.push_back(ScriptRaycastHit2D{ScriptEntity{scene, hit._entity}});
        return result;
    }
}
