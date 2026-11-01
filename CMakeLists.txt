cmake_minimum_required(VERSION 3.20)
project(KirbyDreamCourseRecomp C CXX)
set(CMAKE_C_STANDARD 11)
option(KDC_EXPERIMENTAL_STATE "Enable experimental frame-driver save-state support" OFF)
option(KDC_EXPERIMENTAL_RASTER_STATE "Include raster journal in experimental save states" OFF)
option(KDC_EXPERIMENTAL_FIELD_STATE "Include experimental beam field state" OFF)
option(KDC_EXPERIMENTAL_DSP_STATE "Use the isolated incremental DSP state format" OFF)
option(KDC_EXPERIMENTAL_BEAM_HDMA_STATE "Include experimental beam HDMA state" OFF)
option(KDC_EXPERIMENTAL_LATE_NMI_STATE "Include experimental late NMI state" OFF)
option(KDC_EXPERIMENTAL_PPU2_BUS_STATE "Include experimental PPU2 open-bus state" OFF)
option(KDC_EXPERIMENTAL_SPC_C4_STATE "Use the experimental staged SPC MOV state" OFF)
option(KDC_EXPERIMENTAL_AUTOJOY_STATE "Use experimental automatic joypad timing and snapshot identity" OFF)
option(KDC_EXPERIMENTAL_SPC_D4_STATE "Use experimental staged indexed SPC store and snapshot identity" OFF)
option(KDC_EXPERIMENTAL_APU_BOUNDARY_STATE "Use experimental APU queue boundary synchronization and snapshot identity" OFF)
set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/local/snesrecomp" CACHE PATH "Pinned SNESRecomp checkout")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR "Pinned SNESRecomp checkout missing; see docs/framework-spike.md")
endif()
execute_process(COMMAND git -C "${SNESRECOMP_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE FRAMEWORK_REV OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE GIT_RESULT)
if(NOT GIT_RESULT EQUAL 0 OR NOT FRAMEWORK_REV STREQUAL "147b55e252dcb3e7680a99c7c2f50e7183891844")
    message(FATAL_ERROR "SNESRecomp revision does not match the tested pin")
endif()
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL "Framework desktop host requires mod support" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/ws_shadow.h" KDC_TILE_PROVIDER_API
     REGEX "WsShadowResolveTile8x8" LIMIT_COUNT 1)
if(NOT KDC_TILE_PROVIDER_API)
    message(FATAL_ERROR "Apply patches/snesrecomp-widescreen-tile-provider.patch; tools/setup.ps1 includes it")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/desktop/host_main.h" KDC_LAUNCHER_ROM_API
    REGEX "int launcher_with_rom;" LIMIT_COUNT 1)
if(NOT KDC_LAUNCHER_ROM_API)
    message(FATAL_ERROR "Apply patches/snesrecomp-launcher-with-rom.patch; tools/setup.ps1 includes it")
endif()
add_executable(KirbyDreamCourseRecomp ${SNESRECOMP_RUNNER_SOURCES}
    src/main.c src/host_contract.c src/game_rtl.c src/course_view.c src/gen_stubs.c)
snesrecomp_rom_identity(KirbyDreamCourseRecomp ${CMAKE_SOURCE_DIR}/rom_identity.txt)
set(KDC_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "Local generated game code")
set(KDC_FUNCTIONS_DIR "${CMAKE_SOURCE_DIR}/recomp" CACHE PATH "Local generated function declarations")
snesrecomp_target_generated_code(KirbyDreamCourseRecomp "${KDC_GENERATED_DIR}")
snesrecomp_target_mod_catalog(KirbyDreamCourseRecomp ${CMAKE_SOURCE_DIR}/mods/preloaded)
target_include_directories(KirbyDreamCourseRecomp PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_ROOT}/runner/src/desktop ${CMAKE_SOURCE_DIR}/src ${KDC_FUNCTIONS_DIR})
target_link_libraries(KirbyDreamCourseRecomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
target_compile_definitions(KirbyDreamCourseRecomp PRIVATE
    SNES_GAME_VERSION="dev" SNES_GAME_TITLE="Kirby Dream Course")
if(KDC_EXPERIMENTAL_STATE)
    file(STRINGS "${SNESRECOMP_ROOT}/runner/src/beam_frame_driver.h" KDC_BEAM_STATE_API
         REGEX "snes_beam_frame_driver_get_state" LIMIT_COUNT 1)
    if(NOT KDC_BEAM_STATE_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_STATE requires the beam-state experiment patch")
    endif()
    file(STRINGS "${SNESRECOMP_ROOT}/runner/src/common_rtl.h" KDC_STATE_PREFLIGHT_API
         REGEX "RtlValidateExecutionState" LIMIT_COUNT 1)
    if(NOT KDC_STATE_PREFLIGHT_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_STATE requires the state-preflight experiment patch")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_STATE=1)
endif()
if(KDC_EXPERIMENTAL_RASTER_STATE)
    if(NOT KDC_EXPERIMENTAL_STATE)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_RASTER_STATE requires KDC_EXPERIMENTAL_STATE")
    endif()
    file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/ppu.h" KDC_RASTER_STATE_API
         REGEX "ppu_rasterSnapshotCapture" LIMIT_COUNT 1)
    if(NOT KDC_RASTER_STATE_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_RASTER_STATE requires the raster-state experiment patch")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_RASTER_STATE=1)
endif()
if(KDC_EXPERIMENTAL_FIELD_STATE)
    if(NOT KDC_EXPERIMENTAL_RASTER_STATE)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_FIELD_STATE requires KDC_EXPERIMENTAL_RASTER_STATE")
    endif()
    file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/snes.c" KDC_FIELD_TIMING_API
         REGEX "EXPERIMENT-field-beam" LIMIT_COUNT 1)
    if(NOT KDC_FIELD_TIMING_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_FIELD_STATE requires the field timing experiment")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_FIELD_STATE=1)
endif()
# The incremental DSP changes the guest snapshot layout. Never build a
# patched DSP with the ordinary snapshot identity, or vice versa.
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/dsp.h" KDC_INCREMENTAL_DSP_API
     REGEX "brrRing" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_DSP_STATE)
    if(NOT KDC_EXPERIMENTAL_FIELD_STATE)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_DSP_STATE requires KDC_EXPERIMENTAL_FIELD_STATE")
    endif()
    if(NOT KDC_INCREMENTAL_DSP_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_DSP_STATE requires the incremental DSP patch")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_DSP_STATE=1)
elseif(KDC_INCREMENTAL_DSP_API)
    message(FATAL_ERROR "The incremental DSP patch requires KDC_EXPERIMENTAL_DSP_STATE")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/beam_frame_driver.h" KDC_BEAM_HDMA_STATE_API
     REGEX "snes_beam_frame_driver_get_hdma_event" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_BEAM_HDMA_STATE)
    if(NOT KDC_EXPERIMENTAL_DSP_STATE OR NOT KDC_BEAM_HDMA_STATE_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_BEAM_HDMA_STATE requires the DSP and beam HDMA experiments")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_BEAM_HDMA_STATE=1)
elseif(KDC_BEAM_HDMA_STATE_API)
    message(FATAL_ERROR "The beam HDMA patch requires KDC_EXPERIMENTAL_BEAM_HDMA_STATE")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/snes.h" KDC_LATE_NMI_API
     REGEX "lateNmiDue" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_LATE_NMI_STATE)
    if(NOT KDC_EXPERIMENTAL_BEAM_HDMA_STATE OR NOT KDC_LATE_NMI_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_LATE_NMI_STATE requires the beam HDMA and late NMI experiments")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_LATE_NMI_STATE=1)
elseif(KDC_LATE_NMI_API)
    message(FATAL_ERROR "The late NMI patch requires KDC_EXPERIMENTAL_LATE_NMI_STATE")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/ppu.h" KDC_PPU2_BUS_API
     REGEX "ppu2OpenBus" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_PPU2_BUS_STATE)
    if(NOT KDC_EXPERIMENTAL_LATE_NMI_STATE OR NOT KDC_PPU2_BUS_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_PPU2_BUS_STATE requires the late NMI and PPU2 bus experiments")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_PPU2_BUS_STATE=1)
elseif(KDC_PPU2_BUS_API)
    message(FATAL_ERROR "The PPU2 bus patch requires KDC_EXPERIMENTAL_PPU2_BUS_STATE")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/apu.c" KDC_SPC_C4_API
     REGEX "EXPERIMENT-spc-c4-phase" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_SPC_C4_STATE)
    if(NOT KDC_EXPERIMENTAL_PPU2_BUS_STATE OR NOT KDC_SPC_C4_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_SPC_C4_STATE requires PPU2 state and the SPC C4 experiment")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_SPC_C4_STATE=1)
elseif(KDC_SPC_C4_API)
    message(FATAL_ERROR "The SPC C4 patch requires KDC_EXPERIMENTAL_SPC_C4_STATE")
endif()
# The changed auto-read semantics require an isolated snapshot identity.
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/snes.c" KDC_AUTOJOY_BEAM_API
     REGEX "EXPERIMENT-autojoy-phase" LIMIT_COUNT 1)
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/joypad.c" KDC_AUTOJOY_LATCH_API
     REGEX "EXPERIMENT-autojoy-latch" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_AUTOJOY_STATE)
    if(NOT KDC_EXPERIMENTAL_PPU2_BUS_STATE OR KDC_EXPERIMENTAL_SPC_C4_STATE OR
       NOT KDC_AUTOJOY_BEAM_API OR NOT KDC_AUTOJOY_LATCH_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_AUTOJOY_STATE requires PPU2 state and both autojoy patches; SPC C4 combination is unqualified")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_AUTOJOY_STATE=1)
elseif(KDC_AUTOJOY_BEAM_API OR KDC_AUTOJOY_LATCH_API)
    message(FATAL_ERROR "The automatic joypad experiment requires KDC_EXPERIMENTAL_AUTOJOY_STATE")
endif()
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/apu.c" KDC_SPC_D4_API
     REGEX "EXPERIMENT-spc-d4-phase" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_SPC_D4_STATE)
    if(NOT KDC_EXPERIMENTAL_AUTOJOY_STATE OR KDC_EXPERIMENTAL_SPC_C4_STATE OR NOT KDC_SPC_D4_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_SPC_D4_STATE requires autojoy state and the SPC D4 patch; SPC C4 combination is unqualified")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_SPC_D4_STATE=1)
elseif(KDC_SPC_D4_API)
    message(FATAL_ERROR "The SPC D4 patch requires KDC_EXPERIMENTAL_SPC_D4_STATE")
endif()
# Port-event ordering is part of snapshot semantics even though its layout
# is unchanged. Keep this experiment isolated from ordinary guarded states.
file(STRINGS "${SNESRECOMP_ROOT}/runner/src/snes/apu.c" KDC_APU_BOUNDARY_API
     REGEX "EXPERIMENT-apu-boundary-sync" LIMIT_COUNT 1)
if(KDC_EXPERIMENTAL_APU_BOUNDARY_STATE)
    if(NOT KDC_EXPERIMENTAL_SPC_D4_STATE OR NOT KDC_APU_BOUNDARY_API)
        message(FATAL_ERROR "KDC_EXPERIMENTAL_APU_BOUNDARY_STATE requires SPC D4 state and the APU boundary patch")
    endif()
    target_compile_definitions(KirbyDreamCourseRecomp PRIVATE KDC_EXPERIMENTAL_APU_BOUNDARY_STATE=1)
elseif(KDC_APU_BOUNDARY_API)
    message(FATAL_ERROR "The APU boundary patch requires KDC_EXPERIMENTAL_APU_BOUNDARY_STATE")
endif()
snesrecomp_target_sdl(KirbyDreamCourseRecomp)
snesrecomp_target_desktop_host(KirbyDreamCourseRecomp)

# The framework owns the host; games select and pin the shared launcher UI.
include(FetchContent)
set(KDC_RECOMP_UI_REV "3f1d6f122a58823614abce14f5b86f4d165a629c")
FetchContent_Declare(kdc_recomp_ui
    GIT_REPOSITORY https://github.com/RetroPortingToolKit/recomp-ui.git
    GIT_TAG ${KDC_RECOMP_UI_REV}
    GIT_SUBMODULES ""
    # Consume its helper only; the top-level project builds standalone demos.
    SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(kdc_recomp_ui)
execute_process(COMMAND git -C "${kdc_recomp_ui_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE KDC_RECOMP_UI_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE UI_GIT_RESULT)
if(NOT UI_GIT_RESULT EQUAL 0 OR NOT KDC_RECOMP_UI_HEAD STREQUAL KDC_RECOMP_UI_REV)
    message(FATAL_ERROR "recomp-ui revision does not match the tested pin")
endif()
# Built-in settings providers do not offer external package management.
set(KDC_UI_PATCH "${CMAKE_SOURCE_DIR}/patches/recomp-ui-builtin-settings.patch")
execute_process(COMMAND git -C "${kdc_recomp_ui_SOURCE_DIR}" apply --reverse --check "${KDC_UI_PATCH}"
    RESULT_VARIABLE KDC_UI_PATCHED OUTPUT_QUIET ERROR_QUIET)
if(NOT KDC_UI_PATCHED EQUAL 0)
    execute_process(COMMAND git -C "${kdc_recomp_ui_SOURCE_DIR}" apply --check "${KDC_UI_PATCH}"
        RESULT_VARIABLE KDC_UI_PATCH_CHECK)
    if(NOT KDC_UI_PATCH_CHECK EQUAL 0)
        message(FATAL_ERROR "recomp-ui built-in settings patch does not apply")
    endif()
    execute_process(COMMAND git -C "${kdc_recomp_ui_SOURCE_DIR}" apply "${KDC_UI_PATCH}"
        RESULT_VARIABLE KDC_UI_PATCH_RESULT)
    if(NOT KDC_UI_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not apply recomp-ui built-in settings patch")
    endif()
endif()
set(RECOMP_UI_ROOT "${kdc_recomp_ui_SOURCE_DIR}" CACHE PATH "Pinned launcher UI" FORCE)
include("${RECOMP_UI_ROOT}/recomp_ui.cmake")
recomp_target_launcher_ui(KirbyDreamCourseRecomp CONSOLE snes)
target_sources(KirbyDreamCourseRecomp PRIVATE src/launcher_settings.c)

# Upstream stages chrome and fonts, but leaves notice staging to consumers.
add_custom_command(TARGET KirbyDreamCourseRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOMP_UI_ROOT}/LICENSE"
        "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOMP_UI_ROOT}/src/third_party/imgui/LICENSE.txt"
        "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui/ImGui.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOMP_UI_ROOT}/assets/common/fonts/NOTICE.md"
        "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui/fonts.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOMP_UI_ROOT}/assets/common/img/NOTICE.md"
        "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui/images.md"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses/recomp-ui"
        "$<TARGET_FILE_DIR:KirbyDreamCourseRecomp>/licenses/recomp-ui"
    VERBATIM)
