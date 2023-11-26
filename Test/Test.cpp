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

int main() {
    int width, height, channels;
    //unsigned char* image_data = stbi_load("C:\\Users\\BoomBac\\Desktop\\MyImage01.jpg", &width, &height, &channels, 3);
    unsigned char* image_data = stbi_load("F:\\ProjectCpp\\AiluEngine\\Engine\\Res\\Textures\\Intergalactic Spaceship_color_4.png", &width, &height, &channels, 3);

    // 检查图像是否成功加载
    if (image_data == NULL) {
        printf("Error loading image\n");
        return -1;
    }
    unsigned char* new_image_data = nullptr;
    // 分配内存给新的四通道数组
    int new_channels = 4;
    if (channels != 4)
    {
        size_t new_size = width * height * new_channels;
        new_image_data = (unsigned char*)malloc(new_size);
        // 将三通道数据复制到四通道数组中
        for (int i = 0; i < width * height; ++i) {
            // 复制RGB值
            memcpy(new_image_data + i * new_channels, image_data + i * channels, 3);

            // 将Alpha通道设置为适当的值（例如，通过检查RGB值是否为白色来决定透明度）
            if (image_data[i * channels] == 255 && image_data[i * channels + 1] == 255 && image_data[i * channels + 2] == 255) {
                new_image_data[i * new_channels + 3] = 0;  // 白色为完全透明
            }
            else {
                new_image_data[i * new_channels + 3] = 255;  // 其他颜色为不透明
            }
        }
    }
    else
        new_image_data = image_data;

    std::list<uint8_t*> mipmaps{};
    while (width > 1)
    {
        try
        {
            auto new_image = DownSample(mipmaps.empty() ? new_image_data : mipmaps.back(), width, height, 4);
            mipmaps.emplace_back(new_image);
            //stbi_write_jpg(std::format("C:\\Users\\BoomBac\\Desktop\\MyImage01_{}.jpg", width).c_str(), width, height, new_channels, new_image, width * new_channels);
            stbi_write_png(std::format("C:\\Users\\BoomBac\\Desktop\\MyImage01_{}.jpg", width).c_str(), width, height, new_channels, new_image, width * new_channels);
        }
        catch (const std::exception& e)
        {
            cout << e.what() << endl;
        }

    }
    for (auto it : mipmaps)
    {
        if(it != nullptr)
            delete[] it;
    }
    // 释放内存
    stbi_image_free(image_data);
    free(new_image_data);
    return 0;
}
