// Default Shader from creating Unlit.
// Modified to handle antialiasing the way Oculus recommends.
//
// Even though this gets called in stereo environment, this is specifically
// for the 2D display of the uDesktopDuplication.
//
// Same target _MainTex, but not looking for double width for each eye.
// This is a standalone shader, so that we don't have to look for stereo
// or 2D in every call, and pay any performance hit for that. We just
// swap out to this shader when in 2D mode.
//
// Our shader here is superior to the one that comes with uDesktopDuplicator,
// because we do the critical MSAA to avoid shimmer and sharpen the VR image.
// Also their shader has a lot of conflicting features like curving screen,
// thickness, invert image that we want to avoid.

Shader "Unlit/shader2D"
{
	Properties
	{
		[NoScaleOffset] _MainTex ("_bothEyes Texture", 2D) = "grey" {}
	}
	SubShader
	{
		Tags { "RenderType" = "Opaque" }
		LOD 100

		Pass
		{
			CGPROGRAM
			#pragma vertex vert
			#pragma fragment frag
			
			#include "UnityCG.cginc" 

			struct appdata
			{
				float4 vertex : POSITION;
				float2 uv : TEXCOORD0;
			};

			struct v2f
			{
				float2 uv : TEXCOORD0;
				float4 vertex : SV_POSITION;
			};

			sampler2D _MainTex;			
			float4 _MainTex_ST;

			v2f vert (appdata v)
			{
				v2f o;
				float4 sb;
					
                o.vertex = UnityObjectToClipPos(v.vertex);
				o.uv = TRANSFORM_TEX(v.uv, _MainTex);

				return o;
			}
			
			// Clear up shimmering using multisampling as described:
			// https://developer.oculus.com/blog/common-rendering-mistakes-how-to-find-them-and-how-to-fix-them/

			fixed4 tex2Dmultisample(sampler2D tex, float2 uv)
			{
				float2 dx = ddx(uv) * 0.25;
				float2 dy = ddy(uv) * 0.25;

				float4 sample0 = tex2D(tex, uv + dx + dy);
				float4 sample1 = tex2D(tex, uv + dx - dy);
				float4 sample2 = tex2D(tex, uv - dx + dy);
				float4 sample3 = tex2D(tex, uv - dx - dy);

				return (sample0 + sample1 + sample2 + sample3) * 0.25;
			}

			fixed4 frag (v2f i) : SV_Target
			{
				// sample the texture
				fixed4 col = tex2Dmultisample(_MainTex, i.uv);
				return col;
			}
			ENDCG
		}
	}
}
