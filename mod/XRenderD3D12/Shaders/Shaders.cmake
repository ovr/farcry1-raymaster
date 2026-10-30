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

# Water surfaces (eD3D12P_Water).
xrender_d3d12_shader(Water_VS Water.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Water_PS Water.hlsl ps_6_0 PSMain)
# The same writing the motion vectors and the upscaler's depth, in an upscaler's scene.
xrender_d3d12_shader(Water_Velocity_VS Water.hlsl vs_6_0 VSMain XR_VELOCITY)
xrender_d3d12_shader(Water_Velocity_PS Water.hlsl ps_6_0 PSMain XR_VELOCITY)
# Into D3D12_HDR_FORMAT (XR_HDR: the reflected sky box with SkyGlow).
xrender_d3d12_shader(Water_Hdr_PS Water.hlsl ps_6_0 PSMain XR_HDR)
xrender_d3d12_shader(Water_Hdr_Velocity_PS Water.hlsl ps_6_0 PSMain XR_VELOCITY XR_HDR)

# The screen effects (eD3D12P_Post).
xrender_d3d12_shader(Post_VS Post.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Post_PS Post.hlsl ps_6_0 PSMain)
# The upscaler's picture over the back buffer writing the highlight layer too (HdrOutput.cpp,
# SPipelineKey::nTarget 4).
xrender_d3d12_shader(Post_Highlight_PS Post.hlsl ps_6_0 PSHighlight)

# Exposure, bloom and tone mapping of the HDR scene (eD3D12P_Hdr, r_HDRRendering).
xrender_d3d12_shader(Hdr_VS Hdr.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Hdr_PS Hdr.hlsl ps_6_0 PSMain)
# The resolve writing the highlight layer too (HdrOutput.cpp, SPipelineKey::nTarget 4).
xrender_d3d12_shader(Hdr_Highlight_PS Hdr.hlsl ps_6_0 PSHighlight)

# The compute kernels (D3D12Compute.h, ED3D12Kernel): Intel XeGTAO's passes (XeGtao.hlsl, one per
# compile) and the ambient occlusion's resolve.
xrender_d3d12_shader(Gtao_PrefilterDepths_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_PREFILTER)
xrender_d3d12_shader(Gtao_Normals_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_NORMALS)
xrender_d3d12_shader(Gtao_Low_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=1 XR_GTAO_STEPS=2)
xrender_d3d12_shader(Gtao_Medium_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=2 XR_GTAO_STEPS=2)
xrender_d3d12_shader(Gtao_High_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=3 XR_GTAO_STEPS=3)
xrender_d3d12_shader(Gtao_Ultra_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_MAIN XR_GTAO_SLICES=9 XR_GTAO_STEPS=3)
xrender_d3d12_shader(Gtao_Denoise_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_DENOISE)
xrender_d3d12_shader(Gtao_DenoiseLast_CS XeGtao.hlsl cs_6_0 CSMain XR_GTAO_DENOISE XR_GTAO_LAST)
xrender_d3d12_shader(AoResolve_CS AoResolve.hlsl cs_6_0 CSMain)
