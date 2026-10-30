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
