#pragma once
#ifndef __GFX_CONFIG_H__
#define __GFX_CONFIG_H__
#include <sstream>
#include <string>
#include <format>


namespace Ailu
{
    struct GfxConfiguration
    {
        /// <summary>
        /// 
        /// </summary>
        /// <param name="r">the red color depth in bits</param>
        /// <param name="g">the green color depth in bits</param>
        /// <param name="b">the blue color depth in bits</param>
        /// <param name="a">the alpha color depth in bits</param>
        /// <param name="d">the depth buffer depth in bits</param>
        /// <param name="s">the stencil buffer depth in bits</param>
        /// <param name="msaa">the msaa sample count</param>
        /// <param name="width">the screen width in pixel</param>
        /// <param name="height">the screen height in pixel</param>
        /// <param name="window_name">the window name</param>
        explicit GfxConfiguration(int32_t r = 8, int32_t g = 8, int32_t b = 8,
            int32_t a = 8, int32_t d = 24, int32_t s = 0,
            int32_t msaa = 1, int32_t width = 1280,
            int32_t height = 720)
            : red_bits_(r),
            green_bits_(g),
            blue_bits_(b),
            alpha_bits_(a),
            depth_bits_(d),
            stencil_bits_(s),
            msaa_samples_(msaa),
            viewport_width_(width),
            viewport_height_(height)
        {
            
        }

        int32_t red_bits_{ 8 };    ///< red color channel depth in bits
        int32_t green_bits_{ 8 };    ///< green color channel depth in bits
        int32_t blue_bits_{ 8 };     ///< blue color channel depth in bits
        int32_t alpha_bits_{ 8 };    ///< alpha color channel depth in bits
        int32_t depth_bits_{ 24 };   ///< depth buffer depth in bits
        int32_t stencil_bits_{ 8 };  ///< stencil buffer depth in bits
        int32_t msaa_samples_{ 4 };  ///< MSAA samples
        int32_t viewport_width_{ 1280 };
        int32_t viewport_height_{ 720 };

        inline std::wstring GetInfo()
        {
            return std::format(L"ColorBufferFormat: ({},{},{},{})\r\nDepthStencilFormat: ({},{})\r\nMSAA sample: {}\r\n RenderViewportSize: {},{}\r\n",red_bits_,green_bits_,blue_bits_,alpha_bits_,
                depth_bits_,stencil_bits_,
                msaa_samples_,
                viewport_width_,viewport_height_);
        }
    };
    static GfxConfiguration g_gfx_config;
}

#endif // !GFX_CONFIG_H__
