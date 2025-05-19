// Test.cpp : Source file for your target.
//

#include "Test.h"
#include <Framework/Common/TimeMgr.h>
#include <Framework/Common/Allocator.hpp>
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


int main(int argc, char **argv)
{
    auto& alloc = Allocator::Get();
    int* a = AL_NEW(int);
    alloc.PrintLeaks();
    Allocator::Shutdown();
}
