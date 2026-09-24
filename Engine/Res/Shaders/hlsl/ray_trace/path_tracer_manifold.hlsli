#ifndef PATH_TRACER_MANIFOLD_HLSLI
#define PATH_TRACER_MANIFOLD_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_light_common.hlsli"
#include "path_tracer_material.hlsli"

static const uint kMaxManifoldVertices = 2u;
static const uint kMaxManifoldIterations = 8u;
static const float kManifoldConstraintEpsilon = 1e-4f;
static const float kManifoldFiniteDifferenceEpsilon = 1e-3f;

struct ManifoldTriangle
{
    float3 _v0;
    float3 _v1;
    float3 _v2;
};

struct ManifoldVertex
{
    float2 _surface_param;
    uint _primitive_index;
    uint _triangle_index;
    uint _material_index;
    uint _front_face;
    float _ior_i;
    float _ior_t;
    float _transmission;
    float3 _sigma_a;
};

struct ManifoldPath
{
    ManifoldVertex _vertices[kMaxManifoldVertices];
    uint _vertex_count;
    bool _valid;
    float _jacobian;
    float3 _throughput;
    float _event_pdf;
};

bool LoadPathTriangle(uint primitive_index, uint triangle_index, out ManifoldTriangle triangle_data);
PathHit TracePathSegment(float3 ray_origin, float3 ray_direction, float max_t);

ManifoldTriangle LoadPathTriangle(uint primitive_index, uint triangle_index)
{
    ManifoldTriangle triangle_data = (ManifoldTriangle)0;
    LoadPathTriangle(primitive_index, triangle_index, triangle_data);
    return triangle_data;
}

float MneeConstraintError(float4 constraints, uint dimension)
{
    float error = 0.0f;
    [loop]
    for (uint index = 0u; index < dimension; ++index)
        error = max(error, abs(constraints[index]));
    return error;
}

bool MneeIsFinite(float value)
{
    return value == value && abs(value) < 1e30f;
}

bool IsManifoldBarycentricValid(float2 barycentrics)
{
    return all(barycentrics >= 0.0.xx) && barycentrics.x + barycentrics.y <= 1.0f;
}

float3 EvaluateManifoldPosition(ManifoldTriangle triangle_data, float2 barycentrics)
{
    return triangle_data._v0 + barycentrics.x * (triangle_data._v1 - triangle_data._v0) +
           barycentrics.y * (triangle_data._v2 - triangle_data._v0);
}

float3 EvaluateManifoldNormal(ManifoldTriangle triangle_data, uint front_face)
{
    float3 normal = normalize(cross(triangle_data._v1 - triangle_data._v0, triangle_data._v2 - triangle_data._v0));
    return front_face != 0u ? normal : -normal;
}

bool EvaluateManifoldConstraints(ManifoldPath path, float3 receiver_position, float3 light_position,
                                 float4 variables, out float4 constraints)
{
    constraints = 0.0.xxxx;
    if (path._vertex_count == 0u || path._vertex_count > kMaxManifoldVertices)
        return false;

    ManifoldTriangle triangles[kMaxManifoldVertices];
    float3 positions[kMaxManifoldVertices];
    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        const ManifoldVertex vertex = path._vertices[index];
        // Variables are packed as (u0, v0, u1, v1).
        const float2 surface_param = float2(variables[index * 2u], variables[index * 2u + 1u]);
        if (!IsManifoldBarycentricValid(surface_param) ||
            !LoadPathTriangle(vertex._primitive_index, vertex._triangle_index, triangles[index]))
            return false;
        positions[index] = EvaluateManifoldPosition(triangles[index], surface_param);
    }

    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        const ManifoldVertex vertex = path._vertices[index];
        const float3 position = positions[index];
        const uint previous_index = index == 0u ? 0u : index - 1u;
        const float3 previous = index == 0u ? receiver_position : positions[previous_index];
        const uint next_index = min(index + 1u, path._vertex_count - 1u);
        const float3 next = index + 1u == path._vertex_count ? light_position : positions[next_index];
        const float3 to_previous = previous - position;
        const float3 to_next = next - position;
        if (dot(to_previous, to_previous) <= 1e-12f || dot(to_next, to_next) <= 1e-12f)
            return false;

        const float3 wo = normalize(to_previous);
        const float3 wi = normalize(to_next);
        const float3 normal = EvaluateManifoldNormal(triangles[index], vertex._front_face);
        if (dot(wo, normal) <= 1e-5f || dot(wi, normal) >= -1e-5f)
            return false;
        const float3 transmitted = refract(-wo, normal, vertex._ior_i / max(vertex._ior_t, 1e-6f));
        if (dot(transmitted, transmitted) <= 1e-12f)
            return false;

        const float3 half_vector = BuildTransmissionHalfVector(wo, wi, vertex._ior_i, vertex._ior_t);
        if (all(half_vector == 0.0.xxx) || dot(half_vector, normal) <= 0.0f)
            return false;
        float3 tangent;
        float3 bitangent;
        build_onb(normal, tangent, bitangent);
        constraints[index * 2u] = dot(half_vector, tangent);
        constraints[index * 2u + 1u] = dot(half_vector, bitangent);
    }
    return true;
}

bool SolveManifoldLinearSystem(inout float4x4 matrix, inout float4 rhs, uint dimension, out float4 solution)
{
    solution = 0.0.xxxx;
    [loop]
    for (uint column = 0u; column < dimension; ++column)
    {
        uint pivot_row = column;
        float pivot_size = abs(matrix[column][column]);
        [loop]
        for (uint row = column + 1u; row < dimension; ++row)
        {
            const float candidate_size = abs(matrix[row][column]);
            if (candidate_size > pivot_size)
            {
                pivot_size = candidate_size;
                pivot_row = row;
            }
        }
        if (!MneeIsFinite(pivot_size) || pivot_size <= 1e-8f)
            return false;

        if (pivot_row != column)
        {
            const float4 matrix_row = matrix[column];
            matrix[column] = matrix[pivot_row];
            matrix[pivot_row] = matrix_row;
            const float rhs_value = rhs[column];
            rhs[column] = rhs[pivot_row];
            rhs[pivot_row] = rhs_value;
        }

        const float inverse_pivot = rcp(matrix[column][column]);
        matrix[column] *= inverse_pivot;
        rhs[column] *= inverse_pivot;
        [loop]
        for (uint row = 0u; row < dimension; ++row)
        {
            if (row == column)
                continue;
            const float factor = matrix[row][column];
            matrix[row] -= matrix[column] * factor;
            rhs[row] -= rhs[column] * factor;
        }
    }

    solution = rhs;
    return all(solution == solution) && all(abs(solution) < 1e20f);
}

bool SolveManifoldPath(inout ManifoldPath path, float3 receiver_position, float3 light_position)
{
    path._valid = false;
    path._jacobian = 0.0f;
    path._throughput = 1.0.xxx;
    path._event_pdf = 1.0f;
    if (path._vertex_count == 0u || path._vertex_count > kMaxManifoldVertices)
        return false;

    const uint dimension = path._vertex_count * 2u;
    float4 variables = 0.0.xxxx;
    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        variables[index * 2u] = path._vertices[index]._surface_param.x;
        variables[index * 2u + 1u] = path._vertices[index]._surface_param.y;
    }

    float4 constraints;
    if (!EvaluateManifoldConstraints(path, receiver_position, light_position, variables, constraints))
        return false;

    float error = MneeConstraintError(constraints, dimension);
    [loop]
    for (uint iteration = 0u; iteration < kMaxManifoldIterations; ++iteration)
    {
        if (error < kManifoldConstraintEpsilon)
        {
            path._valid = true;
            break;
        }

        float4x4 jacobian = (float4x4)0;
        [loop]
        for (uint column = 0u; column < dimension; ++column)
        {
            float4 plus_variables = variables;
            float4 minus_variables = variables;
            plus_variables[column] += kManifoldFiniteDifferenceEpsilon;
            minus_variables[column] -= kManifoldFiniteDifferenceEpsilon;
            float4 plus_constraints;
            float4 minus_constraints;
            if (!EvaluateManifoldConstraints(path, receiver_position, light_position, plus_variables,
                                             plus_constraints) ||
                !EvaluateManifoldConstraints(path, receiver_position, light_position, minus_variables,
                                             minus_constraints))
                return false;
            const float4 derivative = (plus_constraints - minus_constraints) /
                                      (2.0f * kManifoldFiniteDifferenceEpsilon);
            [loop]
            for (uint row = 0u; row < dimension; ++row)
                jacobian[row][column] = derivative[row];
        }

        float4 step;
        float4 rhs = -constraints;
        if (!SolveManifoldLinearSystem(jacobian, rhs, dimension, step))
            return false;

        bool accepted = false;
        float step_scale = 1.0f;
        [loop]
        for (uint backtrack = 0u; backtrack < 6u; ++backtrack)
        {
            const float4 candidate = variables + step_scale * step;
            float4 candidate_constraints;
            if (EvaluateManifoldConstraints(path, receiver_position, light_position, candidate,
                                            candidate_constraints))
            {
                const float candidate_error = MneeConstraintError(candidate_constraints, dimension);
                if (candidate_error < error)
                {
                    variables = candidate;
                    constraints = candidate_constraints;
                    error = candidate_error;
                    accepted = true;
                    break;
                }
            }
            step_scale *= 0.5f;
        }
        if (!accepted)
            return false;
    }

    if (!path._valid && error < kManifoldConstraintEpsilon)
        path._valid = true;
    if (!path._valid)
        return false;

    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        path._vertices[index]._surface_param = float2(variables[index * 2u], variables[index * 2u + 1u]);
    }
    return true;
}

bool AppendManifoldVertex(inout ManifoldPath path, PathHit hit)
{
    if (path._vertex_count >= kMaxManifoldVertices || hit._type != kPathHitSurface ||
        !hit._material._is_smooth_dielectric)
        return false;

    const uint index = path._vertex_count++;
    ManifoldVertex vertex = (ManifoldVertex)0;
    vertex._surface_param = hit._barycentrics;
    vertex._primitive_index = hit._primitive_index;
    vertex._triangle_index = hit._triangle_index;
    vertex._material_index = hit._material_index;
    vertex._front_face = hit._front_face;
    const TraceContext trace_context = CreatePathTraceContext(hit._material, hit._front_face != 0u);
    vertex._ior_i = trace_context._ior_i;
    vertex._ior_t = trace_context._ior_t;
    vertex._transmission = hit._material._transmission;
    vertex._sigma_a = hit._material._sigma_a;
    path._vertices[index] = vertex;
    return true;
}

bool IsSupportedManifoldMedium(ManifoldPath path)
{
    if (path._vertex_count == 1u)
        return path._vertices[0]._front_face != 0u && path._vertices[0]._ior_i <= 1.0001f;
    if (path._vertex_count != 2u)
        return false;

    const ManifoldVertex entry = path._vertices[0];
    const ManifoldVertex exit = path._vertices[1];
    return entry._front_face != 0u && exit._front_face == 0u &&
           entry._material_index == exit._material_index &&
           abs(entry._ior_t - exit._ior_i) < 1e-3f && abs(exit._ior_t - 1.0f) < 1e-3f &&
           all(abs(entry._sigma_a - exit._sigma_a) < 1e-4.xxx);
}

bool TraceManifoldSeed(float3 receiver_position, float3 receiver_normal, float3 light_position,
                       PathHit first_hit, inout ManifoldPath path)
{
    path = (ManifoldPath)0;
    if (first_hit._type != kPathHitSurface || !first_hit._material._is_smooth_dielectric ||
        first_hit._front_face == 0u)
        return false;
    const float3 seed_direction_delta = light_position - receiver_position;
    const float seed_distance = length(seed_direction_delta);
    if (seed_distance <= 2e-3f)
        return false;

    const float3 seed_direction = seed_direction_delta / seed_distance;
    float3 ray_origin = OffsetPathRayOrigin(receiver_position, receiver_normal, seed_direction, 0.0f);
    float remaining_distance = dot(light_position - ray_origin, seed_direction);
    bool reached_light = false;
    PathHit hit = first_hit;
    [loop]
    for (uint step = 0u; step <= kMaxManifoldVertices; ++step)
    {
        if (step > 0u)
        {
            const float endpoint_margin = max(1e-4f, remaining_distance * 1e-5f);
            if (remaining_distance <= endpoint_margin + 1e-3f)
            {
                reached_light = true;
                break;
            }

            hit = TracePathSegment(ray_origin, seed_direction, remaining_distance - endpoint_margin);
            if (hit._type == kPathHitMiss)
            {
                reached_light = true;
                break;
            }
        }
        if (!AppendManifoldVertex(path, hit))
            return false;

        const ManifoldTriangle triangle_data = LoadPathTriangle(hit._primitive_index, hit._triangle_index);
        const float3 normal = EvaluateManifoldNormal(triangle_data, hit._front_face);
        ray_origin = OffsetPathRayOrigin(hit._world_position, normal, seed_direction, hit._hit_t);
        remaining_distance = dot(light_position - ray_origin, seed_direction);
    }

    return reached_light && path._vertex_count > 0u && IsSupportedManifoldMedium(path);
}

bool IsManifoldPathVisible(ManifoldPath path, float3 receiver_position, float3 receiver_normal,
                           float3 light_position)
{
    float3 segment_start = receiver_position;
    float3 start_normal = receiver_normal;
    [unroll]
    for (uint index = 0u; index <= kMaxManifoldVertices; ++index)
    {
        const bool is_light_endpoint = index == path._vertex_count;
        if (index > path._vertex_count)
            break;
        float3 segment_end = light_position;
        float3 end_normal = 0.0.xxx;
        if (!is_light_endpoint)
        {
            const uint vertex_index = min(index, kMaxManifoldVertices - 1u);
            const ManifoldVertex vertex = path._vertices[vertex_index];
            const ManifoldTriangle triangle_data = LoadPathTriangle(vertex._primitive_index, vertex._triangle_index);
            segment_end = EvaluateManifoldPosition(triangle_data, vertex._surface_param);
            end_normal = EvaluateManifoldNormal(triangle_data, vertex._front_face);
        }

        const float3 delta = segment_end - segment_start;
        const float distance_squared = dot(delta, delta);
        if (distance_squared <= 1e-10f)
            return false;
        const float distance = sqrt(distance_squared);
        const float3 direction = delta / distance;
        const float3 origin = OffsetPathRayOrigin(segment_start, start_normal, direction, 0.0f);
        const float max_t = dot(segment_end - origin, direction) - max(1e-4f, distance * 1e-5f);
        if (max_t <= 1e-3f)
            return false;
        const PathHit blocker = TracePathSegment(origin, direction, max_t);
        if (blocker._type != kPathHitMiss)
            return false;

        segment_start = segment_end;
        start_normal = end_normal;
    }
    return true;
}

bool IsMneeLightPointValid(SampleLightData light, float3 position)
{
    if (light._type == LIGHT_TYPE_RECT_AREA)
    {
        const float3 base = light._position - 0.5f * light._light_u - 0.5f * light._light_v;
        const float uu = dot(light._light_u, light._light_u);
        const float vv = dot(light._light_v, light._light_v);
        const float uv = dot(light._light_u, light._light_v);
        const float3 rel = position - base;
        const float ru = dot(rel, light._light_u);
        const float rv = dot(rel, light._light_v);
        const float det = uu * vv - uv * uv;
        if (abs(det) <= 1e-10f)
            return false;
        const float a = (ru * vv - rv * uv) / det;
        const float b = (rv * uu - ru * uv) / det;
        return a >= 0.0f && a <= 1.0f && b >= 0.0f && b <= 1.0f;
    }
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        const TriangleData triangle_data = LoadTriangleAreaLightData(light);
        const float3 e1 = triangle_data.v1 - triangle_data.v0;
        const float3 e2 = triangle_data.v2 - triangle_data.v0;
        const float3 rel = position - triangle_data.v0;
        const float d00 = dot(e1, e1);
        const float d01 = dot(e1, e2);
        const float d11 = dot(e2, e2);
        const float d20 = dot(rel, e1);
        const float d21 = dot(rel, e2);
        const float det = d00 * d11 - d01 * d01;
        if (abs(det) <= 1e-10f)
            return false;
        const float u = (d11 * d20 - d01 * d21) / det;
        const float v = (d00 * d21 - d01 * d20) / det;
        return IsManifoldBarycentricValid(float2(u, v));
    }
    if (light._type == LIGHT_TYPE_POINT || light._type == LIGHT_TYPE_SPOT)
    {
        const float radius = light._radius_or_sun_angle;
        return radius > 1e-6f && abs(distance(position, light._position) - radius) <= max(1e-3f, radius * 1e-3f);
    }
    return false;
}

float3 PerturbMneeLightPoint(SampleLightData light, float3 position, float3 tangent_u, float3 tangent_v,
                             float u, float v)
{
    const float3 perturbed = position + u * tangent_u + v * tangent_v;
    if (light._type == LIGHT_TYPE_POINT || light._type == LIGHT_TYPE_SPOT)
        return light._position + normalize(perturbed - light._position) * light._radius_or_sun_angle;
    return perturbed;
}

bool ComputeManifoldSolidAngleJacobian(ManifoldPath path, float3 receiver_position, float3 light_position,
                                       float3 tangent_u, float3 tangent_v, SampleLightData light,
                                       out float jacobian)
{
    jacobian = 0.0f;
    const float3 seed_direction = normalize(EvaluateManifoldPosition(
        LoadPathTriangle(path._vertices[0]._primitive_index, path._vertices[0]._triangle_index),
        path._vertices[0]._surface_param) - receiver_position);
    const float epsilon = max(1e-4f, distance(receiver_position, light_position) * 1e-5f);
    const float3 light_u = PerturbMneeLightPoint(light, light_position, tangent_u, tangent_v, epsilon, 0.0f);
    const float3 light_v = PerturbMneeLightPoint(light, light_position, tangent_u, tangent_v, 0.0f, epsilon);
    const float3 light_minus_u = PerturbMneeLightPoint(light, light_position, tangent_u, tangent_v, -epsilon, 0.0f);
    const float3 light_minus_v = PerturbMneeLightPoint(light, light_position, tangent_u, tangent_v, 0.0f, -epsilon);
    if (!IsMneeLightPointValid(light, light_u) || !IsMneeLightPointValid(light, light_v) ||
        !IsMneeLightPointValid(light, light_minus_u) || !IsMneeLightPointValid(light, light_minus_v))
        return false;

    ManifoldPath plus_u_path = path;
    ManifoldPath minus_u_path = path;
    ManifoldPath plus_v_path = path;
    ManifoldPath minus_v_path = path;
    if (!SolveManifoldPath(plus_u_path, receiver_position, light_u) ||
        !SolveManifoldPath(minus_u_path, receiver_position, light_minus_u) ||
        !SolveManifoldPath(plus_v_path, receiver_position, light_v) ||
        !SolveManifoldPath(minus_v_path, receiver_position, light_minus_v))
        return false;

    const float3 direction_plus_u = normalize(EvaluateManifoldPosition(
        LoadPathTriangle(plus_u_path._vertices[0]._primitive_index, plus_u_path._vertices[0]._triangle_index),
        plus_u_path._vertices[0]._surface_param) - receiver_position);
    const float3 direction_minus_u = normalize(EvaluateManifoldPosition(
        LoadPathTriangle(minus_u_path._vertices[0]._primitive_index, minus_u_path._vertices[0]._triangle_index),
        minus_u_path._vertices[0]._surface_param) - receiver_position);
    const float3 direction_plus_v = normalize(EvaluateManifoldPosition(
        LoadPathTriangle(plus_v_path._vertices[0]._primitive_index, plus_v_path._vertices[0]._triangle_index),
        plus_v_path._vertices[0]._surface_param) - receiver_position);
    const float3 direction_minus_v = normalize(EvaluateManifoldPosition(
        LoadPathTriangle(minus_v_path._vertices[0]._primitive_index, minus_v_path._vertices[0]._triangle_index),
        minus_v_path._vertices[0]._surface_param) - receiver_position);
    const float3 derivative_u = (direction_plus_u - direction_minus_u) / (2.0f * epsilon);
    const float3 derivative_v = (direction_plus_v - direction_minus_v) / (2.0f * epsilon);
    jacobian = abs(dot(seed_direction, cross(derivative_u, derivative_v)));
    return MneeIsFinite(jacobian) && jacobian > 1e-12f;
}

float GetUnifiedLightAreaPdf(uint light_index, float3 reference_position, float3 light_position)
{
    const SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_RECT_AREA)
        return rcp(max(length(cross(light._light_u, light._light_v)), 1e-6f));
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        const TriangleData triangle_data = LoadTriangleAreaLightData(light);
        const float area = 0.5f * length(cross(triangle_data.v1 - triangle_data.v0, triangle_data.v2 - triangle_data.v0));
        return rcp(max(area, 1e-6f));
    }
    if (light._type == LIGHT_TYPE_POINT || light._type == LIGHT_TYPE_SPOT)
    {
        const float3 direction = normalize(light_position - reference_position);
        const float distance_squared = dot(light_position - reference_position, light_position - reference_position);
        const float3 normal = normalize(light_position - light._position);
        const float pdf_omega = PdfPointLight(light, reference_position, direction);
        return pdf_omega * abs(dot(normal, -direction)) / max(distance_squared, 1e-6f);
    }
    return 0.0f;
}

bool GetManifoldLightFrame(uint light_index, float3 sample_position, out SampleLightData light,
                           out float3 tangent_u, out float3 tangent_v)
{
    light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    tangent_u = 0.0.xxx;
    tangent_v = 0.0.xxx;
    if (!IsMneeLightPointValid(light, sample_position))
        return false;

    if (light._type == LIGHT_TYPE_RECT_AREA)
    {
        tangent_u = normalize(light._light_u);
        const float3 perpendicular_v = light._light_v - dot(light._light_v, tangent_u) * tangent_u;
        if (dot(perpendicular_v, perpendicular_v) <= 1e-12f)
            return false;
        tangent_v = normalize(perpendicular_v);
        return true;
    }
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        const TriangleData triangle_data = LoadTriangleAreaLightData(light);
        tangent_u = normalize(triangle_data.v1 - triangle_data.v0);
        const float3 perpendicular_v = triangle_data.v2 - triangle_data.v0 -
                                        dot(triangle_data.v2 - triangle_data.v0, tangent_u) * tangent_u;
        if (dot(perpendicular_v, perpendicular_v) <= 1e-12f)
            return false;
        tangent_v = normalize(perpendicular_v);
        return true;
    }
    if ((light._type == LIGHT_TYPE_POINT || light._type == LIGHT_TYPE_SPOT) && light._radius_or_sun_angle > 1e-6f)
    {
        build_onb(normalize(sample_position - light._position), tangent_u, tangent_v);
        return true;
    }
    return false;
}

bool EvaluateManifoldTransport(ManifoldPath path, float3 receiver_position, float3 light_position,
                               out float3 throughput, out float event_pdf)
{
    throughput = 1.0.xxx;
    event_pdf = 1.0f;
    if (!IsSupportedManifoldMedium(path))
        return false;

    float3 positions[kMaxManifoldVertices];
    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        const ManifoldVertex vertex = path._vertices[index];
        const ManifoldTriangle triangle_data = LoadPathTriangle(vertex._primitive_index, vertex._triangle_index);
        positions[index] = EvaluateManifoldPosition(triangle_data, vertex._surface_param);
    }

    [unroll]
    for (uint index = 0u; index < kMaxManifoldVertices; ++index)
    {
        if (index >= path._vertex_count)
            break;
        const ManifoldVertex vertex = path._vertices[index];
        const uint previous_index = index == 0u ? 0u : index - 1u;
        const float3 previous = index == 0u ? receiver_position : positions[previous_index];
        const uint next_index = min(index + 1u, path._vertex_count - 1u);
        const float3 next = index + 1u == path._vertex_count ? light_position : positions[next_index];
        const float3 wo = normalize(previous - positions[index]);
        const float3 wi = normalize(next - positions[index]);
        const ManifoldTriangle triangle_data = LoadPathTriangle(vertex._primitive_index, vertex._triangle_index);
        const float3 normal = EvaluateManifoldNormal(triangle_data, vertex._front_face);
        const float fresnel = FresnelDielectric(dot(wo, normal), vertex._ior_i, vertex._ior_t);
        const float3 refracted = refract(-wo, normal, vertex._ior_i / max(vertex._ior_t, 1e-6f));
        if (dot(refracted, refracted) <= 1e-12f || dot(normalize(refracted), wi) < 1.0f - 1e-4f)
            return false;

        const float transmission_probability = vertex._transmission * (1.0f - fresnel);
        const float eta_i_over_t = vertex._ior_i / max(vertex._ior_t, 1e-6f);
        // Match the rough BTDF and smooth continuation radiance transport convention.
        throughput *= transmission_probability * eta_i_over_t * eta_i_over_t;
        event_pdf *= transmission_probability;
    }

    if (path._vertex_count == 1u)
        throughput *= ApplyMediumAbsorption(path._vertices[0]._sigma_a,
                                            distance(positions[0], light_position));
    else
        throughput *= ApplyMediumAbsorption(path._vertices[0]._sigma_a, distance(positions[0], positions[1]));
    return all(throughput == throughput) && all(abs(throughput) < 1e20f) && MneeIsFinite(event_pdf);
}

float3 EvaluateManifoldLightRadiance(uint light_index, ManifoldPath path, float3 light_position,
                                     float2 light_uv)
{
    const SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_DIRECTIONAL)
        return 0.0.xxx;

    const ManifoldVertex last_vertex = path._vertices[path._vertex_count - 1u];
    const ManifoldTriangle triangle_data = LoadPathTriangle(last_vertex._primitive_index, last_vertex._triangle_index);
    const float3 last_position = EvaluateManifoldPosition(triangle_data, last_vertex._surface_param);
    const float3 last_direction = normalize(light_position - last_position);
    if (light._type == LIGHT_TYPE_RECT_AREA || light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        const float3 normal = light._type == LIGHT_TYPE_RECT_AREA
            ? -normalize(cross(light._light_u, light._light_v))
            : GetTriangleAreaLightNormal(LoadTriangleAreaLightData(light));
        const float cos_light = dot(normal, -last_direction);
        if (!light._is_double_sided && cos_light <= 0.0f)
            return 0.0.xxx;
    }
    return EvaluateFiniteLightRadiance(light, last_position, light_uv);
}

float3 EvaluateManifoldDirectLighting(PathHit hit, TraceContext trace_context, float3 wo, uint light_index,
                                      float light_selection_pmf, LightSample light_sample, PathHit first_hit)
{
    if (light_selection_pmf <= 0.0f || light_sample._pdf_area <= 0.0f ||
        hit._material._is_smooth_dielectric && hit._material._transmission >= 1.0f)
        return 0.0.xxx;

    SampleLightData light;
    float3 tangent_u;
    float3 tangent_v;
    if (!GetManifoldLightFrame(light_index, light_sample._position, light, tangent_u, tangent_v))
        return 0.0.xxx;

    ManifoldPath path;
    if (!TraceManifoldSeed(hit._world_position, hit._geometric_normal, light_sample._position, first_hit, path) ||
        !SolveManifoldPath(path, hit._world_position, light_sample._position) ||
        !IsManifoldPathVisible(path, hit._world_position, hit._geometric_normal, light_sample._position))
        return 0.0.xxx;

    float jacobian;
    if (!ComputeManifoldSolidAngleJacobian(path, hit._world_position, light_sample._position,
                                           tangent_u, tangent_v, light, jacobian))
        return 0.0.xxx;
    path._jacobian = jacobian;

    float3 manifold_throughput;
    float event_pdf;
    if (!EvaluateManifoldTransport(path, hit._world_position, light_sample._position,
                                   manifold_throughput, event_pdf))
        return 0.0.xxx;

    const ManifoldTriangle first_triangle = LoadPathTriangle(path._vertices[0]._primitive_index,
                                                             path._vertices[0]._triangle_index);
    const float3 first_position = EvaluateManifoldPosition(first_triangle, path._vertices[0]._surface_param);
    const float3 wi = normalize(first_position - hit._world_position);
    const float cosine = DirectLightCosTheta(trace_context._mat, hit._shading_normal, wi);
    if (cosine <= 1e-6f)
        return 0.0.xxx;

    float bsdf_pdf;
    const float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, wo, wi, bsdf_pdf);
    const float3 light_radiance = EvaluateManifoldLightRadiance(light_index, path, light_sample._position,
                                                                 light_sample._uv);
    if (bsdf_pdf <= 0.0f || all(bsdf == 0.0.xxx) || !any(light_radiance > 0.0.xxx))
        return 0.0.xxx;

    const float path_light_pdf = light_sample._pdf_area * light_selection_pmf;
    const float path_bsdf_pdf = bsdf_pdf * event_pdf * jacobian;
    const float mis_weight = PowerHeuristic(path_light_pdf, path_bsdf_pdf);
    return light_radiance * bsdf * (cosine * mis_weight * jacobian / max(path_light_pdf, 1e-12f)) *
           manifold_throughput;
}

float EvaluateManifoldHitMIS(ManifoldPath path, float3 receiver_position, uint light_index,
                             float receiver_bsdf_pdf, float3 light_position, float light_selection_pmf)
{
    if (path._vertex_count == 0u || path._vertex_count > kMaxManifoldVertices ||
        !IsSupportedManifoldMedium(path) || receiver_bsdf_pdf <= 0.0f)
        return 1.0f;

    SampleLightData light;
    float3 tangent_u;
    float3 tangent_v;
    if (!GetManifoldLightFrame(light_index, light_position, light, tangent_u, tangent_v))
        return 1.0f;

    ManifoldPath solved_path = path;
    if (!SolveManifoldPath(solved_path, receiver_position, light_position))
        return 1.0f;
    float jacobian;
    if (!ComputeManifoldSolidAngleJacobian(solved_path, receiver_position, light_position,
                                           tangent_u, tangent_v, light, jacobian))
        return 1.0f;

    const float light_area_pdf = GetUnifiedLightAreaPdf(light_index, receiver_position, light_position);
    const float mnee_pdf = light_area_pdf * light_selection_pmf;
    const float bsdf_pdf = receiver_bsdf_pdf * path._event_pdf * jacobian;
    if (mnee_pdf <= 0.0f || !MneeIsFinite(bsdf_pdf))
        return 1.0f;
    return PowerHeuristic(bsdf_pdf, mnee_pdf);
}

#endif // PATH_TRACER_MANIFOLD_HLSLI
