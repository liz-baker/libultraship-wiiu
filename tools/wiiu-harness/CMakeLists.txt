# Wii U on-hardware test harness (see GitHub issue #5). Builds a loadable
# .wuhb that exercises libultraship's Wii U paths in stages, since Phases
# A-D got the port compile-clean but nothing in it has ever actually run.
#
# Only built for CafeOS; gated by LUS_BUILD_WIIU_HARNESS so games consuming
# this library don't pull in an unrelated executable target.

option(LUS_BUILD_WIIU_HARNESS "Build the Wii U on-hardware test harness (.wuhb)" ON)

if(NOT LUS_BUILD_WIIU_HARNESS)
    return()
endif()

add_executable(lus-harness
    src/main.cpp
)

set_property(TARGET lus-harness PROPERTY CXX_STANDARD 20)

target_link_libraries(lus-harness PRIVATE
    -Wl,--whole-archive
    libultraship
    -Wl,--no-whole-archive
)

# No explicit -lwhb: current wut builds libwhb's sources directly into
# libwut.a rather than packaging it as its own archive, and -lwut is added
# automatically by the toolchain's wut.specs file.

# --whole-archive forces the linker to pull in and resolve every object file
# in libultraship.a, not just the ones lus-harness itself references. That's
# the point of Stage 0: a static archive alone doesn't prove its Wii U code
# paths link cleanly, only that they compiled.

target_include_directories(lus-harness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# devkitPro's upstream wut/share/wut.cmake is now just a deprecation notice
# pointing at devkitPro/cmake, so wut_create_rpx()/wut_create_wuhb() may or
# may not exist depending on what's installed. Prefer them if present; fall
# back to invoking elf2rpl/wuhbtool from $DEVKITPRO/tools/bin directly.

if(COMMAND wut_create_rpx)
    wut_create_rpx(lus-harness)
    set(LUS_HARNESS_RPX "$<TARGET_FILE_DIR:lus-harness>/lus-harness.rpx")
else()
    find_program(LUS_ELF2RPL_EXECUTABLE elf2rpl
        PATHS "$ENV{DEVKITPRO}/tools/bin"
    )
    if(NOT LUS_ELF2RPL_EXECUTABLE)
        message(FATAL_ERROR "elf2rpl not found (looked in $ENV{DEVKITPRO}/tools/bin); "
            "install the 'wut-tools' devkitPro package")
    endif()

    set(LUS_HARNESS_RPX "${CMAKE_CURRENT_BINARY_DIR}/lus-harness.rpx")
    add_custom_command(TARGET lus-harness POST_BUILD
        COMMAND ${LUS_ELF2RPL_EXECUTABLE} $<TARGET_FILE:lus-harness> ${LUS_HARNESS_RPX}
        BYPRODUCTS ${LUS_HARNESS_RPX}
        COMMENT "lus-harness: converting ELF to RPX"
        VERBATIM
    )
endif()

set(LUS_HARNESS_WUHB "${CMAKE_CURRENT_BINARY_DIR}/lus-harness.wuhb")

if(COMMAND wut_create_wuhb)
    wut_create_wuhb(lus-harness
        NAME "libultraship Wii U Harness"
        SHORTNAME "LUS Harness"
        AUTHOR "libultraship"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        TVSPLASH "${CMAKE_CURRENT_SOURCE_DIR}/assets/tvSplash.png"
        DRCSPLASH "${CMAKE_CURRENT_SOURCE_DIR}/assets/drcSplash.png"
    )
else()
    find_program(LUS_WUHBTOOL_EXECUTABLE wuhbtool
        PATHS "$ENV{DEVKITPRO}/tools/bin"
    )
    if(NOT LUS_WUHBTOOL_EXECUTABLE)
        message(FATAL_ERROR "wuhbtool not found (looked in $ENV{DEVKITPRO}/tools/bin); "
            "install the 'wut-tools' devkitPro package")
    endif()

    add_custom_command(TARGET lus-harness POST_BUILD
        COMMAND ${LUS_WUHBTOOL_EXECUTABLE} ${LUS_HARNESS_RPX} ${LUS_HARNESS_WUHB}
            "--name=libultraship Wii U Harness"
            "--short-name=LUS Harness"
            "--author=libultraship"
            "--icon=${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
            "--tv-image=${CMAKE_CURRENT_SOURCE_DIR}/assets/tvSplash.png"
            "--drc-image=${CMAKE_CURRENT_SOURCE_DIR}/assets/drcSplash.png"
        DEPENDS ${LUS_HARNESS_RPX}
        BYPRODUCTS ${LUS_HARNESS_WUHB}
        COMMENT "lus-harness: packaging .wuhb"
        VERBATIM
    )
endif()
