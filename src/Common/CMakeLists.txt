include(${CMAKE_CURRENT_SOURCE_DIR}/../../common.cmake)

add_library(${PROJECT_NAME}_Common INTERFACE)

set(config
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/HybridRaytracer.json
)

copyTargetCommand("${config}" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} copied_common_config)
add_dependencies(${PROJECT_NAME}_Common copied_common_config)
