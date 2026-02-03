#ifndef _BINDLESS_HLSLI_
#define _BINDLESS_HLSLI_

Texture2D g_bindless_texture2d[]: register(t0,space8);

#define INVALID_BINDLESS_SRV_INDEX UINT32_MAX
#define valid_bindless_srv(index) (index != INVALID_BINDLESS_SRV_INDEX)
#endif