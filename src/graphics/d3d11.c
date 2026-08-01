#include "base.h"
#include "graphics_internal.h"
#include "os_graphical.h"

#define       CINTERFACE
#define       COBJMACROS
#define D3D11_NO_HELPERS
#pragma comment(lib,        "Gdi32")
#pragma comment(lib,       "dxguid")
#pragma comment(lib,        "d3d11")
#pragma comment(lib,  "d3dcompiler")
#include <d3dcompiler.h>
#include   <dxgidebug.h>
#include        <dxgi.h>
#include       <d3d11.h>
#include     <dxgi1_3.h>

// Todo, we want to also support loading shaders from a file, to allow people
// to write their own shaders...
#include "text_mask_ps.h"
#include "ps_rect_sdf.h"
#include "vs_rect_sdf.h"
#include "ps_blit.h"
#include "ps_barrel.h"
#include "ps_gaussian.h"
#include "ps_copy.h"
#include "ps_blur_material.h"
#include "ps_glass.h"
#include "ps_crt_scanlines.h"
#include "ps_luminance.h"
#include "ps_rewind.h"

static void r__init_base_texture(GFX_Texture *texture, GFX_TextureUsage usage, GFX_TextureBindFlags bind_flags, GFX_Format format, vec2i size)
{
	texture->reso = size;
	texture->usage = usage;
	texture->bind_flags = bind_flags;
	texture->format = format;
	texture->sampler = GRAPHICS_SAMPLER_LINEAR;
}

vec2i gfx_texture_size(const GFX_Texture *texture)
{
	Assert(texture);
	return texture->reso;
}

typedef struct
{
	Matrix transform;
	Matrix mixer;
	vec2   texture0_reso;
	vec2   padding;
	f32    opacity;
	f32    padding2[3];
	GFX_ShaderBlock custom;
}
_CBUFFER;

STATIC_ASSERT((offsetof(_CBUFFER, transform) & 15) == 0);
STATIC_ASSERT((offsetof(_CBUFFER, mixer) & 15) == 0);
STATIC_ASSERT((offsetof(_CBUFFER, texture0_reso) & 15) == 0);
STATIC_ASSERT((offsetof(_CBUFFER, opacity) & 15) == 0);
STATIC_ASSERT((offsetof(_CBUFFER, custom) & 15) == 0);

typedef struct D3D_Texture D3D_Texture;
struct D3D_Texture
{
	GFX_Texture base;
	D3D_Texture *next_free;
	ID3D11ShaderResourceView *input_view;
	ID3D11RenderTargetView   *output_view;
	ID3D11Texture2D          *readback;
	union {
		ID3D11Texture2D       *texture;
		ID3D11Resource        *as_resource;
	};
};

typedef struct GFX_TransientTexture GFX_TransientTexture;
struct GFX_TransientTexture
{
	GFX_TransientTexture *next;
	GFX_Texture *texture;
	GFX_TextureDesc desc;
	u64 acquired_frame;
};

enum {
	VBUFFER_INITIAL_CAPACITY = MB(1),
};

typedef struct
{
	GFX_Texture         *output;
	rect_i32           viewport;
	GFX_Texture        *texture;
	rect_i32            scissor;
	GFX_Sampler         sampler;
	GFX_Blender         blender;
	ID3D11PixelShader  *pshader;
	ID3D11Buffer       *vbuffer;
}
D3D_Pipeline;

struct GFX_Renderer {
	Arena                    main_arena;
	ID3D11InfoQueue         *info;
	ID3D11Device            *device;
	ID3D11DeviceContext     *context;
	D3D_Pipeline          state;
	GFX_Texture        *fallback_texture;
	D3D_Texture             *free_textures;
	GFX_TransientTexture    *transient_textures;
	u64                      frame_index;

	ID3D11Buffer            *vbuffer;
	u32                      vbuffer_capacity;
	ID3D11RasterizerState   *default_rasterizer;
	ID3D11DepthStencilState *default_depth_stencil;
	ID3D11Buffer            *cbuffer;

	ID3D11VertexShader      *vshader;
	ID3D11InputLayout       *ilayout;
	ID3D11PixelShader       *pshaders[GFX_SHADER_COUNT];
	ID3D11BlendState        *blenders[GFX_BLENDER_COUNT];
	ID3D11SamplerState      *samplers[GRAPHICS_SAMPLER_COUNT];
};

struct GFX_Window {
	GFX_Renderer  *renderer;
	IDXGISwapChain2 *swapchain;
	D3D_Texture    output;
};

#define g (*renderer)


#define RELEASE(v) ((v) ? (v)->lpVtbl->Release(v) : (0))
#define GET_BUFFER_DATA(v) ((v)->lpVtbl->GetBufferPointer(v))
#define GET_BUFFER_SIZE(v) ((v)->lpVtbl->GetBufferSize(v))

static void
d3d_ensure_vertex_buffer_capacity(GFX_Renderer *renderer, u32 required)
{
	if (g.vbuffer && required <= g.vbuffer_capacity)
	{
		return;
	}

	u32 capacity = g.vbuffer_capacity ? g.vbuffer_capacity : VBUFFER_INITIAL_CAPACITY;
	while (capacity < required)
	{
		Assert(capacity <= MAX_VALUE_U32 / 2);
		capacity *= 2;
	}

	RELEASE(g.vbuffer);
	g.vbuffer = 0;
	// Recreating the buffer invalidates the renderer's cached binding even if
	// the allocator happens to reuse the same COM interface address.
	g.state.vbuffer = 0;
	D3D11_BUFFER_DESC desc = {
		.Usage = D3D11_USAGE_DYNAMIC,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.BindFlags = D3D11_BIND_VERTEX_BUFFER,
		.ByteWidth = capacity,
	};
	HRESULT hr = ID3D11Device_CreateBuffer(g.device, &desc, NULL, &g.vbuffer);
	Assert(SUCCEEDED(hr));
	g.vbuffer_capacity = capacity;
}

GFX_Texture *r_get_window_output(GFX_Window *window)
{
	return &window->output.base;
}

static
D3D_Texture *d3d_texture_from_texture(GFX_Texture *texture)
{
	return (D3D_Texture *) texture;
}

static
ID3D11Resource *r_resource_from_texture(D3D_Texture *texture)
{
	return texture->as_resource;
}



GFX_Renderer *r_renderer_create(Arena *owner) {
	GFX_Renderer *renderer = arena_push_zero(owner, sizeof(*renderer));

	g.main_arena = arena_create(0, "r main arena");


	u32 device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

#if defined(_DEBUG)
	device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif


	HRESULT hr;
	{
		D3D_FEATURE_LEVEL requires[] = {D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, device_flags
		, requires, ArrayCount(requires), D3D11_SDK_VERSION
		, &g.device, NULL, &g.context);
	}

	// The optional Direct3D debug layer is not installed on every machine.
	// Keep diagnostics when available, but do not make them a startup
	// requirement even for a developer build.
	if (FAILED(hr) && (device_flags & D3D11_CREATE_DEVICE_DEBUG)) {
		device_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		D3D_FEATURE_LEVEL requires[] = {D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, device_flags
		, requires, ArrayCount(requires), D3D11_SDK_VERSION
		, &g.device, NULL, &g.context);
	}

	if (FAILED(hr)) {
		// The embedded shaders target shader model 5, so the software
		// renderer must provide the same feature level as the hardware path.
		D3D_FEATURE_LEVEL requires[] = {D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, 0, device_flags
		, requires, ArrayCount(requires), D3D11_SDK_VERSION
		, &g.device, NULL, &g.context);
	}

	Assert(SUCCEEDED(hr));


	if (device_flags & D3D11_CREATE_DEVICE_DEBUG) {
		hr = ID3D11Device_QueryInterface(g.device, &IID_ID3D11InfoQueue, (void**)&g.info);
		if (SUCCEEDED(hr)) {

			{
				D3D11_MESSAGE_ID id = D3D11_MESSAGE_ID_OMSETRENDERTARGETS_UNBINDDELETINGOBJECT;
				D3D11_INFO_QUEUE_FILTER filter = {
					.DenyList.NumIDs = 1,
					.DenyList.pIDList = &id,
				};
				hr = ID3D11InfoQueue_AddStorageFilterEntries(g.info, &filter);
				Assert(SUCCEEDED(hr));
			}

			ID3D11InfoQueue_SetBreakOnSeverity(g.info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			ID3D11InfoQueue_SetBreakOnSeverity(g.info, D3D11_MESSAGE_SEVERITY_WARNING,    TRUE);
			ID3D11InfoQueue_SetBreakOnSeverity(g.info, D3D11_MESSAGE_SEVERITY_ERROR,      TRUE);
		}
	}

	{
		D3D11_DEPTH_STENCIL_DESC desc = {
			.DepthEnable                  = FALSE,
			.DepthWriteMask               = D3D11_DEPTH_WRITE_MASK_ALL,
			.DepthFunc                    = D3D11_COMPARISON_GREATER,
			.StencilEnable                = FALSE,
			.StencilReadMask              = D3D11_DEFAULT_STENCIL_READ_MASK,
			.StencilWriteMask             = D3D11_DEFAULT_STENCIL_WRITE_MASK,
			.FrontFace.StencilFailOp      = D3D11_STENCIL_OP_KEEP,
			.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR,
			.FrontFace.StencilPassOp      = D3D11_STENCIL_OP_KEEP,
			.FrontFace.StencilFunc        = D3D11_COMPARISON_ALWAYS,
			.BackFace.StencilFailOp       = D3D11_STENCIL_OP_KEEP,
			.BackFace.StencilDepthFailOp  = D3D11_STENCIL_OP_DECR,
			.BackFace.StencilPassOp       = D3D11_STENCIL_OP_KEEP,
			.BackFace.StencilFunc         = D3D11_COMPARISON_ALWAYS,
		};

		hr = ID3D11Device_CreateDepthStencilState(g.device, &desc, &g.default_depth_stencil);
		Assert(SUCCEEDED(hr));
	}


	{
		D3D11_RASTERIZER_DESC desc = {
			.FillMode               = D3D11_FILL_SOLID,
			.CullMode               = D3D11_CULL_NONE,
			.FrontCounterClockwise  = FALSE,
			.DepthBias              = D3D11_DEFAULT_DEPTH_BIAS,
			.DepthBiasClamp         = D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
			.SlopeScaledDepthBias   = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			.DepthClipEnable        = FALSE,
			.ScissorEnable          = TRUE,
			.MultisampleEnable      = FALSE,
			.AntialiasedLineEnable  = FALSE,
		};
		hr = ID3D11Device_CreateRasterizerState(g.device, &desc, &g.default_rasterizer);
		Assert(SUCCEEDED(hr));
	}
	// BLENDERS
	{
		D3D11_BLEND_DESC desc = {
			.RenderTarget[0].BlendEnable = FALSE,
			.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
		};
		hr = ID3D11Device_CreateBlendState(g.device, &desc, &g.blenders[GFX_BLENDER_DISABLED]);
		Assert(SUCCEEDED(hr));
	}
	{
		D3D11_BLEND_DESC desc = {
			.RenderTarget[0].BlendEnable           = TRUE,
			.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA,
			.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA,
			.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD,
			.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ZERO,
			.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO,
			.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD,
			.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
		};
		hr = ID3D11Device_CreateBlendState(g.device, &desc, &g.blenders[GFX_BLENDER_ALPHA_BLEND]);
		Assert(SUCCEEDED(hr));
	}
	{
		D3D11_BLEND_DESC desc = {
			.RenderTarget[0].BlendEnable           = TRUE,
			.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE,
			.RenderTarget[0].DestBlend             = D3D11_BLEND_ONE,
			.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD,
			.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ZERO,
			.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE,
			.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD,
			.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
		};
		hr = ID3D11Device_CreateBlendState(g.device, &desc, &g.blenders[GFX_BLENDER_ADDITIVE]);
		Assert(SUCCEEDED(hr));
	}
	// SAMPLERS
	{
		D3D11_SAMPLER_DESC desc = {
			.ComparisonFunc  = D3D11_COMPARISON_NEVER,
			.AddressU        = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV        = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW        = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MinLOD          = 0,
			.MaxLOD          = D3D11_FLOAT32_MAX,
		};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		hr = ID3D11Device_CreateSamplerState(g.device, &desc, &g.samplers[GRAPHICS_SAMPLER_POINT]);
		Assert(SUCCEEDED(hr));

		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		hr = ID3D11Device_CreateSamplerState(g.device, &desc, &g.samplers[GRAPHICS_SAMPLER_LINEAR]);
		Assert(SUCCEEDED(hr));
	}
	// VERTEX BUFFER
	{
		d3d_ensure_vertex_buffer_capacity(renderer, VBUFFER_INITIAL_CAPACITY);
	}
	// CONSTANT BUFFER
	{
		// must be 16 byte aligned
		i32 size = sizeof(_CBUFFER) + 15 & ~15;
		D3D11_BUFFER_DESC desc = {
			.ByteWidth      = size,
			.Usage          = D3D11_USAGE_DYNAMIC,
			.BindFlags      = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		};
		hr = ID3D11Device_CreateBuffer(g.device, &desc, 0, &g.cbuffer);
		Assert(SUCCEEDED(hr));
	}

	// SDF SHADER
	{
		ID3D11PixelShader  *pshader = NULL;
		ID3D11VertexShader *vshader = NULL;
		ID3D11InputLayout  *ilayout = NULL;

		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_RectSDF, sizeof(g_PS_RectSDF), NULL, &pshader);
		Assert(SUCCEEDED(hr));

		hr = ID3D11Device_CreateVertexShader(g.device, g_VS_Rect, sizeof(g_VS_Rect), NULL, &vshader);
		Assert(SUCCEEDED(hr));


		D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
			{ "POS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "TEX",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COL",  1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COL",  2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COL",  3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "CRAD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "STY",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};

		hr = ID3D11Device_CreateInputLayout(g.device
		, layout_desc, ArrayCount(layout_desc), g_VS_Rect, sizeof(g_VS_Rect), &ilayout);
		Assert(SUCCEEDED(hr));

		g.vshader = vshader;
		g.ilayout = ilayout;

		g.pshaders[GFX_SHADER_SDF_RECT] = pshader;
	}

	// TEXT MASK SHADER
	{
		ID3D11PixelShader *pshader = NULL;

		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_TextMask, sizeof(g_PS_TextMask), NULL, &pshader);
		Assert(SUCCEEDED(hr));

		g.pshaders[GFX_SHADER_TEXT_MASK] = pshader;
	}

	// FINAL LINEAR-TO-SRGB BLIT
	{
		ID3D11PixelShader *pshader = NULL;

		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Blit, sizeof(g_PS_Blit), NULL, &pshader);
		Assert(SUCCEEDED(hr));

		g.pshaders[GFX_SHADER_BLIT] = pshader;
	}
	// BARREL DISTORTION
	{
		ID3D11PixelShader *pshader = NULL;

		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Barrel, sizeof(g_PS_Barrel), NULL, &pshader);
		Assert(SUCCEEDED(hr));

		g.pshaders[GFX_SHADER_BARREL] = pshader;
	}
	// SEPARABLE GAUSSIAN BLUR
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Gaussian, sizeof(g_PS_Gaussian), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_GAUSSIAN] = pshader;
	}
	// TEXTURE COPY
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Copy, sizeof(g_PS_Copy), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_COPY] = pshader;
	}
	// BLURRED MATERIAL COMPOSITE
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_BlurMaterial, sizeof(g_PS_BlurMaterial), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_BLUR_MATERIAL] = pshader;
	}
	// #SHADER FROSTED GLASS COMPOSITE
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Glass, sizeof(g_PS_Glass), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_GLASS] = pshader;
	}

	// CRT SCANLINES AND MASK
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_CRTScanlines, sizeof(g_PS_CRTScanlines), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_CRT_SCANLINES] = pshader;
	}
	// LUMINANCE EXTRACTION
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Luminance, sizeof(g_PS_Luminance), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_LUMINANCE] = pshader;
	}
	// ANALOG REWIND
	{
		ID3D11PixelShader *pshader = NULL;
		hr = ID3D11Device_CreatePixelShader(g.device, g_PS_Rewind, sizeof(g_PS_Rewind), NULL, &pshader);
		Assert(SUCCEEDED(hr));
		g.pshaders[GFX_SHADER_REWIND] = pshader;
	}

	// Todo, free this?
	Image_rgba_u8 white_image = push_image_rgba_u8_filled(&g.main_arena, v2i(2,2), 255);
	g.fallback_texture = gfx_create_texture(renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8,
		.size = white_image.reso,
		.data = white_image.data,
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "fallback texture",
	});

	// INVARIANT PIPELINE STATE
	ID3D11DeviceContext_OMSetDepthStencilState(g.context, 0, 0);
	ID3D11DeviceContext_RSSetState(g.context, g.default_rasterizer);
	ID3D11DeviceContext_VSSetConstantBuffers(g.context, 0, 1, &g.cbuffer);
	ID3D11DeviceContext_PSSetConstantBuffers(g.context, 0, 1, &g.cbuffer);
	ID3D11DeviceContext_IASetPrimitiveTopology(g.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	ID3D11DeviceContext_VSSetShader(g.context, g.vshader, 0, 0);
	ID3D11DeviceContext_IASetInputLayout(g.context, g.ilayout);

	u32 stride = sizeof(GFX_RectInst);
	u32 offset = 0;
	ID3D11DeviceContext_IASetVertexBuffers(g.context, 0, 1, &g.vbuffer, &stride, &offset);
	return renderer;
}

GFX_Window *gfx_create_window(Arena *owner, GFX_Renderer *renderer, OS_Window *os_window)
{
	Assert(owner);
	Assert(renderer);
	Assert(os_window);
	GFX_Window *window = arena_push_zero(owner, sizeof(*window));
	window->renderer = renderer;

	IDXGIDevice *device_dxgi = 0;
	HRESULT hr = ID3D11Device_QueryInterface(g.device, &IID_IDXGIDevice, (void **)&device_dxgi);
	Assert(SUCCEEDED(hr));
	IDXGIAdapter *adapter_dxgi = 0;
	hr = IDXGIDevice_GetAdapter(device_dxgi, &adapter_dxgi);
	Assert(SUCCEEDED(hr));
	IDXGIFactory2 *factory_dxgi = 0;
	hr = IDXGIAdapter_GetParent(adapter_dxgi, &IID_IDXGIFactory2, (void **)&factory_dxgi);
	Assert(SUCCEEDED(hr));
	DXGI_SWAP_CHAIN_DESC1 desc = {
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = 2,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
		.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
		.SampleDesc.Count = 1,
	};
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = {
		.RefreshRate.Numerator = 60,
		.RefreshRate.Denominator = 1,
		.Windowed = TRUE,
	};
	HWND hwnd = os_window_native_handle(os_window);
	hr = IDXGIFactory2_CreateSwapChainForHwnd(factory_dxgi, (IUnknown *)g.device, hwnd, &desc, &fullscreen, 0, (IDXGISwapChain1 **)&window->swapchain);
	Assert(SUCCEEDED(hr));
	RELEASE(factory_dxgi);
	RELEASE(adapter_dxgi);
	RELEASE(device_dxgi);
	window->output.base.renderer = renderer;
	window->output.base.bind_flags = GFX_TEXTURE_BIND_OUTPUT;
	return window;
}

APIFUNC
GFX_Texture *r_get_fallback_texture(GFX_Renderer *renderer)
{
	return g.fallback_texture;
}


APIFUNC
void r_resize_output_targets(GFX_Window *window, vec2i reso) {
	GFX_Renderer *renderer = window->renderer;
	if (window->output.base.reso.x != reso.x || window->output.base.reso.y != reso.y) {
		window->output.base.reso = reso;

		RELEASE(window->output.output_view);
		RELEASE(window->output.texture);

		IDXGISwapChain_ResizeBuffers(window->swapchain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

		IDXGISwapChain_GetBuffer(window->swapchain, 0, &IID_ID3D11Texture2D, (void **)(&window->output.texture));

		ID3D11Device_CreateRenderTargetView(g.device, window->output.as_resource, 0, &window->output.output_view);
	}
}

APIFUNC
void r_clear_output(GFX_Renderer *renderer, GFX_Texture *output, Color_SRGBA color) {
	Assert(output);

	D3D_Texture *texture = (D3D_Texture *) output;
	ID3D11RenderTargetView *render_target_view = texture->output_view;

	Assert(render_target_view);

	Color_Linear linear = color_linear_from_srgba(color);
	f32 clear_color[] = { linear.x, linear.y, linear.z, linear.w };
	ID3D11DeviceContext_ClearRenderTargetView(g.context, render_target_view, clear_color);
}

// Clair De Lune 10/16/2025
void gfx_present_window(GFX_Window *window)
{
	GFX_Renderer *renderer = window->renderer;
	IDXGISwapChain_Present(window->swapchain, 1u, 0);
	// output target is unbound on present
	g.state.output = 0;
}

static void update_pipeline_state(GFX_Renderer *renderer, D3D_Pipeline state) {
	Assert(state.sampler != GRAPHICS_SAMPLER_NONE);
	Assert(state.pshader != 0);
	Assert(state.texture);


	if (g.state.output != state.output) {

		if (g.state.texture == state.output) {
			ID3D11ShaderResourceView *nullptr_shader_resource_view = NULL;
			ID3D11DeviceContext_PSSetShaderResources(g.context, 0, 1, &nullptr_shader_resource_view);
		}

		D3D_Texture *texture = (D3D_Texture *) state.output;
		ID3D11RenderTargetView *render_target_view = texture->output_view;
		ID3D11DeviceContext_OMSetRenderTargets(g.context, 1, &render_target_view, 0);
	}

	if (g.state.blender != state.blender) {
		ID3D11BlendState *blender = g.blenders[state.blender];
		ID3D11DeviceContext_OMSetBlendState(g.context, blender, 0, 0xffffffff);
	}

	if (!rect_i32_equal(g.state.viewport, state.viewport)) {
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, state.viewport.w, state.viewport.h, 0.0f, 1.0f };
		ID3D11DeviceContext_RSSetViewports(g.context, 1, &viewport);
	}

	if (!rect_i32_equal(g.state.scissor, state.scissor)) {
		D3D11_RECT rect = {
			.left   = state.scissor.x,
			.right  = state.scissor.x + state.scissor.w,
			.top    = state.scissor.y,
			.bottom = state.scissor.y + state.scissor.h,
		};
		ID3D11DeviceContext_RSSetScissorRects(g.context, 1, &rect);
	}

	if (g.state.pshader != state.pshader) {
		ID3D11PixelShader *pshader = state.pshader;
		ID3D11DeviceContext_PSSetShader(g.context, pshader, 0, 0);
	}

	if (g.state.texture != state.texture) {
		D3D_Texture *texture = (D3D_Texture *) state.texture;
		ID3D11ShaderResourceView *shader_resource_view = texture->input_view;

		ID3D11DeviceContext_PSSetShaderResources(g.context, 0, 1, &shader_resource_view);
	}

	if (g.state.sampler != state.sampler) {
		ID3D11SamplerState *sampler = g.samplers[state.sampler];
		ID3D11DeviceContext_PSSetSamplers(g.context, 0, 1, &sampler);
	}

	g.state = state;
}

static void d3d_mapped_write_discard(GFX_Renderer *renderer, ID3D11Resource *resource, void *data, u32 size)
{
	D3D11_MAPPED_SUBRESOURCE mapped;

	HRESULT hr = ID3D11DeviceContext_Map(g.context, resource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	Assert(SUCCEEDED(hr));

	CopyMemory(mapped.pData, data, size);

	ID3D11DeviceContext_Unmap(g.context, resource, 0);
}

static ID3D11Resource *r_resource_from_d3d_buffer(ID3D11Buffer *buffer) {
	return (ID3D11Resource *) buffer;
}

static ID3D11Resource *r_resource_from_d3d_texture(ID3D11Texture2D *texture) {
	return (ID3D11Resource *) texture;
}

void gfx_submit_draw(GFX_Renderer *renderer, GFX_DrawData draw) {
	Assert(draw.pass_count > 0);
	if (draw.instances_size > 0)
	{
		d3d_ensure_vertex_buffer_capacity(renderer, draw.instances_size);
		d3d_mapped_write_discard(renderer, r_resource_from_d3d_buffer(g.vbuffer),
			draw.instances, draw.instances_size);
	}

	for (u32 pass_index = 0; pass_index < draw.pass_count; ++pass_index)
	{
		GFX_Pass *pass = &draw.passes[pass_index];
		if (pass->desc.clear) {
			r_clear_output(renderer, pass->desc.output, pass->desc.clear_color);
		}
		for (u32 i = pass->batch_offset; i < pass->batch_offset + pass->batch_count; ++i)
		{
		GFX_BatchDesc desc = draw.batches[i].desc;
		Assert(desc.texture != pass->desc.output);
		u32 offset = draw.batches[i].instance_offset;
		u32 length = draw.batches[i].instance_count;

		{
			D3D_Pipeline pipeline_state = {
				.output   = pass->desc.output,
				.texture  = desc.texture,
				.viewport = pass->desc.viewport,
				.scissor  = desc.scissor,
				.sampler  = desc.sampler,
				.blender  = desc.blender,
				.pshader  = g.pshaders[desc.shader],
				.vbuffer  = g.vbuffer,
			};
			update_pipeline_state(renderer, pipeline_state);
		}
		{
			vec2 viewport_size = v2_from_v2i(pass->desc.viewport.size);
			Matrix transform = graphics_matrix_for_ndc_transform(viewport_size);
			_CBUFFER cdata = {
				.transform     = transform,
				.mixer         = desc.texture_mode == GRAPHICS_TEXTURE_MASK ?
					MIXER_RRRR : MIXER_RGBA,
				.texture0_reso = v2_from_v2i(desc.texture->reso),
				.custom = desc.shader_block,
			};
			d3d_mapped_write_discard(renderer, r_resource_from_d3d_buffer(g.cbuffer), &cdata, sizeof(cdata));
		}

		ID3D11DeviceContext_DrawInstanced(g.context, 4, length, 0, offset);
		}
	}
}

void gfx_update_texture(GFX_Texture *pub, GFX_TextureUpdateParams p)
{
	Assert(pub);
	GFX_Renderer *renderer = pub->renderer;
	Assert(pub->format != GRAPHICS_FORMAT_NONE);
	Assert(p.data);
	Assert(pub != g.state.output);
	// TODO(RJ) look into whether this causes contention for our cases
	// Assert(pub != g.state.texture);
	Assert(p.dest.x >= 0 && p.dest.y >= 0);
	Assert(p.size.x > 0 && p.size.y > 0);
	Assert(p.dest.x + p.size.x <= pub->reso.x);
	Assert(p.dest.y + p.size.y <= pub->reso.y);

	D3D_Texture *tex = d3d_texture_from_texture(pub);
	ID3D11Resource *res = r_resource_from_texture(tex);
	u32 bytes_per_pixel = pub->format & 255;
	u32 row_size = p.size.x * bytes_per_pixel;
	Assert(p.stride >= row_size);

	if (pub->usage == GRAPHICS_TEXTURE_USAGE_PER_FRAME)
	{
		// D3D11 dynamic textures are not valid UpdateSubresource destinations.
		// Discard-map the complete image and honor the driver's row pitch.
		Assert(p.dest.x == 0 && p.dest.y == 0);
		Assert(p.size.x == pub->reso.x && p.size.y == pub->reso.y);
		D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
		HRESULT hr = ID3D11DeviceContext_Map(g.context, res, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		Assert(SUCCEEDED(hr));
		for (i32 row = 0; row < p.size.y; row ++) {
			memory_copy((u8 *)mapped.pData + row * mapped.RowPitch, (u8 *)p.data + row * p.stride, row_size);
		}
		ID3D11DeviceContext_Unmap(g.context, res, 0);
	}
	else
	{
		Assert(pub->usage == GRAPHICS_TEXTURE_USAGE_RARE_UPDATES);
		D3D11_BOX box = {
			.left = p.dest.x,
			.top = p.dest.y,
			.right = p.dest.x + p.size.x,
			.bottom = p.dest.y + p.size.y,
			.front = 0,
			.back = 1,
		};
		ID3D11DeviceContext_UpdateSubresource(g.context, res, 0, &box,
			p.data, p.stride, 0);
	}
}

b32 gfx_read_texture(GFX_Texture *pub, void *data, u32 stride)
{
	Assert(pub);
	Assert(data);
	GFX_Renderer *renderer = pub->renderer;
	D3D_Texture *texture = d3d_texture_from_texture(pub);
	u32 row_size = pub->reso.x * (pub->format & 255);
	Assert(stride >= row_size);

	if (!texture->readback)
	{
		D3D11_TEXTURE2D_DESC desc;
		ID3D11Texture2D_GetDesc(texture->texture, &desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		HRESULT hr = ID3D11Device_CreateTexture2D(g.device, &desc, 0, &texture->readback);
		if (FAILED(hr)) {
			return false;
		}
	}

	if (g.state.output == pub)
	{
		ID3D11DeviceContext_OMSetRenderTargets(g.context, 0, 0, 0);
		g.state.output = 0;
	}
	if (g.state.texture == pub)
	{
		ID3D11ShaderResourceView *null_view = 0;
		ID3D11DeviceContext_PSSetShaderResources(g.context, 0, 1, &null_view);
		g.state.texture = 0;
	}
	ID3D11DeviceContext_CopyResource(g.context, (ID3D11Resource *)texture->readback, texture->as_resource);
	D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
	HRESULT hr = ID3D11DeviceContext_Map(g.context, (ID3D11Resource *)texture->readback, 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr)) {
		return false;
	}
	for (i32 row = 0; row < pub->reso.y; ++row) {
		memory_copy((u8 *)data + row * stride, (u8 *)mapped.pData + row * mapped.RowPitch, row_size);
	}
	ID3D11DeviceContext_Unmap(g.context, (ID3D11Resource *)texture->readback, 0);
	return true;
}

static GFX_Texture *d3d_new_texture(GFX_Renderer *renderer, GFX_TextureUsage usage, GFX_TextureBindFlags bind_flags, GFX_Format format, vec2i size, u32 stride, void *data)
{
	// I've had problems with 1x1 textures in the past, some drivers seem to mishandle 1x1 shader-resource textures.
	// Keep the minimum at 2x2 even though D3D11 nominally permits a one-pixel dimension.
	Assert(size.x > 1 && size.x < (u16) ~0);
	Assert(size.y > 1 && size.y < (u16) ~0);
	Assert(usage == GRAPHICS_TEXTURE_USAGE_RARE_UPDATES || usage == GRAPHICS_TEXTURE_USAGE_PER_FRAME);
	Assert(bind_flags & (GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT));
	Assert(usage != GRAPHICS_TEXTURE_USAGE_PER_FRAME || !(bind_flags & GFX_TEXTURE_BIND_OUTPUT));

	ID3D11Texture2D *texture = NULL;

	DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
	switch (format)
	{
		case GRAPHICS_FORMAT_R_U8:         dxgi_format = DXGI_FORMAT_R8_UNORM; break;
		case GRAPHICS_FORMAT_RGBA_U8:      dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
		case GRAPHICS_FORMAT_RGBA_U8_SRGB: dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
		case GRAPHICS_FORMAT_RGBA_F32:     dxgi_format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
		default: break;
	}
	Assert(dxgi_format != DXGI_FORMAT_UNKNOWN);
	HRESULT hr;
	{
		D3D11_TEXTURE2D_DESC texture_desc = {
			.Width              = size.x,
			.Height             = size.y,
			.MipLevels          = 1,
			.ArraySize          = 1,
			.Format             = dxgi_format,
			.SampleDesc.Count   = 1,
			.SampleDesc.Quality = 0,
			.BindFlags          = (bind_flags & GFX_TEXTURE_BIND_INPUT ? D3D11_BIND_SHADER_RESOURCE : 0) |
				(bind_flags & GFX_TEXTURE_BIND_OUTPUT ? D3D11_BIND_RENDER_TARGET : 0),
			.MiscFlags          = 0,
		};
		if (usage == GRAPHICS_TEXTURE_USAGE_PER_FRAME)
		{
			texture_desc.Usage = D3D11_USAGE_DYNAMIC;
			texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		}
		else
		{
			texture_desc.Usage = D3D11_USAGE_DEFAULT;
		}
		if (data)
		{
			D3D11_SUBRESOURCE_DATA data_desc = {
				.pSysMem = data,
				.SysMemPitch = stride,
			};
			hr = ID3D11Device_CreateTexture2D(g.device, &texture_desc,
				&data_desc, &texture);
		}
		else
		{
			hr = ID3D11Device_CreateTexture2D(g.device, &texture_desc, NULL,
				&texture);
		}
		Assert(SUCCEEDED(hr));
	}

	ID3D11ShaderResourceView *shader_resource_view = NULL;
	if (bind_flags & GFX_TEXTURE_BIND_INPUT)
	{
		hr = ID3D11Device_CreateShaderResourceView(g.device, r_resource_from_d3d_texture(texture), 0, &shader_resource_view);
		Assert(SUCCEEDED(hr));
	}
	ID3D11RenderTargetView *render_target_view = NULL;
	if (bind_flags & GFX_TEXTURE_BIND_OUTPUT)
	{
		hr = ID3D11Device_CreateRenderTargetView(g.device, r_resource_from_d3d_texture(texture), 0, &render_target_view);
		Assert(SUCCEEDED(hr));
	}

	D3D_Texture *tex;
	if (g.free_textures)
	{
		tex = g.free_textures;
		g.free_textures = tex->next_free;
		memory_zero(tex, sizeof(*tex));
	}
	else
	{
		tex = arena_push_zero(&g.main_arena, sizeof(*tex));
	}
	r__init_base_texture(&tex->base, usage, bind_flags, format, size);
	tex->base.renderer = renderer;

	tex->input_view = shader_resource_view;
	tex->output_view = render_target_view;
	tex->texture = texture;

	return &tex->base;
}

APIFUNC
GFX_Texture *gfx_create_texture(GFX_Renderer *renderer, GFX_TextureDesc desc)
{
	u32 stride = desc.size.x * (desc.format & 255);
	GFX_Texture *texture = d3d_new_texture(renderer, desc.usage, desc.bind_flags, desc.format,
		desc.size, stride, desc.data);
	texture->sampler = desc.sampler != GRAPHICS_SAMPLER_NONE
		? desc.sampler : GRAPHICS_SAMPLER_LINEAR;
	texture->label = desc.label;
	return texture;
}

static b32 gfx_texture_desc_match(GFX_TextureDesc a, GFX_TextureDesc b)
{
	return a.usage == b.usage &&
		a.bind_flags == b.bind_flags &&
		a.format == b.format &&
		a.size.x == b.size.x &&
		a.size.y == b.size.y &&
		a.sampler == b.sampler;
}

void r_begin_frame(GFX_Renderer *renderer)
{
	Assert(renderer);
	g.frame_index += 1;
	Assert(g.frame_index);
}

GFX_Texture *gfx_acquire_transient_texture(GFX_Renderer *renderer, GFX_TextureDesc desc)
{
	Assert(renderer);
	Assert(g.frame_index);
	Assert(!desc.data);
	GFX_TransientTexture *available = 0;
	for (GFX_TransientTexture *entry = g.transient_textures; entry; entry = entry->next)
	{
		if (entry->acquired_frame != g.frame_index && gfx_texture_desc_match(entry->desc, desc))
		{
			entry->acquired_frame = g.frame_index;
			entry->texture->label = desc.label;
			return entry->texture;
		}
		if (entry->acquired_frame != g.frame_index && (!available || entry->acquired_frame < available->acquired_frame)) {
			available = entry;
		}
	}

	GFX_TransientTexture *entry = available;
	if (entry) {
		gfx_destroy_texture(entry->texture);
	} else {
		entry = arena_push_zero(&g.main_arena, sizeof(*entry));
		entry->next = g.transient_textures;
		g.transient_textures = entry;
	}
	entry->desc = desc;
	entry->texture = gfx_create_texture(renderer, desc);
	entry->acquired_frame = g.frame_index;
	return entry->texture;
}

void gfx_destroy_texture(GFX_Texture *pub)
{
	if (!pub) return;
	GFX_Renderer *renderer = pub->renderer;
	Assert(pub != g.fallback_texture);
	Assert(pub->format != GRAPHICS_FORMAT_NONE);

	if (g.state.texture == pub)
	{
		ID3D11ShaderResourceView *null_view = 0;
		ID3D11DeviceContext_PSSetShaderResources(g.context, 0, 1, &null_view);
		g.state.texture = 0;
	}
	D3D_Texture *texture = d3d_texture_from_texture(pub);
	RELEASE(texture->input_view);
	RELEASE(texture->output_view);
	RELEASE(texture->readback);
	RELEASE(texture->texture);
	memory_zero(texture, sizeof(*texture));
	texture->next_free = g.free_textures;
	g.free_textures = texture;
}

