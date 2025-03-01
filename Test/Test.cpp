// Test.cpp : Source file for your target.
//

#include "Test.h"
#include <Framework/Common/TimeMgr.h>
#include <Framework/Math/ALMath.hpp>
#include <Framework/Math/Random.hpp>
#include <Render/Texture.h>
#include <iostream>

#include "DirectXMath.h"
#include "DirectXPackedVector.h"
#include "PerlinNoise.hpp"
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Ailu;

struct Test
{
public:
    Test() = default;
    virtual ~Test() = default;
    virtual void Run() = 0;
};

struct MatrixCompute : public Test
{
    void Run() override
    {
        TimeMgr timer;
        const u32 count = 1000000;
        Vector<Vector3f> pos(count);
        Vector<Vector3f> new_pos(count);
        for (u32 i = 0; i < count; ++i)
        {
            pos.push_back(Vector3f(i, i, i));
        };
        Matrix4x4f m = MatrixTranslation(1.f, 2.f, 3.f);
        timer.Mark();
        for (u32 i = 0; i < count; ++i)
        {
            new_pos[i] = TransformCoord(m, pos[i]);
        };
        //LOG_INFO("10W vertex transform: %f", timer.GetElapsedSinceLastMark());
        std::cout << "10W vertex transform: " << timer.GetElapsedSinceLastMark() << std::endl;

        std::vector<XMVECTOR> pos_dx(count);
        std::vector<XMVECTOR> new_pos_dx(count);

        for (u32 i = 0; i < count; ++i)
        {
            pos_dx.push_back(XMVectorSet(static_cast<float>(i), static_cast<float>(i), static_cast<float>(i), 1.0f));// w = 1.0 for homogeneous coordinates
        }
        XMMATRIX mdx = XMMatrixTranslation(1.0f, 2.0f, 3.0f);
        timer.Mark();
        // 执行矩阵变换
        for (u32 i = 0; i < count; ++i)
        {
            new_pos_dx[i] = XMVector3Transform(pos_dx[i], mdx);// 变换位置
        }
        //LOG_INFO("Directx 10W vertex transform: %f", timer.GetElapsedSinceLastMark());
        std::cout << "DirectX 10W vertex transform: " << timer.GetElapsedSinceLastMark() << std::endl;
    }
};

struct RegisterRead : public Test
{
    void Run() override
    {
        HKEY hKey = HKEY_LOCAL_MACHINE;                                                                        // 根键
        LPCWSTR subKey = L"SOFTWARE\\Classes\\CLSID\\{5D6BF029-A6BA-417A-8523-120492B1DCE3}\\InprocServer32\\";// 子键路径
        //LPCWSTR valueName = L"YourValue";     // 值名称
        wchar_t value[256];              // 存储结果
        DWORD bufferSize = sizeof(value);// 缓冲区大小

        // 获取值
        LONG result = RegGetValue(
                hKey,
                subKey,
                nullptr,
                RRF_RT_REG_SZ,// 期望字符串类型
                nullptr,
                value,
                &bufferSize);

        if (result == ERROR_SUCCESS)
        {
            std::wcout << L"Value: " << value << std::endl;
        }
        else
        {
            std::wcerr << L"Failed to read registry value. Error: " << result << std::endl;
        }
    }
};


#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

float SmoothStep(float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

class PerlinNoise
{
public:
    static const unsigned tableSize = 256;
    static const unsigned tableSizeMask = tableSize - 1;
    Vector3f gradients[tableSize];
    unsigned permutationTable[tableSize * 2];

    PerlinNoise()
    {
        unsigned seed = 2016;
        std::mt19937 generator(seed);
        std::uniform_real_distribution<float> distribution;
        auto dice = std::bind(distribution, generator);
        float gradientLen2;
        for (unsigned i = 0; i < tableSize; ++i)
        {
            do {
                gradients[i] = Vector3f(2 * dice() - 1, 2 * dice() - 1, 2 * dice() - 1);
                gradientLen2 = SqrMagnitude(gradients[i]);
            } while (gradientLen2 > 1);
            gradients[i] /= sqrtf(gradientLen2);//normalize gradient
            permutationTable[i] = i;
        }

        std::uniform_int_distribution<unsigned> distributionInt;
        auto diceInt = std::bind(distributionInt, generator);
        // create permutation table
        for (unsigned i = 0; i < tableSize; ++i)
            std::swap(permutationTable[i], permutationTable[diceInt() & tableSizeMask]);
        // extend the permutation table in the index range [256:512]
        for (unsigned i = 0; i < tableSize; ++i)
        {
            permutationTable[tableSize + i] = permutationTable[i];
        }
    }
    virtual ~PerlinNoise() {}

    /* inline */
    int hash(const int &x, const int &y, const int &z) const
    {
        return permutationTable[permutationTable[permutationTable[x] + y] + z];
    }

    float eval(const Vector3f &p) const
    {
        int xi0 = ((int) std::floor(p.x)) & tableSizeMask;
        int yi0 = ((int) std::floor(p.y)) & tableSizeMask;
        int zi0 = ((int) std::floor(p.z)) & tableSizeMask;

        int xi1 = (xi0 + 1) & tableSizeMask;
        int yi1 = (yi0 + 1) & tableSizeMask;
        int zi1 = (zi0 + 1) & tableSizeMask;

        float tx = p.x - ((int) std::floor(p.x));
        float ty = p.y - ((int) std::floor(p.y));
        float tz = p.z - ((int) std::floor(p.z));

        float u = SmoothStep(tx);
        float v = SmoothStep(ty);
        float w = SmoothStep(tz);

        // gradients at the corner of the cell
        const Vector3f &c000 = gradients[hash(xi0, yi0, zi0)];
        const Vector3f &c100 = gradients[hash(xi1, yi0, zi0)];
        const Vector3f &c010 = gradients[hash(xi0, yi1, zi0)];
        const Vector3f &c110 = gradients[hash(xi1, yi1, zi0)];

        const Vector3f &c001 = gradients[hash(xi0, yi0, zi1)];
        const Vector3f &c101 = gradients[hash(xi1, yi0, zi1)];
        const Vector3f &c011 = gradients[hash(xi0, yi1, zi1)];
        const Vector3f &c111 = gradients[hash(xi1, yi1, zi1)];

        // generate vectors going from the grid points to p
        float x0 = tx, x1 = tx - 1;
        float y0 = ty, y1 = ty - 1;
        float z0 = tz, z1 = tz - 1;

        Vector3f p000 = Vector3f(x0, y0, z0);
        Vector3f p100 = Vector3f(x1, y0, z0);
        Vector3f p010 = Vector3f(x0, y1, z0);
        Vector3f p110 = Vector3f(x1, y1, z0);

        Vector3f p001 = Vector3f(x0, y0, z1);
        Vector3f p101 = Vector3f(x1, y0, z1);
        Vector3f p011 = Vector3f(x0, y1, z1);
        Vector3f p111 = Vector3f(x1, y1, z1);

        // linear interpolation
        float a = Lerp(DotProduct(c000, p000), DotProduct(c100, p100), u);
        float b = Lerp(DotProduct(c010, p010), DotProduct(c110, p110), u);
        float c = Lerp(DotProduct(c001, p001), DotProduct(c101, p101), u);
        float d = Lerp(DotProduct(c011, p011), DotProduct(c111, p111), u);

        float e = Lerp(a, b, v);
        float f = Lerp(c, d, v);

        return Lerp(e, f, w);//g
    }
};


#define M_PI 3.14159265358979323846
#define M_PI_2 6.28318530717958647692
/*
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

// 平滑插值函数 (S曲线)
// float SmoothStep(float t)
// {
//     return t * t * t * (t * (t * 6 - 15) + 10);
// }

// 梯度向量（随机方向）
struct Gradient
{
    float x, y;
    Gradient(float x = 0, float y = 0) : x(x), y(y) {}
    float Dot(float dx, float dy) const
    {
        return x * dx + y * dy;
    }
};

// 随机梯度表
std::vector<int> perm;
std::vector<Gradient> gradients;

// 初始化梯度和置换表
void InitializePerlin()
{
    srand(static_cast<unsigned>(time(0)));
    for (int i = 0; i < 256; i++)
    {
        gradients.emplace_back(cos(i * 2.0 * M_PI / 256), sin(i * 2.0 * M_PI / 256));
        perm.push_back(i);
    }

    // 随机打乱
    for (int i = 0; i < 256; i++)
    {
        int j = rand() % 256;
        std::swap(perm[i], perm[j]);
    }

    // 重复
    perm.insert(perm.end(), perm.begin(), perm.end());
}

// 获取梯度向量
Gradient GetGradient(int x, int y)
{
    int idx = perm[(perm[x % 256] + y % 256) % 256];
    return gradients[idx];
}

// 单个网格点的贡献
float Surflet(int gridX, int gridY, float x, float y)
{
    float dx = x - gridX;
    float dy = y - gridY;

    Gradient grad = GetGradient(gridX, gridY);
    float dot = grad.Dot(dx, dy);

    float sx = SmoothStep(1 - fabs(dx));
    float sy = SmoothStep(1 - fabs(dy));

    return dot * sx * sy;
}

// 柏林噪声主函数
float PerlinNoise(float x, float y, int period = 1)
{
    int intX = static_cast<int>(floor(x));
    int intY = static_cast<int>(floor(y));

    float value = 0.0f;
    value += Surflet(intX % period, intY % period, x, y);
    value += Surflet((intX + 1) % period, intY % period, x, y);
    value += Surflet(intX % period, (intY + 1) % period, x, y);
    value += Surflet((intX + 1) % period, (intY + 1) % period, x, y);

    return value * 0.5f + 0.5f;
}
*/
float remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}
int main(int argc, char **argv)
{
    const siv::PerlinNoise::seed_type seed = 123456u;
	const siv::PerlinNoise perlin{ seed };
    //InitializePerlin();
    //PerlinNoise perlin;
    u16 w = 128, h = 128;
    auto noise_tex = Texture2D::Create(w, h, false);
    u32 size = w * h * 4;
    u8 *data = new u8[size];
    f32 div = argc > 1 ? std::stof(argv[1]) : 0.2f;
    f32 period = argc > 2 ? std::stof(argv[2]) : 1.0f;
    u32 octaves = argc > 3 ? std::stoi(argv[3]) : 4;
    std::cout << "Generating noise texture with div " << div << std::endl;
    std::cout << "Generating noise texture with period " << period << std::endl;
    std::cout << "Generating noise texture with octaves " << octaves << std::endl;

    Random::NoiseGenerator noise_gen(2033);
    for (u32 i = 0; i < size; i += 4)
    {
        
        u32 idx = i / 4;
        u32 uxi = idx % w;
        u32 uyi = idx / w;
        f32 u = (uxi / (f32) (w - 1));
        f32 v = (uyi / (f32) (h - 1));
        //Vector2f fpos = {u, v};
        f32 worley = noise_gen.FBM(u, v,0.f,octaves, 2.0f, 0.5f, period, &Random::NoiseGenerator::Voronoi3D);
        f32 fnoise = noise_gen.FBM(u, v,0.f,octaves, 2.0f, 0.5f, 0.1f, &Random::NoiseGenerator::Perlin3D);
        //fnoise = Lerp(1.f, fnoise, 0.5f);
        //f32 fnoise = perlinfbm(Vector2f(u / div, v / div), period,octaves) * 0.5 + 0.5;
        f32 t = worley;
        //t = remap(fnoise, 0.f, 1.0f, worley, 1.0f);
        Clamp(t,0.f,1.f);
        u8 noise = static_cast<u8>(t * 255);
        Color32 c{noise};
        c.a = 255;
        //std::cout << c.ToString() << std::endl;
        memcpy(data + i, c, sizeof(Color32));
    }
    noise_tex->SetPixelData(data, 0);
    noise_tex->EncodeToPng(L"test_noise.png");
    return 0;
}
