using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace Prism.Utils {
	public enum SharpenType { Standard=0, Photographic=1, PhotographicFast=2 };
}

#if UNITY_5_4_OR_NEWER
    [ImageEffectAllowedInSceneView]
#endif
    [RequireComponent(typeof(Camera)),  ExecuteInEditMode]
    [AddComponentMenu("PRISM/Prism.Sharpen", -1)]

public class PrismSharpen : MonoBehaviour {
	[HideInInspector]
	public Camera m_Camera;

	//Just the main pass
	[HideInInspector]
	public Material m_Material;
	[HideInInspector]
	public Shader m_Shader;

	[Header("Mouse over the variables for tips")]

	[Tooltip("The strength of the sharpening effect")]
	[Range(0f,2f)]
	public float sharpenAmount = 0.65f;

	[Range(0f,1f)]
	[HideInInspector]
	public float maximumSharpness = 0.25f;

	[Range(0f,2f)]
	[Tooltip("The range of the sharpening effect")]
	[HideInInspector]
	public float sharpenRadius = 1.0f;

	[Tooltip("Standard sharpen is good for any use case, and fastest (also recommended for mobile). Photographic sharpen tends to sharpen more, at a higher cost, and doesn't work as well when applying noise. Photographic Fast is for lower quality options.")]
	public Prism.Utils.SharpenType sharpenType = Prism.Utils.SharpenType.Standard;

	void OnEnable()
	{
		m_Camera = GetComponent<Camera>();

		if(!m_Shader)
		{
			m_Shader = Shader.Find("Hidden/PrismSharpen");
			
			if(!m_Shader)
			{
				Debug.LogError("Couldn't find shader for PRISM Sharpen! You shouldn't see this error.");
			}
		}
	}

	void OnDisable()
	{
		if (m_Material) {
			DestroyImmediate (m_Material);
			m_Material = null;
		}
	}

	protected Material CreateMaterial(Shader shader)
	{
		if (!shader)
			return null;
		Material m = new Material(shader);
		m.hideFlags = HideFlags.HideAndDontSave;
		return m;
	}

	protected bool CreateMaterials()
	{
		if (m_Material == null && m_Shader != null && m_Shader.isSupported)
		{
			m_Material = CreateMaterial(m_Shader);
		} else if(m_Shader.isSupported == false){

			//Debug.LogError("(1) Prism is not supported on this platform, or you have a shader compilation error somewhere.");

			return false;
		}

		return true;
	}
	
	protected void OnRenderImage(RenderTexture source, RenderTexture destination)
	{	
		if(!CreateMaterials())
		{
			Graphics.Blit(source,destination);
			return;
		}

		m_Material.SetFloat("_SharpenAmount",sharpenAmount);
		m_Material.SetFloat("sharp_clamp",maximumSharpness);
		m_Material.SetFloat("offset_bias",sharpenRadius);

		Graphics.Blit(source,destination, m_Material, (int)sharpenType);
	}
}
