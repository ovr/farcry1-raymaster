# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# The shader programs: xrender_d3d12_shader(<name> <file.hlsl> <profile> <entry> [defines...]) per
# program (see ../CMakeLists.txt). The C++ side includes Shaders/<name>.h (array g_<name>).

# The general program (D3D12Pipeline.h, eD3D12P_General): every DF_* flag of Constants.h.
xrender_d3d12_shader(General_VS General.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(General_PS General.hlsl ps_6_0 PSMain)

# Water surfaces (eD3D12P_Water).
xrender_d3d12_shader(Water_VS Water.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Water_PS Water.hlsl ps_6_0 PSMain)

# The screen effects (eD3D12P_Post).
xrender_d3d12_shader(Post_VS Post.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(Post_PS Post.hlsl ps_6_0 PSMain)
