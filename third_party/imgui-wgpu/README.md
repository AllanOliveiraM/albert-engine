Dear ImGui WebGPU renderer backend (`backends/imgui_impl_wgpu.*` from Dear ImGui 1.92.6,
`6ded5230d043aa32c755e65c910c2af5002fb9f9`, MIT license, © Omar Cornut and contributors),
adapted for wgpu-native v29, which follows the standard `webgpu.h`: a local stage struct replaces
`WGPUProgrammableStageDescriptor`, vertex attributes and surface errors use the standard layout.
Changes are marked `Albert:`.
