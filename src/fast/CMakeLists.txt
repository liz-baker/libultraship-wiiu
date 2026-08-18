#=================== Top-Level ===================
file(GLOB Source_Files__TopLevel RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")

source_group("" FILES ${Source_Files__TopLevel})
target_sources(libultraship PRIVATE ${Source_Files__TopLevel})

#=================== Debug ===================

file(GLOB Source_Files__Debug RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "debug/*.cpp")

source_group("debug" FILES ${Source_Files__Debug})
target_sources(libultraship PRIVATE ${Source_Files__Debug})

#=================== Resource ===================

file(GLOB Source_Files__Resource__Types RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/type/*.h" "resource/type/*.cpp")
source_group("resource/type" FILES ${Source_Files__Resource__Types})

file(GLOB Source_Files__Resource__Factories RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/factory/*.h" "resource/factory/*.cpp")
source_group("resource/factory" FILES ${Source_Files__Resource__Factories})

target_sources(libultraship PRIVATE ${Source_Files__Resource} ${Source_Files__Resource__Types} ${Source_Files__Resource__Factories} ${Source_Files__Resource__Archive})

#=================== Graphic ===================

file(GLOB Source_Files__Graphic RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "backends/*.cpp" "backends/*.c")

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_dxgi*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_direct3d*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/dxsdk/*")
endif()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_metal*")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_opengl*")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
    # Wii U uses the GX2 renderer and its own window backend; drop the
    # desktop SDL/OpenGL backends.
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_sdl*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_opengl*")
endif()

# The GX2/Wii U renderer is still being ported to the class-based Fast3D API.
# It is only compiled under the Cafe toolchain when LUS_WIIU_GX2 is enabled so
# the rest of the Wii U port can be validated independently. See
# docs/wiiu-port-status.md.
if (NOT (CMAKE_SYSTEM_NAME STREQUAL "CafeOS" AND LUS_WIIU_GX2))
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_wiiu*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gfx_gx2*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "backends/gx2_*")
endif()

source_group("graphic" FILES ${Source_Files__Graphic})
target_sources(libultraship PRIVATE ${Source_Files__Graphic})
