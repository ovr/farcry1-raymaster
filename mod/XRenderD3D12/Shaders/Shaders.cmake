# The shader programs: xrender_d3d12_shader(<name> <file.hlsl> <profile> <entry> [defines...]) per
# program (see ../CMakeLists.txt). The C++ side includes Shaders/<name>.h (array g_<name>).

# The general program (D3D12Pipeline.h, eD3D12P_General): every DF_* flag of Constants.h.
xrender_d3d12_shader(General_VS General.hlsl vs_6_0 VSMain)
xrender_d3d12_shader(General_PS General.hlsl ps_6_0 PSMain)
