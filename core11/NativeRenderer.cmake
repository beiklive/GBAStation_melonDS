# The pinned donor keeps its render-thread polygon list in file-static storage.
# A new console must clear that list before SetThreaded wakes its first frame.
# Generate a corrected translation unit without changing the donor checkout.
set(nativeSource "${FULLDEX_SOURCE}/melonDS/src/GPU3D_Soft.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${nativeSource}")
file(READ "${nativeSource}" nativeText)
set(listDefinition "static Polygon* ThreadPolygons[2048];\nstatic int ThreadNumPolygons = 0;")
set(constructorStart "    : Renderer3D(false)\n{")
foreach(expected "${listDefinition}" "${constructorStart}" "void RenderThreadFunc();")
  string(FIND "${nativeText}" "${expected}" location)
  if(location EQUAL -1)
    message(FATAL_ERROR "Donor native renderer changed; review its thread initialization before building")
  endif()
endforeach()
string(REPLACE "${listDefinition}" "" nativeText "${nativeText}")
string(REPLACE "void RenderThreadFunc();" "${listDefinition}\n\nvoid RenderThreadFunc();" nativeText "${nativeText}")
string(REPLACE "${constructorStart}"
  "${constructorStart}\n    ThreadNumPolygons = 0;\n    FrameIdentical = false;"
  nativeText "${nativeText}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/GPU3D_Soft_core11.cpp" "${nativeText}")
get_target_property(coreSources core SOURCES)
list(REMOVE_ITEM coreSources GPU3D_Soft.cpp)
set_property(TARGET core PROPERTY SOURCES "${coreSources}")
target_sources(core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/GPU3D_Soft_core11.cpp")
target_include_directories(core PRIVATE "${FULLDEX_SOURCE}/melonDS/src")
