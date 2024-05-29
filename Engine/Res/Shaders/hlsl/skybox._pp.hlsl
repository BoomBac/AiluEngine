//info bein
//pass begin::
//name: skybox_pp
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Fill: Solid
//ZTest: LEqual
//ZWrite: Off
//pass end::
//info end
#include "common.hlsli"

static const float _PlanetRadius = 6371000;
static const float _AtmosWidth = 191313;
static const float4 _SunLight = float4(1.0,1.0,1.0,1.0);
static const float _RayleighScale = 0.35;
static const float _MieScale = 0.1;
static const float _SunSize = 0.000167f;
static const float _SunLightIntensity = 80;
static const float4 _GroundColor = float4(0,0,0,0);
static const float4 _Rayleigh = float4(5.802, 13.558, 33.1, 1.0);
static const float4 _Mie = float4(3.996, 3.996, 3.996, 1.0);
static const float4 _Ambient =float4 (0.0, 0.0, 0.0, 1.0);
static const float4 _Absorption = float4(2.04, 4.97, 19.5, 1.0);
static const float _G = 0.973;
static const float _RayleighScatteringScalarHeight = 9344;
static const float _MieScatteringScalarHeight = 1333;
static const float _AbsorptionScalarHeight = 30000;
static const float _AbsorptionFalloff = 4000;

struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 vertex : SV_POSITION;
	float3 positionWS : TEXCOORD0;
	float3 viewDirWS : TEXCOORD1;
	float3 eyePosWS : TEXCOORD2;
};

#define SIZE 10000.0f
static const float3x3 world_matrix = float3x3(
float3(SIZE,0.f,0.f),
float3(0.f, SIZE, 0.f),
float3(0.f, 0.f, SIZE)
);

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.positionWS = mul(world_matrix, v.position);
	result.vertex = TransformFromWorldToClipSpace(result.positionWS);
	result.vertex.z = result.vertex.w;
	result.viewDirWS = result.positionWS - _CameraPos.xyz;
	result.eyePosWS = _CameraPos.xyz;
	return result;
}

#define PLANET_POS ((float3)0.)
#define PLANET_RADIUS _PlanetRadius
#define ATMOS_RADIUS _PlanetRadius + _AtmosWidth
#define SUN_LUMINANCE (_SunLight * _SunLightIntensity).rgb
#define RAY_BETA (_Rayleigh * 0.000001).rgb
#define MIE_BETA (_Mie * 0.000001).rgb
#define AMBIENT_BETA (_Ambient * 0.000001).rgb
#define ABSORPTION_BETA (_Absorption * 0.000001).rgb
#define G _G
#define HEIGHT_RAY _RayleighScatteringScalarHeight
#define HEIGHT_MIE _MieScatteringScalarHeight
#define HEIGHT_ABSORPTION _AbsorptionScalarHeight
#define ABSORPTION_FALLOFF _AbsorptionFalloff


#define PRIMARY_STEPS 16
#define LIGHT_STEPS 4

#define CAMERA_MODE 0

            //瑞利散射相位函数
inline float RayleiPhase(float cos_theta)
{
	return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

            //米氏散射相位函数
inline float MiePhase(float g, float cos_theta)
{
	float gg = g * g;
	return (1 - gg) / (4.0 * PI * pow(1.0 + gg - 2.0 * g * cos_theta, 1.5f));
}

float2 RaySphereIntersect(float3 start, float3 dir, float radius)
{
	float a = dot(dir, dir);
	float b = 2. * dot(dir, start);
	float c = dot(start, start) - radius * radius;
	float d = b * b - 4. * a * c;
	if (d < 0.)
		return float2(10000000., -10000000.);
	return float2((-b - sqrt(d)) / (2. * a), (-b + sqrt(d)) / (2. * a));
}

float3 calculate_scattering(float3 start, float3 dir, float max_dist, float3 scene_color, float3 light_dir, float3 light_intensity,
            float3 planet_position, float planet_radius, float atmo_radius, float3 beta_ray, float3 beta_mie,
            float3 beta_absorption, float3 beta_ambient, float g, float height_ray, float height_mie, float height_absorption, float absorption_falloff, int steps_i, int steps_l)
{
	start -= planet_position;
	float2 ray_length = RaySphereIntersect(start, dir, atmo_radius);
	if (ray_length.x > ray_length.y)
		return scene_color;
                    
	bool allow_mie = max_dist > ray_length.y;
	ray_length.y = min(ray_length.y, max_dist);
	ray_length.x = max(ray_length.x, 0.);
	float step_size_i = (ray_length.y - ray_length.x) / float(steps_i);
	float ray_pos_i = ray_length.x + step_size_i * 0.5;
	float3 total_ray = ((float3) 0.);
	float3 total_mie = ((float3) 0.);
	float3 opt_i = ((float3) 0.);
	float2 scale_height = float2(height_ray, height_mie);
	float mu = dot(dir, light_dir);
	float phase_ray = RayleiPhase(mu);
	float phase_mie = MiePhase(g, mu);

	for (int i = 0; i < steps_i; ++i)
	{
		float3 pos_i = start + dir * ray_pos_i; //p1
		float height_i = length(pos_i) - planet_radius;
		float3 density = float3(exp(-height_i / scale_height), 0.);
		float denom = (height_absorption - height_i) / absorption_falloff;
		density.z = 1. / (denom * denom + 1.) * density.x;
		density *= step_size_i;
		opt_i += density;
		ray_length = RaySphereIntersect(pos_i, light_dir, atmo_radius);
		float step_size_l = step_size_l = ray_length.y / float(steps_l);
                    
		float ray_pos_l = step_size_l * 0.5;
		float3 opt_l = ((float3) 0.);
		for (int l = 0; l < steps_l; ++l)
		{
			float3 pos_l = pos_i + light_dir * ray_pos_l; //p2
			float height_l = length(pos_l) - planet_radius;
			float3 density_l = float3(exp(-height_l / scale_height), 0.);
			float denom = (height_absorption - height_l) / absorption_falloff;
			density_l.z = 1. / (denom * denom + 1.) * density_l.x;
			density_l *= step_size_l;
			opt_l += density_l;
			ray_pos_l += step_size_l;
		}
		float3 attn = exp(-beta_ray * (opt_i.x + opt_l.x) - beta_mie * (opt_i.y + opt_l.y) - beta_absorption * (opt_i.z + opt_l.z));
		total_ray += density.x * attn;
		total_mie += density.y * attn;
		ray_pos_i += step_size_i;
	}
	float3 opacity = exp(-(beta_mie * opt_i.y + beta_ray * opt_i.x + beta_absorption * opt_i.z));
	return (_RayleighScale * phase_ray * beta_ray * total_ray + _MieScale * phase_mie * beta_mie * total_mie + opt_i.x * beta_ambient) * light_intensity + scene_color * opacity;
}



float3 skylight(float3 sample_pos, float3 surface_normal, float3 light_dir, float3 background_col)
{
	surface_normal = normalize(lerp(surface_normal, light_dir, 0.6));
	return calculate_scattering(sample_pos, surface_normal, 3. * ATMOS_RADIUS, background_col, light_dir, ((float3) 40.), PLANET_POS, PLANET_RADIUS, ATMOS_RADIUS, RAY_BETA, MIE_BETA, ABSORPTION_BETA, AMBIENT_BETA, G, HEIGHT_RAY, HEIGHT_MIE, HEIGHT_ABSORPTION, ABSORPTION_FALLOFF, LIGHT_STEPS, LIGHT_STEPS);
}

float4 render_scene(float3 pos, float3 dir, float3 light_dir)
{
	float4 color = float4(0., 0., 0., 1000000000000.);
	color.xyz = ((float3) dot(dir, light_dir) > 0.99998 * (1 - _SunSize) ? 3. : 0.);
	float2 planet_intersect = RaySphereIntersect(pos - PLANET_POS, dir, PLANET_RADIUS);
	if (0. < planet_intersect.y)
	{
		color.w = max(planet_intersect.x, 0.);
		float3 sample_pos = pos + dir * planet_intersect.x - PLANET_POS;
		float3 surface_normal = normalize(sample_pos);
		color.xyz = _GroundColor;
		float3 N = surface_normal;
		float3 V = -dir;
		float3 L = light_dir;
		float dotNV = max(0.000001, dot(N, V));
		float dotNL = max(0.000001, dot(N, L));
		float shadow = dotNL / (dotNL + dotNV);
		color.xyz *= shadow * _GroundColor;
		color.xyz += clamp(skylight(sample_pos, surface_normal, light_dir, ((float3) 0.)) * _GroundColor.rgb, 0., 1.);
	}
                
	return color;
}

float4 PSMain(PSInput vertex_output) : SV_TARGET
{
	float4 fragColor = 0;
	float2 fragCoord = vertex_output.vertex.xy;
	float3 camera_vector = normalize(vertex_output.viewDirWS);
	float3 camera_position = float3(0., PLANET_RADIUS + vertex_output.eyePosWS.y + 500, 0.);
    float3 light_dir = -normalize(_DirectionalLights[0]._LightPosOrDir.xyz);
	//float3 light_dir = float3(0.71, 0.0, 0.71);
	float4 scene = render_scene(camera_position, camera_vector, light_dir);
	float3 col = ((float3) 0.);
	col += calculate_scattering(camera_position, camera_vector, scene.w, scene.xyz, light_dir, SUN_LUMINANCE, PLANET_POS, PLANET_RADIUS, ATMOS_RADIUS, RAY_BETA, MIE_BETA, ABSORPTION_BETA, AMBIENT_BETA, lerp(0.0001, 1, G), HEIGHT_RAY, HEIGHT_MIE, HEIGHT_ABSORPTION, ABSORPTION_FALLOFF, PRIMARY_STEPS, LIGHT_STEPS);
    col = 1.-exp(-col);
	fragColor = float4(col, 1.);
	return fragColor;
}
