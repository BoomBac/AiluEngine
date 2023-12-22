#define STB_IMAGE_IMPLEMENTATION

#include "../Engine/Ext/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../Engine/Ext/stb/stb_image_write.h"
#include <cstdint>
#define __STDC_LIB_EXT1__
#include <iostream>
#include <list>
#include <format>
#include <string>

#include <bitset>

using namespace std;

uint8_t* DownSample(unsigned char* image, int& width, int& height, int channels)
{
    size_t row_size = channels * width;
    size_t new_width = width >> 1;
    size_t new_height = height >> 1;
    size_t new_img_size = new_width * new_height * channels;
    uint8_t* new_image = new uint8_t[new_img_size];
    auto downsample_sub_task = [&](int xbegin,int xend,int ybegin,int yend,int new_width) {
        uint8_t blend_color[4]{};
        for (size_t i = ybegin; i < yend; ++i)
        {
            for (size_t j = xbegin; j < xend; ++j)
            {
                // 计算混合颜色的坐标
                size_t a_i = i * 2;
                size_t a_j = j * 2;
                // 获取当前像素及其右侧像素
                uint8_t* a = &image[a_i * row_size + a_j * channels];
                uint8_t* b = &image[a_i * row_size + (a_j + 1) * channels];
                // 计算齐次加权平均值
                for (int k = 0; k < channels; ++k) {
                    blend_color[k] = static_cast<uint8_t>((a[k] + b[k]) / 2);
                }
                size_t new_index = i * new_width * channels + j * channels;
                memcpy(new_image + new_index, blend_color, sizeof(blend_color));
            }
        }
    };
    int sub_task_block_size = 32;
    int task_num = new_width / sub_task_block_size;
    task_num = task_num == 0 ? 1 : task_num;
    if(task_num == 1)
        downsample_sub_task(0, new_width, 0, new_height, new_width);
    else
    {
        for (size_t i = 0; i < task_num; i++)
        {
            for (size_t j = 0; j < task_num; j++)
            {
                downsample_sub_task(j * sub_task_block_size, (j + 1) * sub_task_block_size, i * sub_task_block_size, (i + 1) * sub_task_block_size, new_width);
            }
        }
    }
    width = new_width;
    height = new_height;
    return new_image;
}

template<uint8_t Size>
class Hash
{
public:
    Hash()
    {
        _hash.reset();
    }
    void Set(uint8_t pos, uint8_t size,uint32_t value)
    {
        while (size--)
        {
            _hash.set(pos++, value & 1);
            value >>= 1;
        }
        if (value > 0)
        {

        }
    }
    bool operator==(const Hash<Size>& other) const
    {
        return _hash == other._hash;
    }
private:
    bitset<Size> _hash;
};

int main() 
{
    Hash<64u> pso_hash;
    Hash<64u> pso_hash0;
    pso_hash.Set(0,2,2);
    cout << "Hello" << (pso_hash == pso_hash0 ? "true" : "false") << endl;
    return 0;
}
