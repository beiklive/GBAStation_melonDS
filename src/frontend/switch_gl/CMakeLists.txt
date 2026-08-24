project(switch_gl)

include(${CMAKE_SOURCE_DIR}/cmake/Toolchain-cross-Switch.cmake)

if (NOT BEIKLIVE_STATION_DIR)
    set(BEIKLIVE_STATION_DIR "${CMAKE_SOURCE_DIR}/../BeikLiveStation")
endif()
set(BOREALIS_ROOT "${BEIKLIVE_STATION_DIR}/third_party/borealis")
set(BOREALIS_INCLUDE "${BOREALIS_ROOT}/library/include")
set(GLAD_SOURCE "${BOREALIS_ROOT}/library/lib/extern/glad/glad.c")
set(NANOVG_SOURCE "${BOREALIS_ROOT}/library/lib/extern/nanovg/nanovg.c")

foreach(required IN ITEMS "${BOREALIS_INCLUDE}/borealis/extern/glad/glad.h"
                         "${BOREALIS_INCLUDE}/borealis/extern/nanovg/nanovg.h"
                         "${BOREALIS_INCLUDE}/borealis/extern/nanovg/nanovg_gl.h"
                         "${GLAD_SOURCE}" "${NANOVG_SOURCE}")
    if (NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing Switch OpenGL frontend dependency: ${required}")
    endif()
endforeach()

add_executable(melonDS_gl.elf
    main.cpp
    ../switch/Platform.cpp
    ../switch/PlatformConfig.cpp
    ../switch/FaultHandler.cpp
    "${NANOVG_SOURCE}"
)
set_source_files_properties("${NANOVG_SOURCE}" PROPERTIES LANGUAGE C)
target_compile_definitions(melonDS_gl.elf PRIVATE NANOVG_GL3_IMPLEMENTATION)
target_include_directories(melonDS_gl.elf PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../switch"
    "${BOREALIS_INCLUDE}"
    "${BOREALIS_INCLUDE}/borealis/extern/nanovg"
    "${BOREALIS_ROOT}/library/lib/extern/glad"
    "${BOREALIS_ROOT}/library/lib/extern/nanovg"
)
target_link_libraries(melonDS_gl.elf PRIVATE core glfw3 EGL glapi drm_nouveau nx m curl z)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/control_gl.nacp
    COMMAND ${NACPTOOL} --create "melonDS OpenGL" "Arisotura, BeikLiveStation" "experimental" ${CMAKE_BINARY_DIR}/control_gl.nacp)
add_custom_target(melonDS_gl.nro ALL
    ${ELF2NRO} ${CMAKE_BINARY_DIR}/melonDS_gl.elf ${CMAKE_BINARY_DIR}/melonDS_gl.nro
        --icon=${CMAKE_SOURCE_DIR}/icon/melon_switch.jpg
        --nacp=${CMAKE_BINARY_DIR}/control_gl.nacp
    DEPENDS melonDS_gl.elf ${CMAKE_BINARY_DIR}/control_gl.nacp)
