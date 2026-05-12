#ifndef _BINDLESS_HLSLI_
#define _BINDLESS_HLSLI_

Texture2D g_bindless_texture2d[]: register(t0,space8);
RWTexture2D<float4> g_bindless_rw_texture2d[]: register(u0,space8);
ByteAddressBuffer g_bindless_buffer[]: register(t0,space9);
RWByteAddressBuffer g_bindless_rw_buffer[]: register(u0,space9);
// Reserved for future bindless constant buffer support: b0, space10.

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffff
#endif

#define INVALID_BINDLESS_SRV_INDEX UINT32_MAX
#define valid_bindless_handle(index) (index != INVALID_BINDLESS_SRV_INDEX)
#define INVALID_BINDLESS_BUFFER_INDEX INVALID_BINDLESS_SRV_INDEX
#define valid_bindless_buffer(index) valid_bindless_handle(index)

#define g_bindless_vertex_buffer g_bindless_buffer
#define g_bindless_index_buffer g_bindless_buffer
#define g_bindless_structured_buffer g_bindless_buffer
#define BINDLESS_BUFFER_LOAD(type, index, byte_offset) g_bindless_buffer[index].Load<type>(byte_offset)

#define BINDLESS_RWBUFFER_STORE_OFFSET(type,handle,byte_offset,value) g_bindless_rw_buffer[handle].Store<type>(byte_offset, value)
#define BINDLESS_RWBUFFER_STORE_INDEX(type,handle,index,value) g_bindless_rw_buffer[handle].Store<type>(index * sizeof(type), value)

#define BINDLESS_RWBUFFER_LOAD_OFFSET(type,handle,byte_offset) g_bindless_rw_buffer[handle].Load<type>(byte_offset)
#define BINDLESS_RWBUFFER_LOAD_INDEX(type,handle,index) g_bindless_rw_buffer[handle].Load<type>(index * sizeof(type))
#endif