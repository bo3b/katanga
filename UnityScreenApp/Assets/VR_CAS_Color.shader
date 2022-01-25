// == Contrast Adaptive Sharpening =============================================
// LICENSE
// =======
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE

//Initial port to ReShade: SLSNe    https://gist.github.com/SLSNe/bbaf2d77db0b2a2a0755df581b3cf00c

//Optimizations by Marty McFly:
//	vectorized math, even with scalar gcn hardware this should work
//	out the same, order of operations has not changed
//	For some reason, it went from 64 to 48 instructions, a lot of MOV gone
//	Also modified the way the final window is calculated
//
//	reordered min() and max() operations, from 11 down to 9 registers    
//
//	restructured final weighting, 49 -> 48 instructions
//
//	delayed RCP to replace SQRT with RSQRT
//
//	removed the saturate() from the control var as it is clamped
//	by UI manager already, 48 -> 47 instructions
//
//	replaced tex2D with tex2Doffset intrinsic (address offset by immediate integer)
//	47 -> 43 instructions
//	9 -> 8 registers

//Further modified by OopyDoopy and Lord of Lunacy:
//	Changed wording in the UI for the existing variable and added a new variable and relevant code to adjust sharpening strength.

//Fix by Lord of Lunacy:
//	Made the shader use a linear colorspace rather than sRGB, as recommended by the original AMD documentation from FidelityFX.

//Modified by CeeJay.dk:
//	Included a label and tooltip description. I followed AMDs official naming guidelines for FidelityFX.
//
//	Used gather trick to reduce the number of texture operations by one (9 -> 8). It's now 42 -> 51 instructions but still faster
//	because of the texture operation that was optimized away.

//Modified by Bo3b for Unity use:
//	Directly used in replacement of our prior Prism sharpening.
//	Runs on the entire VR view, not just screen.

Shader "Unlit/VR_CAS_Color" {

	Properties{
		_MainTex("", 2D) = "white" {}
	}

	CGINCLUDE
#include "UnityCG.cginc" 
#pragma target 4.0

	uniform float CASContrast /*<
		ui_category = "Sharpening";
		ui_type = "drag";
		ui_label = "Contrast Adaptation";
		ui_tooltip = "Adjusts the range the shader adapts to high contrast (0 is not all the way off).  Higher values = more high contrast sharpening.";
		ui_min = 0.0; ui_max = 1.0;
	>*/ = 0.5;

	uniform float MaxDelta /*<
		ui_category = "Sharpening";
		ui_type = "drag";
		ui_label = "Max color delta";
		ui_tooltip = "The maximum amount the sharpened color value may differ from its original value. Lower values may help reduce oversharpening in scenes with small details.";
		ui_min = 0.0; ui_max = 1.0;
	>*/ = 1.0;

	uniform float Sharpening /*<
		ui_category = "Sharpening";
		ui_type = "drag";
		ui_label = "Sharpening intensity";
		ui_tooltip = "Adjusts sharpening intensity by averaging the original pixels to the sharpened result.  1.0 is the unmodified default.";
		ui_min = 0.0; ui_max = 1.0;
	>*/ = 0.0;

	//	#include "ReShade.fxh"
	//texture TexColor : COLOR;
	//sampler sTexColor{ Texture = TexColor; SRGBTexture = true; };	

//	SamplerState  sTexColor{ Texture = _MainTex; };
	sampler2D _MainTex;
	float4 _MainTex_ST;
	float4 _MainTex_TexelSize;


	float3 tex2Doffset(sampler2D samp, float2 coord, int2 offset)
	{
		//return _MainTex.Sample(samp, coord, offset);
		//return tex2D(samp, coord + offset);
		//float2 pixelSize = 1 / float2(WIDTH / HEIGHT);

		return tex2D(samp, coord + offset*_MainTex_TexelSize.xy).rgb;
	}

//	float3 CASPass(float4 vpos, float2 texcoord)
//	{
//		// fetch a 3x3 neighborhood around the pixel 'e',
//		//  a b c
//		//  d(e)f
//		//  g h i
//
//		float3 a = tex2Doffset(_MainTex, texcoord, int2(-1, -1)).rgb;
//		float3 b = tex2Doffset(_MainTex, texcoord, int2(0, -1)).rgb;
//		float3 c = tex2Doffset(_MainTex, texcoord, int2(1, -1)).rgb;
//		float3 d = tex2Doffset(_MainTex, texcoord, int2(-1, 0)).rgb;
//
//		float3 g = tex2Doffset(_MainTex, texcoord, int2(-1, 1)).rgb;
//
////#if __RENDERER__ >= 0xa000 // If DX10 or higher
//		float4 red_efhi = tex2Dgather(_MainTex, texcoord, 0);
//		float4 green_efhi = tex2Dgather(_MainTex, texcoord, 1);
//		float4 blue_efhi = tex2Dgather(_MainTex, texcoord, 2);
//
//		float3 e = float3(red_efhi.w, green_efhi.w, blue_efhi.w);
//		float3 f = float3(red_efhi.z, green_efhi.z, blue_efhi.z);
//		float3 h = float3(red_efhi.x, green_efhi.x, blue_efhi.x);
//		float3 i = float3(red_efhi.y, green_efhi.y, blue_efhi.y);
//
////#else // If DX9
////		float3 e = tex2D(sTexColor, texcoord).rgb;
////		float3 f = tex2Doffset(sTexColor, texcoord, int2(1, 0)).rgb;
////
////		float3 h = tex2Doffset(sTexColor, texcoord, int2(0, 1)).rgb;
////		float3 i = tex2Doffset(sTexColor, texcoord, int2(1, 1)).rgb;
////
////#endif    
//
//		// Soft min and max.
//		//  a b c             b
//		//  d e f * 0.5  +  d e f * 0.5
//		//  g h i             h
//		// These are 2.0x bigger (factored out the extra multiply).
//		float3 mnRGB = min(min(min(d, e), min(f, b)), h);
//		float3 mnRGB2 = min(mnRGB, min(min(a, c), min(g, i)));
//		mnRGB += mnRGB2;
//
//		float3 mxRGB = max(max(max(d, e), max(f, b)), h);
//		float3 mxRGB2 = max(mxRGB, max(max(a, c), max(g, i)));
//		mxRGB += mxRGB2;
//
//		// Smooth minimum distance to signal limit divided by smooth max.
//		float3 rcpMRGB = rcp(mxRGB);
//		float3 ampRGB = saturate(min(mnRGB, 2.0 - mxRGB) * rcpMRGB);
//
//		// Shaping amount of sharpening.
//		ampRGB = rsqrt(ampRGB);
//
//		float peak = -3.0 * CASContrast + 8.0;
//		float3 wRGB = -rcp(ampRGB * peak);
//
//		float3 rcpWeightRGB = rcp(4.0 * wRGB + 1.0);
//
//		//                          0 w 0
//		//  Filter shape:           w 1 w
//		//                          0 w 0  
//		float3 window = (b + d) + (f + h);
//		float3 outColor = saturate((window * wRGB + e) * rcpWeightRGB);
//		outColor = e + clamp(outColor - e, -MaxDelta, MaxDelta);
//
//		return lerp(e, outColor, Sharpening);
//	}

	float Min3(float x, float y, float z)
	{
		return min(x, min(y, z));
	}

	float Max3(float x, float y, float z)
	{
		return max(x, max(y, z));
	}

	float3 CASPass(float4 vpos : SV_Position, float2 texcoord : TexCoord) : SV_Target
	{ 
		// fetch a 3x3 neighborhood around the pixel 'e',
		//  a b c
		//  d(e)f
		//  g h i
//		float pixelX = ReShade::PixelSize.x;
//		float pixelY = ReShade::PixelSize.y;
		float pixelX = _MainTex_TexelSize.x;			
		float pixelY = _MainTex_TexelSize.y;

		float3 a = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(-pixelX, -pixelY), _MainTex_ST)).rgb;
		float3 b = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(0.0, -pixelY), _MainTex_ST)).rgb;
		float3 c = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(pixelX, -pixelY), _MainTex_ST)).rgb;
		float3 d = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(-pixelX, 0.0), _MainTex_ST)).rgb;
		float3 e = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord, _MainTex_ST)).rgb;
		float3 f = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(pixelX, 0.0), _MainTex_ST)).rgb;
		float3 g = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(-pixelX, pixelY), _MainTex_ST)).rgb;
		float3 h = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(0.0, pixelY), _MainTex_ST)).rgb;
		float3 i = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(texcoord + float2(pixelX, pixelY), _MainTex_ST)).rgb;

		// Soft min and max.
		//  a b c             b
		//  d e f * 0.5  +  d e f * 0.5
		//  g h i             h
		// These are 2.0x bigger (factored out the extra multiply).
		float mnR = Min3(Min3(d.r, e.r, f.r), b.r, h.r);
		float mnG = Min3(Min3(d.g, e.g, f.g), b.g, h.g);
		float mnB = Min3(Min3(d.b, e.b, f.b), b.b, h.b);

		float mnR2 = Min3(Min3(mnR, a.r, c.r), g.r, i.r);
		float mnG2 = Min3(Min3(mnG, a.g, c.g), g.g, i.g);
		float mnB2 = Min3(Min3(mnB, a.b, c.b), g.b, i.b);
		mnR = mnR + mnR2;
		mnG = mnG + mnG2;
		mnB = mnB + mnB2;

		float mxR = Max3(Max3(d.r, e.r, f.r), b.r, h.r);
		float mxG = Max3(Max3(d.g, e.g, f.g), b.g, h.g);
		float mxB = Max3(Max3(d.b, e.b, f.b), b.b, h.b);

		float mxR2 = Max3(Max3(mxR, a.r, c.r), g.r, i.r);
		float mxG2 = Max3(Max3(mxG, a.g, c.g), g.g, i.g);
		float mxB2 = Max3(Max3(mxB, a.b, c.b), g.b, i.b);
		mxR = mxR + mxR2;
		mxG = mxG + mxG2;
		mxB = mxB + mxB2;

		// Smooth minimum distance to signal limit divided by smooth max.
		float rcpMR = rcp(mxR);
		float rcpMG = rcp(mxG);
		float rcpMB = rcp(mxB);

		float ampR = saturate(min(mnR, 2.0 - mxR) * rcpMR);
		float ampG = saturate(min(mnG, 2.0 - mxG) * rcpMG);
		float ampB = saturate(min(mnB, 2.0 - mxB) * rcpMB);

		// Shaping amount of sharpening.
		ampR = sqrt(ampR);
		ampG = sqrt(ampG);
		ampB = sqrt(ampB);

		// Filter shape.
		//  0 w 0
		//  w 1 w
		//  0 w 0  
		float peak = -rcp(lerp(8.0, 5.0, saturate(Sharpening)));

		float wR = ampR * peak;
		float wG = ampG * peak;
		float wB = ampB * peak;

		float rcpWeightR = rcp(1.0 + 4.0 * wR);
		float rcpWeightG = rcp(1.0 + 4.0 * wG);
		float rcpWeightB = rcp(1.0 + 4.0 * wB);

		float3 outColor = float3(saturate((b.r*wR + d.r*wR + f.r*wR + h.r*wR + e.r)*rcpWeightR),
								 saturate((b.g*wG + d.g*wG + f.g*wG + h.g*wG + e.g)*rcpWeightG),
								 saturate((b.b*wB + d.b*wB + f.b*wB + h.b*wB + e.b)*rcpWeightB));
		return outColor;
	}


	// =============================================================================

	// == Color adjustments ========================================================
	uniform float Contrast /*<
		ui_category = "Color adjustments";
		ui_type = "drag";
		ui_label = "Contrast";
		ui_min = 0.0; ui_max = 2.0;
	>*/ = 1.0;
	uniform float Brightness /*<
		ui_category = "Color adjustments";
		ui_type = "drag";
		ui_label = "Brightness";
		ui_min = 0.0; ui_max = 2.0;
	>*/ = 1.0;
	uniform float Saturation /*<
		ui_category = "Color adjustments";
		ui_type = "drag";
		ui_label = "Color saturation";
		ui_min = 0.0; ui_max = 2.0;
	>*/ = 1.0;

	float3 VRColorAdjustmentPass(float3 color)
	{
		color = lerp(0.5, color, Contrast) + (Brightness - 1);
		if (Saturation != 1.0)
		{
			float intensity = dot(float3(0.2125, 0.7154, 0.0721), color.rgb);
			color = lerp(intensity, color, Saturation);
		}
		return color;
	}
	// =============================================================================

	float3 VRSharpenColorPass(float4 vpos : SV_Position, float2 texcoord : TexCoord) : SV_Target
	{
		float3 color;
		//if (Sharpening != 0)
			color = CASPass(vpos, texcoord);
		//else
	//		color = tex2D(sTexColor, texcoord).rgb;
	//color = tex2D(_MainTex, texcoord).rgb;
	return color;
		//return VRColorAdjustmentPass(color);
	}

	// =============================================================================
	struct v2f
	{
		float4 vertex : SV_POSITION;
		float2 uv : TEXCOORD0;
	};
	struct appdata_t
	{
		float4 vertex : POSITION;
		float2 uv : TEXCOORD0;
	};

	// Vertex shader generating a triangle covering the entire screen
	//void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
	//{
	//	texcoord.x = (id == 2) ? 2.0 : 0.0;
	//	texcoord.y = (id == 1) ? 2.0 : 0.0;
	//	position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	//}
	v2f VertCAS(appdata_t v)
	{
		v2f o;
		o.vertex = UnityObjectToClipPos(v.vertex);
		o.uv = TRANSFORM_TEX(v.uv, _MainTex);
		o.uv = v.uv;

		//#if UNITY_UV_STARTS_AT_TOP
		//		o.uv2 = v.texcoord.xy;
		//		if (_MainTex_TexelSize.y < 0.0)
		//			o.uv2.y = 1.0 - o.uv2.y;
		//#endif

		return o;
	}
	v2f vert(appdata_t v)
	{
		v2f o;
		float4 sb;

		v.uv = UnityStereoScreenSpaceUVAdjust(v.uv, _MainTex_ST);

		o.vertex = UnityObjectToClipPos(v.vertex);
		o.uv = TRANSFORM_TEX(v.uv, _MainTex);

		return o;
	}

	ENDCG

		//technique VRSharpenColor <
		//	ui_label = "VR Image Sharpening and Color adjustment";
		//>
		SubShader{
			//Tags{ "RenderType" = "Opaque" }
			//LOD 100

			Pass {
			CGPROGRAM
			//VertexShader = PostProcessVS;
			#pragma vertex vert
			//PixelShader = VRSharpenColorPass;
			#pragma fragment VRSharpenColorPass
			//SRGBWriteEnable = true;
			ENDCG
		}
	}
}
