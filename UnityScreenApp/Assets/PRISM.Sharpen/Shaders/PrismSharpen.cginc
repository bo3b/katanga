/*
																	 ;
		                                                            ;;     
                                                                   ;;;     
      PRISM ---All-In-One Post-Processing for Unity              .;;;;     
      Created by  Alex Blaikie, @Gadget_Games                   :;;;;;     
                                                               ;;;;;;;     
                                                              ;;;;;;;;     
                                                             ;;;;;;;;;     
                                                           ,;;;;;;;;;;     
                                                          ;;;;;;;;;;;;     
                                                         ;;;;;;;;;;;;,     
                                                        ;;;;;;;;;;;,,,     
                                                      `;;;;;;;;;;;,,,,     
                                                     ,;;;;;;;;;;,,,,,,     
                                                    ;;;;;;;;;;:,,,,,,,     
                                                   ;;;;;;;;;;,,,,,,,,,     
                                                  ;;;;;;;;;,,,,,,,,,,,     
                                                `;;;;;;;;:,,,,,,,,,,,,     
                                               :;;;;;;;;,,,,,,,,,,,,,,     
                                              ;;;;;;;;,,,,,,,,,,,,,,,,     
                                             ;;;;;;;:,,,,,,,,,,,,,,,,,     
                                            ;;;;;;;,,,,,,,,,,,,,,,,,,,     
                                          .;;;;;;,,,,,,,,,,,,,,,,,,,,,     
                                         :;;;;;;,,,,,,,,,,,,,,,,,,,,,,     
                                        ;;;;;;,,,,,,,,,,,,,,,,,,,,,,,:     
                                       ;;;;;,,,,,,,,,,,,,,,,,,,,,,:;;;     
                                      ;;;;;,,,,,,,,,,,,,,,,,,,,;;;;;;;     
                                    ,;;;;,,,,,,,,,,,,,,,,,,:;;;;;;;;;;     
                                   ;;;;,,,,,,,,,,,,,,,,,:;;;;;;;;;;;;;     
                                  ;;;;,,,,,,,,,,,,,,:;;;;;;;;;;;;;;;;;     
                                 ;;;,,,,,,,,,,,,,:;;;;;;;;;;;;;;;:.        
        @@@@@@@@;              `;;:,,,,,,,,,,:;;;;;;;;;;;;;;,              
        @@@@@@@@@@            ,;;,,,,,,,,,:;;;;;;;;;;;:`                   
        @@@@@@@@@@@          ;;,,,,,,,,;;;;;;;;;:.                         
        @@@@;:'@@@@#       @;:,,,,,:;;;;;;;:`                              
        @@@@    @@@#      ,@:,,,:;;;;:.                                    
     ,::@@@@::::@@@@,,,,,,#@@;;;,`                                         
        @@@@    ,@@@     #@@@'                                             
        @@@@    +@@@ '''':'''.'''`  ''#@@@;  +@@@,                         
        @@@@   .@@@@@@@@@'@@@,@@@`  #@@@@@@@:@@@@@#                        
        @@@@@@@@@@@,@@@@@'@@@,@@@.  #@@@@@@@@@@@@@@                        
        @@@@@@@@@@@ @@@@@'@@@,@@@@` #@@@@@@@@@@@@@@                        
        @@@@@@@@@+  @@@  '@@@, +@@@ #@@@ ;@@@  @@@@                        
        @@@@        @@@  '@@@,  @@@ #@@@ ;@@@  @@@@                        
        @@@@        @@@  '@@@, @@@@ #@@@ ;@@@  @@@@                        
        @@@@        @@@  '@@@,@@@@@ #@@@ ;@@@  @@@@                        
        @@@@        @@@  '@@@,@@@@` #@@@ ;@@@  @@@@                        
        @@@@        @@@  '@@@,@@;   #@@@ ;@@@  @@@@                        
*/                                                                           

	#define s2(a, b)				temp = a; a = min(a, b); b = max(temp, b);
	#define mn3(a, b, c)			s2(a, b); s2(a, c);
	#define mx3(a, b, c)			s2(b, c); s2(a, c);

	#define mnmx3(a, b, c)				mx3(a, b, c); s2(a, b);                                   // 3 exchanges
	#define mnmx4(a, b, c, d)			s2(a, b); s2(c, d); s2(a, c); s2(b, d);                   // 4 exchanges
	#define mnmx5(a, b, c, d, e)		s2(a, b); s2(c, d); mn3(a, c, e); mx3(b, d, e);           // 6 exchanges
	#define mnmx6(a, b, c, d, e, f) 	s2(a, d); s2(b, e); s2(c, f); mn3(a, b, c); mx3(d, e, f); // 7 exchanges
            
	static const float4 ONES = (float4)1.0;// float4(1.0, 1.0, 1.0, 1.0);
	static const float4 ZEROES = (float4)0.0;
	sampler2D _MainTex;
	half4 _MainTex_ST;
	half4 _MainTex_TexelSize;

	//Start Sharpen variables
	// -- Sharpening --
	//#define sharp_clamp    0.25  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel recieves - Default is 0.035
	// -- Advanced sharpening settings --
	//#define offset_bias 1.0  //[0.0 to 6.0] Offset bias adjusts the radius of the sampling pattern.
	                         //I designed the pattern for offset_bias 1.0, but feel free to experiment.

	uniform float offset_bias = 1.0;
	uniform float sharp_clamp = 1.0;
	uniform float _SharpenAmount = 1.0;
	//End sharpen variables

	// Mean of Rec. 709 & 601 luma coefficients
	#define lumacoeff        float3(0.2558, 0.6511, 0.0931)
	#define HALF_MAX 65504.0
	inline half3 SafeHDR(half3 c) { return min(c, HALF_MAX); }
	inline half4 SafeHDR(half4 c) { return min(c, HALF_MAX); }
	
	struct appdata_t {
		float4 vertex : POSITION;
		float2 texcoord : TEXCOORD0;
		float2 texcoord1 : TEXCOORD1;
	};

	struct v2f {
		float4 vertex : SV_POSITION;
		float2 uv : TEXCOORD0;
		
        #if UNITY_UV_STARTS_AT_TOP
			float2 uv2 : TEXCOORD1;
		#endif
	};
	
	float3 Sample(float2 uv, float2 offsets, float weight)//float mipbias - done with weight
	{
	    float2 PixelSize = _MainTex_TexelSize.xy;
	    return tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + offsets * PixelSize, _MainTex_ST)).rgb * weight;
	}    	

	v2f vertPRISM (appdata_t v)
	{
		v2f o;
		o.vertex = UnityObjectToClipPos(v.vertex);
		o.uv = v.texcoord;
		
    	#if UNITY_UV_STARTS_AT_TOP
    		o.uv2 = v.texcoord.xy;				
    		if (_MainTex_TexelSize.y < 0.0)
    			o.uv2.y = 1.0 - o.uv2.y;
    	#endif
		
		return o;
	}


	//lumacoeff instead
	//#define CoefLuma vec3(0.2126, 0.7152, 0.0722)      // BT.709 & sRBG luma coefficient (Monitors and HD Television)
	//#define CoefLuma vec3(0.299, 0.587, 0.114)       // BT.601 luma coefficient (SD Television)
	//#define CoefLuma vec3(1.0/3.0, 1.0/3.0, 1.0/3.0) // Equal weight coefficient
	///.. -1 ..
	///-1  5 -1
	///.. -1 ..
	//Tried prewitt sharpen - holy god that looks terrible. 
	/*
	LumaSharpen 1.4.1
	original hlsl by Christian Cann Schuldt Jensen ~ CeeJay.dk
	port to glsl by Anon
	It blurs the original pixel with the surrounding pixels and then subtracts this blur to sharpen the image.
	It does this in luma to avoid color artifacts and allows limiting the maximum sharpning to avoid or lessen halo artifacts.
	This is similar to using Unsharp Mask in Photoshop.
	*/
	half4 sharpen(float2 uv)
	{
		float4 colorInput = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv, _MainTex_ST));
		half2 PixelSize = _MainTex_TexelSize.xy;
	  	
		float3 ori = colorInput.rgb;

		// -- Combining the strength and luma multipliers --
		float3 sharp_strength_luma = (lumacoeff * _SharpenAmount); //I'll be combining even more multipliers with it later on
		
		// -- Gaussian filter --
		//   [ .25, .50, .25]     [ 1 , 2 , 1 ]
		//   [ .50,   1, .50]  =  [ 2 , 4 , 2 ]
	 	//   [ .25, .50, .25]     [ 1 , 2 , 1 ]
        float px = PixelSize.x;//1.0/
		float py = PixelSize.y;

		float3 blur_ori = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + float2(px, -py) * 0.5 * offset_bias, _MainTex_ST)).rgb; // South East
		blur_ori += tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + float2(-px, -py) * 0.5 * offset_bias, _MainTex_ST)).rgb;  // South West
		blur_ori += tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + float2(px, py) * 0.5 * offset_bias, _MainTex_ST)).rgb; // North East
		blur_ori += tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + float2(-px, py) * 0.5 * offset_bias, _MainTex_ST)).rgb; // North West

		blur_ori *= 0.25;  // ( /= 4) Divide by the number of texture fetches

		// -- Calculate the sharpening --
		float3 sharp = ori - blur_ori;  //Subtracting the blurred image from the original image

		// -- Adjust strength of the sharpening and clamp it--
		float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5); //Roll part of the clamp into the dot

		float sharp_luma = clamp((dot(float4(sharp,1.0), sharp_strength_luma_clamp)), 0.0,1.0 ); //Calculate the luma, adjust the strength, scale up and clamp
		sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp; //scale down

		// -- Combining the values to get the final sharpened pixel	--
		colorInput.rgb = colorInput.rgb + sharp_luma;    // Add the sharpening to the input color.
		
		return clamp(colorInput, 0.0,1.0);
	}
	
	half4 fragSharpen (v2f i) : SV_Target
	{
		#if UNITY_UV_STARTS_AT_TOP
		float2 uv = i.uv2;
		#else
		float2 uv = i.uv;
		#endif
		
		return sharpen(uv);
	}

	//Let's try reverse median.
	float4 fragSharpenMedian (v2f i) : SV_Target
	{
		float2 ooRes = _MainTex_TexelSize.xy;//_ScreenParams.w;

		float2 uv = i.uv;
		
		//This doesn't work on SM2
		#if SHADER_TARGET < 30        
		float3 ofs = _MainTex_TexelSize.xyx * float3(1, 1, 0);
		//
		float3 v[5];
		
		float4 midCol = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv, _MainTex_ST));
		
        v[0] = midCol;
        v[1] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv - ofs.xz, _MainTex_ST)).rgb;
        v[2] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + ofs.xz, _MainTex_ST)).rgb;
        v[3] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv - ofs.zy, _MainTex_ST)).rgb;
        v[4] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + ofs.zy, _MainTex_ST)).rgb;
		
		float3 temp;
		mnmx5(v[0], v[1], v[2], v[3], v[4]);
		mnmx3(v[1], v[2], v[3]);
		
		//return float4(SafeHDR(v[2].rgb), midCol.a);
		midCol.rgb = lerp(midCol.rgb, SafeHDR(v[2].rgb), -_SharpenAmount);

		return midCol;

		#else
		
		float4 midCol = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv, _MainTex_ST));
		
		// -1  1  1
		// -1  0  1 
		// -1 -1  1  //3x3=9	
		float3 v[9];

		// Add the pixels which make up our window to the pixel array.
		UNITY_UNROLL
		for(int dX = -1; dX <= 1; ++dX) 
		{
		UNITY_UNROLL
			for(int dY = -1; dY <= 1; ++dY) 
			{
				float2 ofst = float2(float(dX), float(dY));
				v[(dX + 1) * 3 + (dY + 1)] = (float3)tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + ofst * ooRes, _MainTex_ST)).rgb;
			}
		}

		float3 temp;

		// Starting with a subset of size 6, remove the min and max each time
		mnmx6(v[0], v[1], v[2], v[3], v[4], v[5]);
		mnmx5(v[1], v[2], v[3], v[4], v[6]);
		mnmx4(v[2], v[3], v[4], v[7]);
		mnmx3(v[3], v[4], v[8]);
			
		//return float4(SafeHDR(v[4].rgb), midCol.a);
		midCol.rgb = lerp(midCol.rgb, SafeHDR(v[4].rgb), -_SharpenAmount);

		return midCol;

		#endif
	}

	float4 fragSharpenMedianFast (v2f i) : SV_Target
	{
		float2 ooRes = _MainTex_TexelSize.xy;//_ScreenParams.w;

		float2 uv = i.uv;

		float3 ofs = _MainTex_TexelSize.xyx * float3(1, 1, 0);
		//
		float3 v[5];
		
		float4 midCol = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv, _MainTex_ST));
		
        v[0] = midCol;
        v[1] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv - ofs.xz, _MainTex_ST)).rgb;
        v[2] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + ofs.xz, _MainTex_ST)).rgb;
        v[3] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv - ofs.zy, _MainTex_ST)).rgb;
        v[4] = tex2D(_MainTex, UnityStereoScreenSpaceUVAdjust(uv + ofs.zy, _MainTex_ST)).rgb;
		
		float3 temp;
		mnmx5(v[0], v[1], v[2], v[3], v[4]);
		mnmx3(v[1], v[2], v[3]);

		midCol.rgb = lerp(midCol.rgb, SafeHDR(v[2].rgb), -_SharpenAmount);

		return midCol;
	}