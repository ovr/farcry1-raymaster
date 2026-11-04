# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# Please do not use this file to train or fine-tune AI models (see README.md).
# The shader programs: xrender_d3d12_shader(<name> <file.hlsl> <profile> <entry> [defines...]) per
# program (see ../CMakeLists.txt). The C++ side includes Shaders/<name>.h (array g_<name>).

# The general program (D3D12Pipeline.h, eD3D12P_General): every DF_* flag of Constants.h.
xrender_d3d12_shader(General_VS General.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(General_PS General.hlsl ps_6_0 PSMain)
# The same writing the motion vectors and the upscaler's depth too, for the draws into an upscaler's scene
# (SPipelineKey::nTarget 3).
xrender_d3d12_shader(General_Velocity_VS General.hlsl vs_6_0 VSMain XR_VELOCITY)
xrender_d3d12_shader(General_Velocity_PS General.hlsl ps_6_0 PSMain XR_VELOCITY)
# The vertex shader of the full set of the sun's cascades: a caster drawn once into all the slices it
# reaches, an instance each (SPipelineKey::bCascadeInstances, Cascades.cpp).
xrender_d3d12_shader(General_CascadeInstances_VS General.hlsl vs_6_0 VSMain XR_CASCADE_INSTANCES)
# Their pixel shaders with the steps of the HDR targets (XR_HDR: DF2_SKY_GLOW, DF2_HDR_CORE), for the
# draws into D3D12_HDR_FORMAT (SPipelineKey::nColorFormat PIPELINE_COLOR_HDR); without them the others are
# the programs without HDR, instruction for instruction.
xrender_d3d12_shader(General_Hdr_PS General.hlsl ps_6_0 PSMain XR_HDR)
xrender_d3d12_shader(General_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Cascades_PS General.hlsl ps_6_0 PSMain XR_CASCADES)
xrender_d3d12_shader(General_Cascades_Velocity_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_VELOCITY)
xrender_d3d12_shader(General_Cascades_Hdr_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_HDR)
xrender_d3d12_shader(General_Cascades_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_VELOCITY XR_HDR)
# All of them writing the ambient light's part of the colour too (XR_AMBIENT, SV_Target3), for the draws while
# the ambient occlusion's target is bound (SPipelineKey::bAmbient); with the same vertex shaders.
xrender_d3d12_shader(General_Ambient_PS General.hlsl ps_6_0 PSMain XR_AMBIENT)
xrender_d3d12_shader(General_Ambient_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_Hdr_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_HDR)
xrender_d3d12_shader(General_Ambient_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_Cascades_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES)
xrender_d3d12_shader(General_Ambient_Cascades_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_Cascades_Hdr_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_HDR)
xrender_d3d12_shader(General_Ambient_Cascades_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_VELOCITY XR_HDR)
# The leaves and the grass letting the sun through (XR_FOLIAGE, DF2_FOLIAGE, SPipelineKey::bCascades 2): programs of
# their own, so that those of the cascades stay as they were.
xrender_d3d12_shader(General_Foliage_PS General.hlsl ps_6_0 PSMain XR_FOLIAGE)
xrender_d3d12_shader(General_Foliage_Velocity_PS General.hlsl ps_6_0 PSMain XR_FOLIAGE XR_VELOCITY)
xrender_d3d12_shader(General_Foliage_Hdr_PS General.hlsl ps_6_0 PSMain XR_FOLIAGE XR_HDR)
xrender_d3d12_shader(General_Foliage_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_FOLIAGE XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_Foliage_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_FOLIAGE)
xrender_d3d12_shader(General_Ambient_Foliage_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_FOLIAGE XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_Foliage_Hdr_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_FOLIAGE XR_HDR)
xrender_d3d12_shader(General_Ambient_Foliage_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_FOLIAGE XR_VELOCITY XR_HDR)
# The cascades' with the sun's visibility from the mask of its shadow by rays where the depth prepass has the
# surface (XR_RAY_SUN, d3d12_RTSunShadows, SPipelineKey::bCascades 3): programs of their own, so that those of the
# cascades stay as they were.
xrender_d3d12_shader(General_RaySun_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_RAY_SUN)
xrender_d3d12_shader(General_RaySun_Velocity_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_RAY_SUN XR_VELOCITY)
xrender_d3d12_shader(General_RaySun_Hdr_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_RAY_SUN XR_HDR)
xrender_d3d12_shader(General_RaySun_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_CASCADES XR_RAY_SUN XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_RaySun_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN)
xrender_d3d12_shader(General_Ambient_RaySun_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_RaySun_Hdr_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_HDR)
xrender_d3d12_shader(General_Ambient_RaySun_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_VELOCITY XR_HDR)
# The draws tracing rays (SPipelineKey::bRayTrace; Shader Model 6.5, inline RayQuery): those reflecting a cube map
# (DF_CUBE0) with rays in its place (XR_RAY_REFL, d3d12_RTReflections with d3d12_RTReflectionSurfaces) and those
# lit by lamps with rays towards them (XR_RAY_LIGHT, d3d12_RTLightShadows); without the cascades, with them and
# with the mask of the sun's shadow by rays (XR_RAY_SUN), the ambient light's part, HDR and velocity.
xrender_d3d12_shader(General_Rays_PS General.hlsl ps_6_5 PSMain XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Cascades_Rays_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_Cascades_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_Cascades_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_Cascades_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_RaySun_Rays_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_RaySun_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_RaySun_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_RaySun_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_Rays_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_Ambient_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_Ambient_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_Cascades_Rays_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_Ambient_Cascades_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_Cascades_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_Ambient_Cascades_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_Ambient_RaySun_Rays_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT)
xrender_d3d12_shader(General_Ambient_RaySun_Rays_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY)
xrender_d3d12_shader(General_Ambient_RaySun_Rays_Hdr_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_HDR)
xrender_d3d12_shader(General_Ambient_RaySun_Rays_Hdr_Velocity_PS General.hlsl ps_6_5 PSMain XR_AMBIENT XR_CASCADES XR_RAY_SUN XR_RAY_REFL XR_RAY_LIGHT XR_VELOCITY XR_HDR)
# The main view's opaque list into the path tracer's G-buffer (SPipelineKey::bPathTrace, XR_PT, PathGBuffer.hlsli; the
# HDR scene targets): without the sun's cascades, with them, with the mask of the sun's shadow by rays too; without and
# with velocity.
xrender_d3d12_shader(General_PT_Hdr_PS General.hlsl ps_6_0 PSMain XR_PT XR_HDR)
xrender_d3d12_shader(General_PT_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_PT XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_PT_Cascades_Hdr_PS General.hlsl ps_6_0 PSMain XR_PT XR_CASCADES XR_HDR)
xrender_d3d12_shader(General_PT_Cascades_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_PT XR_CASCADES XR_VELOCITY XR_HDR)
xrender_d3d12_shader(General_PT_RaySun_Hdr_PS General.hlsl ps_6_0 PSMain XR_PT XR_CASCADES XR_RAY_SUN XR_HDR)
xrender_d3d12_shader(General_PT_RaySun_Hdr_Velocity_PS General.hlsl ps_6_0 PSMain XR_PT XR_CASCADES XR_RAY_SUN XR_VELOCITY XR_HDR)

# Water surfaces (eD3D12P_Water).
xrender_d3d12_shader(Water_VS Water.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Water_PS Water.hlsl ps_6_0 PSMain)
# The same writing the motion vectors and the upscaler's depth, in an upscaler's scene.
xrender_d3d12_shader(Water_Velocity_VS Water.hlsl vs_6_0 VSMain XR_VELOCITY)
xrender_d3d12_shader(Water_Velocity_PS Water.hlsl ps_6_0 PSMain XR_VELOCITY)
# Into D3D12_HDR_FORMAT (XR_HDR: the reflected sky box with SkyGlow).
xrender_d3d12_shader(Water_Hdr_PS Water.hlsl ps_6_0 PSMain XR_HDR)
xrender_d3d12_shader(Water_Hdr_Velocity_PS Water.hlsl ps_6_0 PSMain XR_VELOCITY XR_HDR)
# All four with the reflection by rays (XR_RAYTRACE, d3d12_RTReflections, SPipelineKey::bRayTrace): Shader Model 6.5
# (inline RayQuery), drawn only on a device with DXR 1.1 (RayTracing.h).
xrender_d3d12_shader(Water_RT_PS Water.hlsl ps_6_5 PSMain XR_RAYTRACE)
xrender_d3d12_shader(Water_RT_Velocity_PS Water.hlsl ps_6_5 PSMain XR_RAYTRACE XR_VELOCITY)
xrender_d3d12_shader(Water_RT_Hdr_PS Water.hlsl ps_6_5 PSMain XR_RAYTRACE XR_HDR)
xrender_d3d12_shader(Water_RT_Hdr_Velocity_PS Water.hlsl ps_6_5 PSMain XR_RAYTRACE XR_VELOCITY XR_HDR)

# The screen effects (eD3D12P_Post).
xrender_d3d12_shader(Post_VS Post.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Post_PS Post.hlsl ps_6_0 PSMain)
# The upscaler's picture over the back buffer writing the highlight layer too (HdrOutput.cpp,
# SPipelineKey::nTarget 4).
xrender_d3d12_shader(Post_Highlight_PS Post.hlsl ps_6_0 PSHighlight)

# The anti-aliasing of the finished picture (eD3D12P_AntiAlias, d3d12_AA, PostAA.cpp), and its pass writing the
# highlight layer too (SPipelineKey::nTarget 4).
xrender_d3d12_shader(AntiAlias_VS AntiAlias.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(AntiAlias_PS AntiAlias.hlsl ps_6_0 PSMain)
xrender_d3d12_shader(AntiAlias_Highlight_PS AntiAlias.hlsl ps_6_0 PSHighlight)
# SMAA 1x's edges and blending weights (d3d12_AA smaa), compute kernels with SMAA.hlsl of third_party/smaa, by
# d3d12_AAQuality's SMAA preset; its blending is AntiAlias.hlsl's.
set(XRENDERD3D12_SHADER_INCLUDES "${XRENDERD3D12_SMAA_DIR}")
foreach(q Low Medium High Ultra)
    string(TOUPPER "${q}" preset)
    xrender_d3d12_shader(SmaaEdges${q}_CS AntiAliasSmaa.hlsl cs_6_0 CSEdges SMAA_PRESET_${preset})
    xrender_d3d12_shader(SmaaWeights${q}_CS AntiAliasSmaa.hlsl cs_6_0 CSWeights SMAA_PRESET_${preset})
endforeach()
# CMAA2 (d3d12_AA cmaa2): the kernels of CMAA2.hlsl of third_party/cmaa2 with its entry points, the edges' by
# d3d12_AAQuality's preset (the other three do not depend on it); the picture back into the frame is AntiAlias.hlsl's.
set(XRENDERD3D12_SHADER_INCLUDES "${XRENDERD3D12_CMAA2_DIR}")
set(preset 0)
foreach(q Low Medium High Ultra)
    xrender_d3d12_shader(Cmaa2Edges${q}_CS AntiAliasCmaa2.hlsl cs_6_0 EdgesColor2x2CS CMAA2_STATIC_QUALITY_PRESET=${preset})
    math(EXPR preset "${preset} + 1")
endforeach()
xrender_d3d12_shader(Cmaa2DispatchArgs_CS AntiAliasCmaa2.hlsl cs_6_0 ComputeDispatchArgsCS)
xrender_d3d12_shader(Cmaa2Candidates_CS AntiAliasCmaa2.hlsl cs_6_0 ProcessCandidatesCS)
xrender_d3d12_shader(Cmaa2Apply_CS AntiAliasCmaa2.hlsl cs_6_0 DeferredColorApply2x2CS)
set(XRENDERD3D12_SHADER_INCLUDES "")
# TAA (d3d12_AA taa, UpscalerTaa.cpp, Taa.hlsli): each pixel's motion, the frame blended into the history, the sharpening
# (AMD FidelityFX CAS of third_party/fidelityfx-cas, TaaCas.hlsli).
xrender_d3d12_shader(TaaDilate_CS Taa.hlsl cs_6_0 CSMain XR_TAA_DILATE)
xrender_d3d12_shader(TaaResolve_CS Taa.hlsl cs_6_0 CSMain XR_TAA_RESOLVE)
set(XRENDERD3D12_SHADER_INCLUDES "${XRENDERD3D12_CAS_DIR}")
xrender_d3d12_shader(TaaSharpen_CS Taa.hlsl cs_6_0 CSMain)
set(XRENDERD3D12_SHADER_INCLUDES "")
# MSAA (d3d12_AA msaa2, msaa4, msaa8, Msaa.cpp, Msaa.hlsli): the HDR light's and the depth's resolve.
xrender_d3d12_shader(MsaaResolve_VS Msaa.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(MsaaColor_PS Msaa.hlsl ps_6_0 PSColor)
xrender_d3d12_shader(MsaaDepth_PS Msaa.hlsl ps_6_0 PSDepth XR_MSAA_DEPTH)

# Exposure, bloom and tone mapping of the HDR scene (eD3D12P_Hdr, r_HDRRendering).
xrender_d3d12_shader(Hdr_VS Hdr.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Hdr_PS Hdr.hlsl ps_6_0 PSMain)
# The resolve writing the highlight layer too (HdrOutput.cpp, SPipelineKey::nTarget 4).
xrender_d3d12_shader(Hdr_Highlight_PS Hdr.hlsl ps_6_0 PSHighlight)

# The scene for rays seen from the main view's camera (eD3D12P_RayDebug, d3d12_RTDebug): Shader Model 6.5
# (inline RayQuery), drawn only on a device with DXR 1.1 (RayTracing.h).
xrender_d3d12_shader(RayDebug_VS RayDebug.hlsl vs_6_5 VSMain)
xrender_d3d12_shader(RayDebug_PS RayDebug.hlsl ps_6_5 PSMain)
# The path tracer's light into the picture (eD3D12P_PathComposite, d3d12_PathTracing): no rays.
xrender_d3d12_shader(PathComposite_VS PathComposite.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(PathComposite_PS PathComposite.hlsl ps_6_0 PSMain)

# The compute kernels (D3D12Compute.h, ED3D12Kernel): Intel XeGTAO's passes (XeGtao.hlsl, one per
# compile), the ambient occlusion's resolve, the HDR meter's first level, the bloom's levels, the
# depth tiles of the water's depth copy, the volumetric light's froxels, the HDR statistics and the automatic exposure's
# histogram (d3d12_ExposureMeter 1), the HDR output's frame average limit (d3d12_HDRFrameAverage).
xrender_d3d12_shader(Gtao_PrefilterDepths_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_PREFILTER)
xrender_d3d12_shader(Gtao_Normals_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_NORMALS)
xrender_d3d12_shader(Gtao_Low_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=1 XR_GTAO_STEPS=2)
xrender_d3d12_shader(Gtao_Medium_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=2 XR_GTAO_STEPS=2)
xrender_d3d12_shader(Gtao_High_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=3 XR_GTAO_STEPS=3)
xrender_d3d12_shader(Gtao_Ultra_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=9 XR_GTAO_STEPS=3)
xrender_d3d12_shader(Gtao_Denoise_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_DENOISE)
xrender_d3d12_shader(Gtao_DenoiseLast_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_DENOISE XR_GTAO_LAST)
xrender_d3d12_shader(AoResolve_CS AoResolve.hlsl cs_6_0 CSMain)
xrender_d3d12_shader(Meter_CS Meter.hlsl cs_6_0 CSMain)
xrender_d3d12_shader(Bloom_CS Bloom.hlsl cs_6_0 CSMain)
xrender_d3d12_shader(DepthTiles_CS DepthTiles.hlsl cs_6_0 CSMain)
xrender_d3d12_shader(DepthTilesUp_CS DepthTiles.hlsl cs_6_0 CSMain XR_TILES_UP)
# The FFT ocean (d3d12_WaterFFT, OceanFFT.cpp): the sea's spectra and their inverse FFTs, the waves and their mip levels.
xrender_d3d12_shader(OceanRows_CS OceanFFT.hlsl cs_6_0 CSMain XR_OCEAN_ROWS)
xrender_d3d12_shader(OceanColumns_CS OceanFFT.hlsl cs_6_0 CSMain XR_OCEAN_COLUMNS)
xrender_d3d12_shader(OceanWaves_CS OceanFFT.hlsl cs_6_0 CSMain XR_OCEAN_WAVES)
xrender_d3d12_shader(OceanMips_CS OceanFFT.hlsl cs_6_0 CSMain XR_OCEAN_MIPS)
xrender_d3d12_shader(VolumetricInject_CS Volumetric.hlsl cs_6_0 CSInject)
xrender_d3d12_shader(VolumetricIntegrate_CS Volumetric.hlsl cs_6_0 CSIntegrate)
xrender_d3d12_shader(HdrHistogram_CS HdrStats.hlsl cs_6_0 CSHistogram)
xrender_d3d12_shader(HdrOutputStats_CS HdrStats.hlsl cs_6_0 CSOutput)
xrender_d3d12_shader(ExposureHistogram_CS Exposure.hlsl cs_6_0 CSHistogram)
xrender_d3d12_shader(ExposureTarget_CS Exposure.hlsl cs_6_0 CSTarget)
xrender_d3d12_shader(FrameAverageSum_CS FrameAverage.hlsl cs_6_0 CSSum)
xrender_d3d12_shader(FrameAverageLimit_CS FrameAverage.hlsl cs_6_0 CSLimit)
# The sun's shadow by rays (d3d12_RTSunShadows, RaySun.cpp): Shader Model 6.5 (inline RayQuery), the compute root
# signature with rays (D3D12Compute.h), run only on a device with DXR 1.1.
xrender_d3d12_shader(RaySunTrace_CS RaySun.hlsl cs_6_5 CSTrace)
xrender_d3d12_shader(RaySunTemporal_CS RaySun.hlsl cs_6_5 CSTemporal)
xrender_d3d12_shader(RaySunFilter_CS RaySun.hlsl cs_6_5 CSFilter)
# Its denoise by AMD FidelityFX Denoiser's shadows denoiser (d3d12_RTSunDenoise 2): our kernels with its headers; only when
# its submodule is there (XRENDERD3D12_FFX_DENOISER).
if(XRENDERD3D12_FFX_DENOISER)
    set(XRENDERD3D12_SHADER_INCLUDES "${XRENDERD3D12_FFX_DENOISER_DIR}")
    xrender_d3d12_shader(RaySunFfxInputs_CS RaySunFfx.hlsl cs_6_5 CSInputs RAY_FFX_INPUTS)
    xrender_d3d12_shader(RaySunFfxPrepare_CS RaySunFfx.hlsl cs_6_5 CSPrepare RAY_FFX_PREPARE)
    xrender_d3d12_shader(RaySunFfxClassify_CS RaySunFfx.hlsl cs_6_5 CSClassify RAY_FFX_CLASSIFY)
    xrender_d3d12_shader(RaySunFfxFilter_CS RaySunFfx.hlsl cs_6_5 CSFilter RAY_FFX_FILTER)
    xrender_d3d12_shader(RaySunFfxPack_CS RaySunFfx.hlsl cs_6_5 CSPack RAY_FFX_PACK)
    set(XRENDERD3D12_SHADER_INCLUDES "")
endif()
# The ambient occlusion by rays (d3d12_AO rt, RayAO.cpp), as the sun's shadow's.
xrender_d3d12_shader(RayAOTrace_CS RayAO.hlsl cs_6_5 CSTrace)
xrender_d3d12_shader(RayAOTemporal_CS RayAO.hlsl cs_6_5 CSTemporal)
xrender_d3d12_shader(RayAOFilter_CS RayAO.hlsl cs_6_5 CSFilter)
# The ambient light by rays, a bounce (d3d12_RTGI, RayAO.cpp).
xrender_d3d12_shader(RayGITrace_CS RayAO.hlsl cs_6_5 CSTrace RAY_GI)
xrender_d3d12_shader(RayGITemporal_CS RayAO.hlsl cs_6_5 CSTemporal RAY_GI)
xrender_d3d12_shader(RayGIFilter_CS RayAO.hlsl cs_6_5 CSFilter RAY_GI)
# The path tracer (d3d12_PathTracing, PathTrace.cpp): its paths, their history, its filter, the highlights.
xrender_d3d12_shader(PathTraceTrace_CS PathTrace.hlsl cs_6_5 CSTrace)
xrender_d3d12_shader(PathTraceTemporal_CS PathTrace.hlsl cs_6_5 CSTemporal)
xrender_d3d12_shader(PathTraceFilter_CS PathTrace.hlsl cs_6_5 CSFilter)
xrender_d3d12_shader(PathTraceSpecular_CS PathTrace.hlsl cs_6_5 CSSpecular)
# Its radiance cache (d3d12_PTCache, PathCache.hlsli): its samples, their light.
xrender_d3d12_shader(PathCacheUpdate_CS PathTrace.hlsl cs_6_5 CSCacheUpdate)
xrender_d3d12_shader(PathCacheResolve_CS PathTrace.hlsl cs_6_5 CSCacheResolve)
# Its kernels around NVIDIA NRD (d3d12_PTDenoiser nrd, NrdHost.cpp): what REBLUR takes and what it gives, with NRD's
# NRD.hlsli; only when NRD is built in (XRENDERD3D12_NRD).
if(XRENDERD3D12_NRD)
    set(XRENDERD3D12_SHADER_INCLUDES "${XRENDERD3D12_NRD_DIR}/Shaders")
    xrender_d3d12_shader(PathNrdPack_CS PathNrd.hlsl cs_6_5 CSPack)
    xrender_d3d12_shader(PathNrdResolve_CS PathNrd.hlsl cs_6_5 CSResolve)
    set(XRENDERD3D12_SHADER_INCLUDES "")
endif()
