// Default Shader from creating Unlit.
// Modified to handle SBS texture using which eye is active in VR.
//
// unity_StereoEyeIndex is the variable for which eye is active.

Shader "Unlit/sbsShader"
{
	Properties
	{
		_MainTex ("_bothEyes Texture", 2D) = "grey" {}

		_Coefficient("Screen Curve", Range(0.0,10.0)) = 5.0
	}
	SubShader
	{
		Tags { "RenderType"="Opaque" }
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

			float _Coefficient;

			// Add screen curve as: https://www.bitshiftprogrammer.com/2018/04/curved-surface-shader-unity.html
			//
			// That uses an exponential, and we want a simple circular curve, so formula has changed to quadratic.
			// y = sqrt(r^2 - x^2) In this case, y is Z, as the depth toward the screen. 
			// The radius will be from player to center of screen. We only change Z parameter.
			//
			// We want to add only the delta that the curve provides, so that at x=0 it doesn't move.
			// The exp conversion of coeefficent to radius is to make the manual curve setting feel more 
			// linear.  The 200 is scaling to make it flat at coefficent=0, and the 4 is half screen width,
			// which is the minimum the radius can be without clipping.

			float4 curveIt(float4 v)
			{
				float4 world = mul(unity_ObjectToWorld, v);
				
				float radius = exp(-_Coefficient) * 200 + 4;
				world.z += sqrt(radius*radius - world.x*world.x) - radius;

				return mul(unity_WorldToObject, world);
			}

			v2f vert (appdata v)
			{
				v2f o;
				float4 sb;

				// Modify uv fetched, based on the active eye,
				// by applying a different bias/offset to alternate eyes.
				// The net effect here is to show either the right or
				// left half of the incoming Texture, half for each eye.
				// This is called twice, even for SinglePassStereo

				sb.x = 0.5;									// Scale by half as it's 2x width
				sb.y = 1.0;									// No vertical scaling, full size.
				sb.z = unity_StereoEyeIndex ? 0.0 : 0.5;	// Offset to half for left eye
				sb.w = 0.0;									// No vertical offset.
				v.uv = UnityStereoScreenSpaceUVAdjust(v.uv, sb);
					
                //o.vertex = UnityObjectToClipPos(v.vertex);
				o.vertex = UnityObjectToClipPos(curveIt(v.vertex));
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
