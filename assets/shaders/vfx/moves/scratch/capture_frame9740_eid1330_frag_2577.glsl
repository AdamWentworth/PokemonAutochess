/////////////////////////////// Source file 0/////////////////////////////
#version 450

#define FORCE_EARLY_Z layout(early_fragment_tests) in

#extension GL_ARB_shading_language_420pack : enable

#extension GL_ARB_explicit_attrib_location : enable
#define ATTRIBUTE_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y) layout(location = x, index = y)
#define UBO_BINDING(packing, x) layout(packing, binding = x)
#define SAMPLER_BINDING(x) layout(binding = x)
#define TEXEL_BUFFER_BINDING(x) layout(binding = x)
#define SSBO_BINDING(x) layout(std430, binding = x)
#define IMAGE_BINDING(format, x) layout(format, binding = x)

#define VARYING_LOCATION(x)

#extension GL_ARB_shader_storage_buffer_object : enable










#extension GL_ARB_derivative_control : enable
#extension GL_ARB_texture_query_levels : enable





precision highp sampler2DMSArray;

#define API_OPENGL 1
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define frac fract
#define lerp mix

/////////////////////////////// Source file 1/////////////////////////////
// Pixel Shader for TEV stages
// 1 TEV stages, 1 texgens, 0 IND stages
int idot(int3 x, int3 y)
{
	int3 tmp = x * y;
	return tmp.x + tmp.y + tmp.z;
}
int idot(int4 x, int4 y)
{
	int4 tmp = x * y;
	return tmp.x + tmp.y + tmp.z + tmp.w;
}

int  iround(float  x) { return int (round(x)); }
int2 iround(float2 x) { return int2(round(x)); }
int3 iround(float3 x) { return int3(round(x)); }
int4 iround(float4 x) { return int4(round(x)); }

SAMPLER_BINDING(0) uniform sampler2DArray samp[8];

UBO_BINDING(std140, 1) uniform PSBlock {
	int4 color[4];
	int4 k[4];
	int4 alphaRef;
	int4 texdim[8];
	int4 czbias[2];
	int4 cindscale[2];
	int4 cindmtx[6];
	int4 cfogcolor;
	int4 cfogi;
	float4 cfogf;
	float4 cfogrange[3];
	float4 czslope;
	float2 cefbscale;
	uint  bpmem_genmode;
	uint  bpmem_alphaTest;
	uint  bpmem_fogParam3;
	uint  bpmem_fogRangeBase;
	uint  bpmem_dstalpha;
	uint  bpmem_ztex_op;
	bool  bpmem_late_ztest;
	bool  bpmem_rgba6_format;
	bool  bpmem_dither;
	bool  bpmem_bounding_box;
	uint4 bpmem_pack1[16];
	uint4 bpmem_pack2[8];
	int4  konstLookup[32];
	bool  blend_enable;
	uint  blend_src_factor;
	uint  blend_src_factor_alpha;
	uint  blend_dst_factor;
	uint  blend_dst_factor_alpha;
	bool  blend_subtract;
	bool  blend_subtract_alpha;
	bool  logic_op_enable;
	uint  logic_op_mode;
	uint  time_ms;
};

#define bpmem_combiners(i) (bpmem_pack1[(i)].xy)
#define bpmem_tevind(i) (bpmem_pack1[(i)].z)
#define bpmem_iref(i) (bpmem_pack1[(i)].w)
#define bpmem_tevorder(i) (bpmem_pack2[(i)].x)
#define bpmem_tevksel(i) (bpmem_pack2[(i)].y)
#define samp_texmode0(i) (bpmem_pack2[(i)].z)
#define samp_texmode1(i) (bpmem_pack2[(i)].w)


int4 sampleTexture(uint texmap, in sampler2DArray tex, int2 uv, int layer) {
  float size_s = float(texdim[texmap].x * 128);
  float size_t = float(texdim[texmap].y * 128);
  float3 coords = float3(float(uv.x) / size_s, float(uv.y) / size_t, layer);
  return iround(255.0 * texture(tex, coords));
}

#define sampleTextureWrapper(texmap, uv, layer) sampleTexture(texmap, samp[texmap], uv, layer)
FRAGMENT_OUTPUT_LOCATION_INDEXED(0, 0) out vec4 ocol0;
FRAGMENT_OUTPUT_LOCATION_INDEXED(0, 1) out vec4 ocol1;
VARYING_LOCATION(0) in VertexData {
	 float4 pos;
	 float4 colors_0;
	 float4 colors_1;
	 float clipDist0;
	 float clipDist1;
	 float3 tex0;
};
struct DolphinLightData
{
	float3 position;
	float3 direction;
	float3 color;
	uint attenuation_type;
	float4 cosatt;
	float4 distatt;
};

struct DolphinFragmentInput
{
	vec4 color_0;
	vec4 color_1;
	int layer;
	vec3 normal;
	vec3 position;
	vec3 tex0;
	vec3 tex1;
	vec3 tex2;
	vec3 tex3;
	vec3 tex4;
	vec3 tex5;
	vec3 tex6;
	vec3 tex7;

	DolphinLightData[8] lights_chan0_color;
	DolphinLightData[8] lights_chan0_alpha;
	DolphinLightData[8] lights_chan1_color;
	DolphinLightData[8] lights_chan1_alpha;
	float4[2] ambient_lighting;
	float4[2] base_material;
	uint light_chan0_color_count;
	uint light_chan0_alpha_count;
	uint light_chan1_color_count;
	uint light_chan1_alpha_count;
};

struct DolphinFragmentOutput
{
	ivec4 main;
	ivec4 last_texture;
};

const uint CUSTOM_SHADER_LIGHTING_ATTENUATION_TYPE_NONE = 0u;
const uint CUSTOM_SHADER_LIGHTING_ATTENUATION_TYPE_POINT = 1u;
const uint CUSTOM_SHADER_LIGHTING_ATTENUATION_TYPE_DIR = 2u;
const uint CUSTOM_SHADER_LIGHTING_ATTENUATION_TYPE_SPOT = 3u;
void dolphin_process_emulated_fragment(in DolphinFragmentInput frag_input, out DolphinFragmentOutput frag_output)
{
	vec4 col0 = frag_input.color_0;
	vec4 col1 = frag_input.color_1;
	int layer = frag_input.layer;
	int4 c0 = color[1], c1 = color[2], c2 = color[3], prev = color[0];
	int4 rastemp = int4(0, 0, 0, 0), rawtextemp = int4(0, 0, 0, 0), textemp = int4(0, 0, 0, 0), konsttemp = int4(0, 0, 0, 0);
	int3 comp16 = int3(1, 256, 0), comp24 = int3(1, 256, 256*256);
	int alphabump=0;
	int3 tevcoord=int3(0, 0, 0);
	int2 wrappedcoord=int2(0,0), tempcoord=int2(0,0);
	int4 tevin_a=int4(0,0,0,0),tevin_b=int4(0,0,0,0),tevin_c=int4(0,0,0,0),tevin_d=int4(0,0,0,0);

	int2 fixpoint_uv0 = int2((frag_input.tex0.z == 0.0 ? frag_input.tex0.xy : frag_input.tex0.xy / frag_input.tex0.z) * float2(texdim[0].zw * 128));

	// TEV stage 0
	// indirect op
	int2 indtevtrans0 = int2(0, 0);
	wrappedcoord.x = fixpoint_uv0.x;
	wrappedcoord.y = fixpoint_uv0.y;
	tevcoord.xy = wrappedcoord + indtevtrans0;
	tevcoord.xy = (tevcoord.xy << 8) >> 8;
	rawtextemp = sampleTextureWrapper(0u, tevcoord.xy, layer);
	textemp = rawtextemp.rgba;
	tevin_a = int4(c1.rgb, c1.a)&int4(255, 255, 255, 255);
	tevin_b = int4(c0.rgb, c0.a)&int4(255, 255, 255, 255);
	tevin_c = int4(textemp.rgb, textemp.a)&int4(255, 255, 255, 255);
	tevin_d = int4(int3(0,0,0), 0);
	// color combine
	prev.rgb = clamp((((tevin_d.rgb)) + (((((tevin_a.rgb<<8) + (tevin_b.rgb-tevin_a.rgb)*(tevin_c.rgb+(tevin_c.rgb>>7)))) + 128)>>8)), int3(0,0,0), int3(255,255,255));
	// alpha combine
	prev.a = clamp((((tevin_d.a)) + (((((tevin_a.a<<8) + (tevin_b.a-tevin_a.a)*(tevin_c.a+(tevin_c.a>>7)))) + 128)>>8)), 0, 255);
	frag_output.last_texture = rawtextemp;
	frag_output.main = prev;
}
void process_fragment(in DolphinFragmentInput frag_input, out DolphinFragmentOutput frag_output)
{
	dolphin_process_emulated_fragment(frag_input, frag_output);
}
void main()
{
	float4 rawpos = gl_FragCoord;
	int layer = 0;
	DolphinFragmentInput frag_input;
	frag_input.color_0 = colors_0;
	frag_input.color_1 = colors_1;
	frag_input.layer = layer;
	frag_input.normal = vec3(0, 0, 0);
	frag_input.position = vec3(0, 0, 0);
	frag_input.tex0 = tex0;
	frag_input.tex1 = vec3(0, 0, 0);
	frag_input.tex2 = vec3(0, 0, 0);
	frag_input.tex3 = vec3(0, 0, 0);
	frag_input.tex4 = vec3(0, 0, 0);
	frag_input.tex5 = vec3(0, 0, 0);
	frag_input.tex6 = vec3(0, 0, 0);
	frag_input.tex7 = vec3(0, 0, 0);
	DolphinFragmentOutput frag_output;
	process_fragment(frag_input, frag_output);
	ivec4 prev = frag_output.main & 255;
	if(!( (prev.a >= alphaRef.r) && (prev.a <= alphaRef.g))) {
		ocol0 = float4(0.0, 0.0, 0.0, 0.0);
		ocol1 = float4(0.0, 0.0, 0.0, 0.0);
		discard;
	}
	// Hardware testing indicates that an alpha of 1 can pass an alpha test,
	// but doesn't do anything in blending
	if (prev.a == 1) prev.a = 0;
	int zCoord = int(rawpos.z * 16777216.0);
	zCoord = clamp(zCoord, 0, 0xFFFFFF);
	ocol0.rgb = float3(prev.rgb) / 255.0;
	ocol0.a = float(prev.a >> 2) / 63.0;
	ocol1 = float4(0.0, 0.0, 0.0, float(prev.a) / 255.0);
}
