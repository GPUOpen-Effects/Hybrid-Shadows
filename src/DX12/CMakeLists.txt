project (${PROJECT_NAME})

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common.cmake)

add_compile_options(/MP)

set(sources
	HybridRaytracer.cpp
	HybridRaytracer.h
	Raytracer.cpp
	Raytracer.h
	ShadowRaytracer.cpp
	ShadowRaytracer.h
	ShadowDenoiser.cpp
	ShadowDenoiser.h
	Renderer.cpp
	Renderer.h
	UI.cpp
	UI.h
	BlueNoise.cpp
	BlueNoise.h
	CSMManager.cpp
	CSMManager.h
	CustomShadowResolvePass.cpp
	CustomShadowResolvePass.h
	stdafx.cpp
	stdafx.h
	dpiawarescaling.manifest)

set(common
	${CMAKE_CURRENT_SOURCE_DIR}/../Common/HybridRaytracer.json
)

set(shaders
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/RaytracingCommon.h
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/Utilities.h
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/ShadowRaytrace.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/Classify.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/ClassifyDebug.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/ResloveRaytracing.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/filter_soft_shadows_pass_d3d12.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/prepare_shadow_mask_d3d12.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/tile_classification_d3d12.hlsl
   ${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/CustomShadowResolve.hlsl
)

set(ffx_shadows_dnsr
   ${CMAKE_HOME_DIRECTORY}/libs/FidelityFX-Denoiser/ffx-shadows-dnsr/ffx_denoiser_shadows_filter.h
   ${CMAKE_HOME_DIRECTORY}/libs/FidelityFX-Denoiser/ffx-shadows-dnsr/ffx_denoiser_shadows_prepare.h
   ${CMAKE_HOME_DIRECTORY}/libs/FidelityFX-Denoiser/ffx-shadows-dnsr/ffx_denoiser_shadows_tileclassification.h
   ${CMAKE_HOME_DIRECTORY}/libs/FidelityFX-Denoiser/ffx-shadows-dnsr/ffx_denoiser_shadows_util.h
)

copyCommand("${shaders}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibDX)
copyCommand("${ffx_shadows_dnsr}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibDX/ffx_shadows_dnsr)

source_group("Sources" FILES ${sources})
source_group("Common" FILES ${common})
source_group("Shaders" FILES ${shaders})
source_group("ffx_shadows_dnsr" FILES ${ffx_shadows_dnsr})
source_group("Icon"    FILES ${icon_src}) # defined in top-level CMakeLists.txt

add_executable(${PROJECT_NAME} WIN32 ${sources} ${common} ${shaders} ${ffx_shadows_dnsr} ${icon_src})
target_link_libraries(${PROJECT_NAME} LINK_PUBLIC ${PROJECT_NAME}_Common Cauldron_DX12 ImGUI amd_ags d3dcompiler D3D12)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin" DEBUG_POSTFIX "d")
