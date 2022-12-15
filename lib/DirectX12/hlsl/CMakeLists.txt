include(DXCShader)

add_dxc_shader(FullScreenRectShader FullScreenRect.hlsl EMBEDDED PROFILE vs_6_0)
add_dxc_shader(BlitToSwapchainShader BlitToSwapchain.hlsl EMBEDDED PROFILE ps_6_0)
