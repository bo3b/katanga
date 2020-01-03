Shader "Hidden/PrismSharpen" {
	Properties {
		_MainTex ("", 2D) = "white" {}
	}

	CGINCLUDE
	
		#include "UnityCG.cginc"
		#include "PrismSharpen.cginc"
		#pragma fragmentoption ARB_precision_hint_fastest
		
		#pragma target 3.0
	ENDCG
	
	
	SubShader {
	//Tags {"Queue"="AlphaTest" "IgnoreProjector"="True" "RenderType"="Transparent"}

	ZTest Always Cull Off ZWrite Off
	
	//Combine
	//-----------------------------------------------------
	// Pass 0
	//-----------------------------------------------------		
	Pass{

		CGPROGRAM
		#pragma vertex vertPRISM
		#pragma fragment fragSharpen
		ENDCG			
	}

	//1 = median sharpen
	Pass {
		CGPROGRAM
		#pragma vertex vertPRISM
		#pragma fragment fragSharpenMedian
		ENDCG			
	}

	//2 = median sharpen fast
	Pass {
		CGPROGRAM
		#pragma vertex vertPRISM
		#pragma fragment fragSharpenMedianFast
		ENDCG			
	}
	
	
	
	
	
	
	
	}
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	FallBack off
}
