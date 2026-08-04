#pragma once

#include "Framework/Core/CoreMinimal.h"

namespace Ailu::Render::FrameDebugger
{
enum class EPsoDirtyReason : u32
{
    kNone = 0u,
    kShaderChanged = 1u << 0u,
    kShaderPassChanged = 1u << 1u,
    kShaderVariantChanged = 1u << 2u,
    kVertexLayoutChanged = 1u << 3u,
    kBlendStateChanged = 1u << 4u,
    kRasterizerStateChanged = 1u << 5u,
    kDepthStencilStateChanged = 1u << 6u,
    kRenderTargetStateChanged = 1u << 7u
};

constexpr EPsoDirtyReason operator|(EPsoDirtyReason a, EPsoDirtyReason b) { return EPsoDirtyReason((u32)a | (u32)b); }
constexpr EPsoDirtyReason operator|=(EPsoDirtyReason &a, EPsoDirtyReason b) { a = a | b; return a; }
constexpr bool operator&(EPsoDirtyReason a, EPsoDirtyReason b) { return ((u32)a & (u32)b) != 0u; }

enum class EPsoBindReason : u8
{
    kCacheHit,
    kFirstBind,
    kPsoObjectChanged,
    kCommandListReset
};

enum class EPsoLookupResult : u8
{
    kNotRequired,
    kCacheHit,
    kCacheMiss,
    kNotReady,
    kCreationRequested
};

enum class EBindingInvalidReason : u32
{
    kNone = 0u,
    kSlotUninitialized = 1u << 0u,
    kPsoChanged = 1u << 1u,
    kCommandListReset = 1u << 2u,
    kResourceChanged = 1u << 3u,
    kResourceTypeChanged = 1u << 4u,
    kGpuAddressChanged = 1u << 5u,
    kNativeResourceChanged = 1u << 6u,
    kViewIndexChanged = 1u << 7u,
    kSubResourceChanged = 1u << 8u,
    kDescriptorHeapChanged = 1u << 9u
};

constexpr EBindingInvalidReason operator|(EBindingInvalidReason a, EBindingInvalidReason b) { return EBindingInvalidReason((u32)a | (u32)b); }
constexpr EBindingInvalidReason operator|=(EBindingInvalidReason &a, EBindingInvalidReason b) { a = a | b; return a; }
constexpr bool operator&(EBindingInvalidReason a, EBindingInvalidReason b) { return ((u32)a & (u32)b) != 0u; }

enum class EBindingCacheResult : u8
{
    kBound,
    kSkipped,
    kInherited,
    kUnbound
};

enum class EBindingSource : u8
{
    kUnknown,
    kGlobal,
    kMaterial,
    kCommand,
    kInherited,
    kShader
};

enum class EGeometryBindingInvalidReason : u32
{
    kNone = 0u,
    kFirstBind = 1u << 0u,
    kPsoChanged = 1u << 1u,
    kCommandListReset = 1u << 2u,
    kBufferChanged = 1u << 3u,
    kInputLayoutChanged = 1u << 4u,
    kViewVersionChanged = 1u << 5u
};

constexpr EGeometryBindingInvalidReason operator|(EGeometryBindingInvalidReason a, EGeometryBindingInvalidReason b) { return EGeometryBindingInvalidReason((u32)a | (u32)b); }
constexpr EGeometryBindingInvalidReason operator|=(EGeometryBindingInvalidReason &a, EGeometryBindingInvalidReason b) { a = a | b; return a; }
constexpr bool operator&(EGeometryBindingInvalidReason a, EGeometryBindingInvalidReason b) { return ((u32)a & (u32)b) != 0u; }

enum class EMaterialBindingInvalidReason : u32
{
    kNone = 0u,
    kMaterialResourceChanged = 1u << 0u,
    kBindingLayoutChanged = 1u << 1u,
    kShaderVariantChanged = 1u << 2u,
    kGlobalLayoutChanged = 1u << 3u,
    kGlobalBindingChanged = 1u << 4u
};

constexpr EMaterialBindingInvalidReason operator|(EMaterialBindingInvalidReason a, EMaterialBindingInvalidReason b) { return EMaterialBindingInvalidReason((u32)a | (u32)b); }
constexpr EMaterialBindingInvalidReason operator|=(EMaterialBindingInvalidReason &a, EMaterialBindingInvalidReason b) { a = a | b; return a; }
constexpr bool operator&(EMaterialBindingInvalidReason a, EMaterialBindingInvalidReason b) { return ((u32)a & (u32)b) != 0u; }

enum class EMaterialBindingResolveResult : u8
{
    kCacheHit,
    kGlobalBindingRefresh,
    kFullRebuild
};

} // namespace Ailu::Render::FrameDebugger
