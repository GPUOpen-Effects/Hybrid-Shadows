cmake_minimum_required(VERSION 3.6)

project (HybridShadows_DX12)


# Check MSVC toolset version, Visual Studio 2019 required
if(MSVC_TOOLSET_VERSION VERSION_LESS 142)
    message(FATAL_ERROR "Cannot find MSVC toolset version 142 or greater. Please make sure Visual Studio 2019 or newer installed")
endif()

# ouput exe to bin directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/bin )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

add_compile_options(/MP)

# reference libs used by both backends
add_subdirectory(libs/cauldron)
add_subdirectory(src/Common)

# application icon
set(icon_src 
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/GPUOpenChip.ico
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/resource.h
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/Cauldron_Common.rc
)

add_subdirectory(src/DX12)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/Cauldron_Common.rc PROPERTIES VS_TOOL_OVERRIDE "Resource compiler")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/GPUOpenChip.ico  PROPERTIES VS_TOOL_OVERRIDE "Image")