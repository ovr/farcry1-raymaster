# DirectX-Headers (d3dx12 only)

The D3D12 helper headers (`d3dx12.h` and the headers it includes) of Microsoft's DirectX-Headers,
unchanged from the tag `v1.616.0` (commit `c94b9b23aaadc2034dd1cad656a5a69f1526f98a`) of
<https://github.com/microsoft/DirectX-Headers>, in the repository's folder layout:

- `include/directx/d3dx12.h`, `d3dx12_barriers.h`, `d3dx12_check_feature_support.h`, `d3dx12_core.h`,
  `d3dx12_default.h`, `d3dx12_pipeline_state_stream.h`, `d3dx12_property_format_table.h`,
  `d3dx12_render_pass.h`, `d3dx12_resource_helpers.h`, `d3dx12_root_signature.h`,
  `d3dx12_state_object.h`
- `LICENSE`: Microsoft's MIT license of all the files above

AMD FidelityFX CACAO's Direct3D 12 host (`third_party/fidelityfx-cacao`) needs them; `d3d12.h` itself
comes from the Windows SDK (10.0.26100, `D3D12_SDK_VERSION` 616, the version of this tag).

To update: replace these files with those of the tag that matches the Windows SDK, and update the tag
and the commit here.
