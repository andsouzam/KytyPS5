cmake_minimum_required(VERSION 3.19...4.3)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "Minimum policy version for bundled dependencies")
endif()

get_property(KYTY_GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT KYTY_GENERATOR_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

project(Kyty)

find_program(SCCACHE_PROGRAM sccache)
find_program(CCACHE_PROGRAM ccache)

foreach(lang IN ITEMS C CXX)
    # An explicitly empty launcher disables caching for this language.
    if(NOT DEFINED CMAKE_${lang}_COMPILER_LAUNCHER)
        if(SCCACHE_PROGRAM)
            set(CMAKE_${lang}_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
            message(STATUS "sccache enabled for ${lang}: ${SCCACHE_PROGRAM}")
        elseif(CCACHE_PROGRAM)
            set(CMAKE_${lang}_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
            message(STATUS "ccache enabled for ${lang}: ${CCACHE_PROGRAM}")
        endif()
    endif()
endforeach()

set(KYTY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(KYTY_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set(KYTY_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")

if(CMAKE_SYSTEM_NAME MATCHES ".*Linux")
	set(LINUX TRUE)
endif()

if (NOT (WIN32 OR LINUX OR APPLE))
    message(FATAL_ERROR "only Windows, Linux, and macOS builds are supported")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include("${KYTY_SOURCE_DIR}/utils.cmake")
include(CTest)

option(KYTY_ENABLE_CLANG_TIDY "Run clang-tidy checks during builds" OFF)

set(KYTY_BUILD_ORIGIN "Source" CACHE STRING "Build provenance shown in diagnostics")
set_property(CACHE KYTY_BUILD_ORIGIN PROPERTY STRINGS Source Official Fork)
if(NOT KYTY_BUILD_ORIGIN STREQUAL "Source" AND NOT KYTY_BUILD_ORIGIN STREQUAL "Official" AND
   NOT KYTY_BUILD_ORIGIN STREQUAL "Fork")
	message(FATAL_ERROR "KYTY_BUILD_ORIGIN must be Source, Official, or Fork")
endif()

set(KYTY_RELEASE_TAG "" CACHE STRING "Release tag embedded in official builds")
set(KYTY_BUILD_REPOSITORY "" CACHE STRING "GitHub repository embedded in fork builds")
set(KYTY_OFFICIAL_BUILD OFF)
set(KYTY_FORK_BUILD OFF)
if(KYTY_BUILD_ORIGIN STREQUAL "Official")
	if(KYTY_RELEASE_TAG STREQUAL "")
		message(FATAL_ERROR "Official builds require KYTY_RELEASE_TAG")
	endif()
	set(KYTY_OFFICIAL_BUILD ON)
elseif(KYTY_BUILD_ORIGIN STREQUAL "Fork")
	if(KYTY_BUILD_REPOSITORY STREQUAL "")
		message(FATAL_ERROR "Fork builds require KYTY_BUILD_REPOSITORY")
	endif()
	set(KYTY_FORK_BUILD ON)
endif()

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(TestBigEndian)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "only 64-bit builds are supported")
endif()

test_big_endian(BIG_ENDIAN)
if(BIG_ENDIAN)
	set(KYTY_ENDIAN KYTY_ENDIAN_BIG)
else()
	set(KYTY_ENDIAN KYTY_ENDIAN_LITTLE)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(KYTY_BUILD KYTY_BUILD_DEBUG)
else()
	set(KYTY_BUILD KYTY_BUILD_RELEASE)
endif()

if(LINUX OR APPLE)
	# macOS rides the POSIX/Linux code paths until it gets a dedicated platform
	set(KYTY_PLATFORM KYTY_PLATFORM_LINUX)
else()
	set(KYTY_PLATFORM KYTY_PLATFORM_WINDOWS)
endif()

set(CLANG FALSE)
set(GCC FALSE)
set(KYTY_CLANG_CL FALSE)
get_filename_component(KYTY_CXX_COMPILER_NAME "${CMAKE_CXX_COMPILER}" NAME_WE)
string(TOLOWER "${KYTY_CXX_COMPILER_NAME}" KYTY_CXX_COMPILER_NAME)

if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
	set(CLANG TRUE)
	set(KYTY_COMPILER CLANG)
	if(WIN32 AND KYTY_CXX_COMPILER_NAME STREQUAL "clang-cl")
		set(KYTY_CLANG_CL TRUE)
		set(KYTY_LINKER LLD_LINK)
	elseif(APPLE)
		set(KYTY_LINKER LD64) # Apple's default linker; lld flags don't apply
	else()
		set(KYTY_LINKER LLD)
	endif()
	if ((CMAKE_AR MATCHES "llvm") AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0.0) AND (CMAKE_CXX_COMPILER MATCHES "winlibs"))
		string(REPLACE "llvm-ar.exe" "ar.exe" CMAKE_AR ${CMAKE_AR}) # llvm-ar.exe 11.0.0 from winlibs is broken
	endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(GCC TRUE)
	set(KYTY_COMPILER GCC)
	set(KYTY_LINKER LD)
else()
	message(FATAL_ERROR "only Clang and GCC are supported; found ${CMAKE_CXX_COMPILER_ID}")
endif()

set(KYTY_PROJECT_NAME "Emulator" CACHE STRING "Project name suffix")

string(TOLOWER ${KYTY_COMPILER} KYTY_COMPILER_ID)
string(TOLOWER ${KYTY_LINKER} KYTY_LINKER_ID)

if (CLANG AND (KYTY_LINKER STREQUAL LLD))
	set(KYTY_LD_OPTIONS "-fuse-ld=lld")
endif()

if (KYTY_LINKER STREQUAL LD AND NOT LINUX)
	set(KYTY_LD_OPTIONS "-Wl,--image-base=0x100000000000")
endif()

project(Kyty${KYTY_PROJECT_NAME}${CMAKE_BUILD_TYPE}${KYTY_COMPILER} VERSION 0.3.0)

include_directories(
	${KYTY_THIRD_PARTY_DIR}/gtest/include
	${KYTY_THIRD_PARTY_DIR}/gtest
	${KYTY_THIRD_PARTY_DIR}/fmt/include
	${KYTY_THIRD_PARTY_DIR}/magic_enum/include/magic_enum
	${PROJECT_BINARY_DIR}
	${KYTY_SOURCE_DIR}
)

set(KYTY_VERSION "${PROJECT_VERSION}")

configure_file(
  ${KYTY_SOURCE_DIR}/cmake_config.h.in
  ${PROJECT_BINARY_DIR}/cmake_config.h
  )

find_package(Git)

# See generate_version.cmake: only for builds that leave the generated SPIR-V unchanged.
set(KYTY_SHADER_CACHE_KEY "aaeb0f01f465c27db0fbd0ab9b1a10b14748c69242cb1cce102d130a28aeba4b" CACHE STRING
	"Shader cache key used instead of the hash of the recompiler and pipeline sources" FORCE)
if(KYTY_SHADER_CACHE_KEY)
	message(STATUS "Shader cache key: pinned ${KYTY_SHADER_CACHE_KEY}")
else()
	message(STATUS "Shader cache key: computed from the recompiler and pipeline sources")
endif()


add_custom_target(KytyGitVersion
	COMMAND ${CMAKE_COMMAND}
		-D INPUT_FILE=${KYTY_SOURCE_DIR}/kytyGitVersion.h.in
		-D OUTPUT_FILE=${PROJECT_BINARY_DIR}/kytyGitVersion.h
		-D GIT_EXECUTABLE=${GIT_EXECUTABLE}
		-D GIT_WORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
		-D SOURCE_DIRECTORY=${KYTY_SOURCE_DIR}
		-D KYTY_SHADER_CACHE_KEY_PINNED=${KYTY_SHADER_CACHE_KEY}
		-P ${KYTY_SOURCE_DIR}/generate_version.cmake
	BYPRODUCTS ${PROJECT_BINARY_DIR}/kytyGitVersion.h
	COMMENT "Generate kytyGitVersion.h"
)

add_library(kyty_git_version INTERFACE)
add_dependencies(kyty_git_version KytyGitVersion)

if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0.0)
	list(APPEND KYTY_IWYU
		kyty_emulator
		common
		#launcher
	)
	list(APPEND KYTY_CLANG_TIDY
		kyty_emulator
		#common
		launcher
	)
endif()

option(KYTY_BUILD_LAUNCHER "Build Qt launcher" ON)

config_compiler_and_linker()

add_subdirectory("${KYTY_THIRD_PARTY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/3rdparty")
# A build given to other people: no switch names, no fatal error texts, no class names.
option(KYTY_SHARE_BUILD "Hide switch names, error texts and class names (build for sharing)" OFF)
# NVIDIA Nsight Aftermath GPU crash dumps (debug builds; KYTY_AFTERMATH=1 at run time, see
# src/graphics/host_gpu/aftermath.h). The SDK ships with Nsight Graphics and is not part of this
# repository: set KYTY_AFTERMATH_SDK to its root.
option(KYTY_AFTERMATH "Link the Nsight Aftermath SDK for GPU crash dumps" OFF)
set(KYTY_AFTERMATH_SDK "" CACHE PATH "Nsight Aftermath SDK root (include/, lib/x64/)")
if(KYTY_AFTERMATH AND NOT KYTY_AFTERMATH_SDK)
	foreach(candidate
		"C:/Program Files/NVIDIA Corporation/Nsight Graphics 2026.3.1/SDKs/NsightAftermathSDK/2026.3.26197.2318")
		if(EXISTS "${candidate}/include/GFSDK_Aftermath.h")
			set(KYTY_AFTERMATH_SDK "${candidate}")
			break()
		endif()
	endforeach()
endif()
if(KYTY_AFTERMATH AND NOT EXISTS "${KYTY_AFTERMATH_SDK}/include/GFSDK_Aftermath.h")
	message(FATAL_ERROR "KYTY_AFTERMATH: Nsight Aftermath SDK not found (set KYTY_AFTERMATH_SDK)")
endif()
if(KYTY_SHARE_BUILD)
	add_compile_definitions(KYTY_SHARE_BUILD=1)
endif()

add_subdirectory("${KYTY_SOURCE_DIR}/common" "${CMAKE_CURRENT_BINARY_DIR}/common")

file(GLOB kyty_emulator_src CONFIGURE_DEPENDS
	"${KYTY_SOURCE_DIR}/libs/*.cpp"
	"${KYTY_SOURCE_DIR}/libs/*.h"
	"${KYTY_SOURCE_DIR}/graphics/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/*.h"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/*.h"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/command_processor/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/command_processor/*.h"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/*.h"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/*.h"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/cache/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/cache/*.h"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/image/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/image/*.h"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/cfg/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/cfg/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/decode/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/decode/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/translate/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/translate/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/backend/spirv/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/backend/spirv/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/opcodes/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/opcodes/*.h"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/opcodes/*.inc"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/*.h"
	"${KYTY_SOURCE_DIR}/graphics/presentation/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/presentation/*.h"
	"${KYTY_SOURCE_DIR}/graphics/presentation/window/*.cpp"
	"${KYTY_SOURCE_DIR}/graphics/presentation/window/*.h"
	"${KYTY_SOURCE_DIR}/kernel/*.cpp"
	"${KYTY_SOURCE_DIR}/kernel/*.h"
	"${KYTY_SOURCE_DIR}/loader/*.cpp"
	"${KYTY_SOURCE_DIR}/loader/*.h"
)

find_program(KYTY_GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
set(gpu_tiler_shader_dir "${KYTY_SOURCE_DIR}/graphics/host_gpu/shaders")
set(gpu_tiler_generated_dir "${PROJECT_BINARY_DIR}/gpu_tiler_shaders")
set(gpu_tiler_shader_names
	standard256
	standard4
	standard4_3d
	standard64
	standard64_3d
	prt
	prt_3d
	render_target
	render_target_upload16
	render_target_upload32
	render_target_upload64
	depth
	promote_d16
	demote_d16
	color_transform
)
file(GLOB gpu_tiler_shader_includes CONFIGURE_DEPENDS "${gpu_tiler_shader_dir}/gpu_tiler_*.inc")
foreach(shader_name IN LISTS gpu_tiler_shader_names)
	set(shader_source "${gpu_tiler_shader_dir}/gpu_tiler_${shader_name}.comp")
	set(shader_spv "${gpu_tiler_generated_dir}/gpu_tiler_${shader_name}.spv")
	set(shader_header "${gpu_tiler_generated_dir}/gpu_tiler_${shader_name}_spv.h")
	string(TOUPPER "GPU_TILER_${shader_name}_SPV" shader_symbol)
	add_custom_command(
		OUTPUT "${shader_header}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_tiler_generated_dir}"
		COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
		        "-I${gpu_tiler_shader_dir}" -o "${shader_spv}" "${shader_source}"
		COMMAND ${CMAKE_COMMAND} -DINPUT=${shader_spv} -DOUTPUT=${shader_header}
		        -DSYMBOL=${shader_symbol} -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
		DEPENDS "${shader_source}" ${gpu_tiler_shader_includes}
		VERBATIM
	)
	list(APPEND gpu_tiler_shader_headers "${shader_header}")
endforeach()

set(fault_buffer_shader_source "${gpu_tiler_shader_dir}/fault_buffer_process.comp")
set(fault_buffer_shader_spv "${gpu_tiler_generated_dir}/fault_buffer_process.spv")
set(fault_buffer_shader_header "${gpu_tiler_generated_dir}/fault_buffer_process_spv.h")
add_custom_command(
	OUTPUT "${fault_buffer_shader_header}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_tiler_generated_dir}"
	COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
	        -DCACHING_PAGEBITS=14 -DMAX_PAGE_FAULTS=1024
	        -o "${fault_buffer_shader_spv}" "${fault_buffer_shader_source}"
	COMMAND ${CMAKE_COMMAND} -DINPUT=${fault_buffer_shader_spv} -DOUTPUT=${fault_buffer_shader_header}
	        -DSYMBOL=FAULT_BUFFER_PROCESS_SPV -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
	DEPENDS "${fault_buffer_shader_source}"
	VERBATIM
)
list(APPEND kyty_emulator_src "${fault_buffer_shader_header}")
list(APPEND kyty_emulator_src ${gpu_tiler_shader_headers})

set(dcc_clear_shader_source "${gpu_tiler_shader_dir}/dcc_clear_check.comp")
set(dcc_clear_shader_spv "${gpu_tiler_generated_dir}/dcc_clear_check.spv")
set(dcc_clear_shader_header "${gpu_tiler_generated_dir}/dcc_clear_check_spv.h")
add_custom_command(
	OUTPUT "${dcc_clear_shader_header}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_tiler_generated_dir}"
	COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
	        -o "${dcc_clear_shader_spv}" "${dcc_clear_shader_source}"
	COMMAND ${CMAKE_COMMAND} -DINPUT=${dcc_clear_shader_spv} -DOUTPUT=${dcc_clear_shader_header}
	        -DSYMBOL=DCC_CLEAR_CHECK_SPV -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
	DEPENDS "${dcc_clear_shader_source}"
	VERBATIM
)
list(APPEND kyty_emulator_src "${dcc_clear_shader_header}")

set(htile_synth_shader_source "${gpu_tiler_shader_dir}/htile_synth.comp")
set(htile_synth_shader_spv "${gpu_tiler_generated_dir}/htile_synth.spv")
set(htile_synth_shader_header "${gpu_tiler_generated_dir}/htile_synth_spv.h")
add_custom_command(
	OUTPUT "${htile_synth_shader_header}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_tiler_generated_dir}"
	COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
	        -o "${htile_synth_shader_spv}" "${htile_synth_shader_source}"
	COMMAND ${CMAKE_COMMAND} -DINPUT=${htile_synth_shader_spv} -DOUTPUT=${htile_synth_shader_header}
	        -DSYMBOL=HTILE_SYNTH_SPV -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
	DEPENDS "${htile_synth_shader_source}"
	VERBATIM
)
list(APPEND kyty_emulator_src "${htile_synth_shader_header}")

set(mesh_indirect_shader_source "${gpu_tiler_shader_dir}/mesh_indirect_args.comp")
set(mesh_indirect_shader_spv "${gpu_tiler_generated_dir}/mesh_indirect_args.spv")
set(mesh_indirect_shader_header "${gpu_tiler_generated_dir}/mesh_indirect_args_spv.h")
add_custom_command(
	OUTPUT "${mesh_indirect_shader_header}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_tiler_generated_dir}"
	COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
	        -o "${mesh_indirect_shader_spv}" "${mesh_indirect_shader_source}"
	COMMAND ${CMAKE_COMMAND} -DINPUT=${mesh_indirect_shader_spv} -DOUTPUT=${mesh_indirect_shader_header}
	        -DSYMBOL=MESH_INDIRECT_ARGS_SPV -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
	DEPENDS "${mesh_indirect_shader_source}"
	VERBATIM
)
list(APPEND kyty_emulator_src "${mesh_indirect_shader_header}")

set(gpu_blit_generated_dir "${PROJECT_BINARY_DIR}/gpu_blit_shaders")
set(gpu_blit_shader_sources
	"${gpu_tiler_shader_dir}/gpu_blit_fs_triangle.vert"
	"${gpu_tiler_shader_dir}/gpu_blit_color_to_ms_depth.frag"
	"${gpu_tiler_shader_dir}/gpu_video_out_overlay.frag"
	"${gpu_tiler_shader_dir}/gpu_video_out_downscale.frag"
	"${gpu_tiler_shader_dir}/gpu_video_out_fxaa.frag"
)
foreach(shader_source IN LISTS gpu_blit_shader_sources)
	get_filename_component(shader_name "${shader_source}" NAME_WE)
	set(shader_spv "${gpu_blit_generated_dir}/${shader_name}.spv")
	set(shader_header "${gpu_blit_generated_dir}/${shader_name}_spv.h")
	string(TOUPPER "${shader_name}_SPV" shader_symbol)
	add_custom_command(
		OUTPUT "${shader_header}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_blit_generated_dir}"
		COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
		        "-I${gpu_tiler_shader_dir}" -o "${shader_spv}" "${shader_source}"
		COMMAND ${CMAKE_COMMAND} -DINPUT=${shader_spv} -DOUTPUT=${shader_header}
		        -DSYMBOL=${shader_symbol} -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
		DEPENDS "${shader_source}"
		VERBATIM
	)
	list(APPEND gpu_blit_shader_headers "${shader_header}")
endforeach()
list(APPEND kyty_emulator_src ${gpu_blit_shader_headers})

list(APPEND kyty_emulator_src
	"${KYTY_SOURCE_DIR}/emulator.h"
	"${KYTY_SOURCE_DIR}/emulator.cpp"
	"${KYTY_SOURCE_DIR}/diagnostics/dlssHostMemoryProbe.cpp"
)
list(REMOVE_DUPLICATES kyty_emulator_src)

if (KYTY_CLANG_CL)
	set_source_files_properties(${kyty_emulator_src} PROPERTIES COMPILE_FLAGS "-Wno-pragma-pack -Wno-deprecated-declarations -D_TIMESPEC_DEFINED")
else()
	set_source_files_properties(
		"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/computePipelineQueue.cpp"
		"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/dispatchReplay.cpp"
		"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp"
		"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/temporalDlss.cpp"
		"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/vertexExplode.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ShaderRecompiler.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/cfg/ShaderCFG.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp"
		"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp"
		PROPERTIES COMPILE_FLAGS "-fexceptions"
	)
endif()

include("${KYTY_SOURCE_DIR}/dlss.cmake")
set(kyty_emulator_link_libraries common Vulkan::Headers spirv-tools-opt spirv-tools SDL3::SDL3-static imgui xxhash FFmpeg::ffmpeg fmt::fmt nlohmann_json::nlohmann_json LibAtrac9 kyty_git_version Zydis::Zydis xbyak::xbyak opus kyty_dlss)

# Linux system libraries.
if(LINUX)
	find_package(Threads REQUIRED)
	list(APPEND kyty_emulator_link_libraries m ${CMAKE_DL_LIBS} Threads::Threads)
endif()

set(inc_headers
	${KYTY_SOURCE_DIR}
	${KYTY_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
	${KYTY_THIRD_PARTY_DIR}/SPIRV-Tools/include
	${KYTY_THIRD_PARTY_DIR}/SPIRV-Headers/include
	${KYTY_THIRD_PARTY_DIR}/fmt/include
	${KYTY_THIRD_PARTY_DIR}/xxhash
	${KYTY_THIRD_PARTY_DIR}/cpuinfo/include
	${KYTY_THIRD_PARTY_DIR}/renderdoc
	${KYTY_THIRD_PARTY_DIR}/stb
)

if (KYTY_CLANG_CL)
	list(APPEND kyty_emulator_link_libraries winpthread)
	list(APPEND inc_headers
		${KYTY_THIRD_PARTY_DIR}/winpthread/include
	)
endif()

list(APPEND check_headers
	${KYTY_SOURCE_DIR}
)

function(add_kyty_full_emulator_test target source)
	add_executable(${target} EXCLUDE_FROM_ALL ${source} ${kyty_emulator_src})
	target_link_libraries(${target} ${kyty_emulator_link_libraries})
	target_include_directories(${target} PRIVATE ${inc_headers})
	if (WIN32)
		target_link_libraries(${target} iphlpapi)
	endif()
	if (KYTY_CLANG_CL)
		target_link_libraries(${target} onecore)
		add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${KYTY_THIRD_PARTY_DIR}/winpthread/bin/libwinpthread-1.dll" $<TARGET_FILE_DIR:${target}>/libwinpthread-1.dll)
	endif()
	# The macOS x86_64 guest address space needs its .zerofill segments anchored
	# by linker flags, or the kernel kills the binary on load (posix_spawn EIO).
	configure_macos_guest_address_space(${target})
endfunction()

function(configure_macos_guest_address_space target)
	if(APPLE AND (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR
			(NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")))
		target_sources(${target} PRIVATE "${KYTY_SOURCE_DIR}/kernel/macosGuestAddressSpace.cpp")
		target_compile_definitions(${target} PRIVATE KYTY_LINKED_GUEST_ADDRESS_SPACE=1)
		target_link_options(${target} PRIVATE
			-Wl,-ld_classic,-no_pie,-no_fixup_chains,-no_huge,-pagezero_size,0x40000,-segaddr,SYSTEM_MANAGED,0x40000,-segaddr,SYSTEM_RESERVED,0x7ffffc000,-segaddr,USER_AREA,0x7000000000,-image_base,0x700000000000)
	endif()
endfunction()

add_kyty_full_emulator_test(shader_cfg_tests "${KYTY_TESTS_DIR}/shaderCfgTests.cpp")

# Offline journal emission and bounded descriptor regression: no Vulkan device is created.
add_kyty_full_emulator_test(shader_batch_audit "${KYTY_TESTS_DIR}/ShaderBatchAudit.cpp")
target_sources(shader_batch_audit PRIVATE "${KYTY_TESTS_DIR}/TextureLayoutRegression.cpp")
target_include_directories(shader_batch_audit PRIVATE "${KYTY_TESTS_DIR}")
target_compile_definitions(shader_batch_audit PRIVATE KYTY_RESOURCE_REGRESSION=1)

# Real bindless allocator host-state regression with a minimal native Vulkan bootstrap.
add_kyty_full_emulator_test(bindless_lifecycle_tests "${KYTY_TESTS_DIR}/BindlessLifecycleTests.cpp")
target_compile_definitions(bindless_lifecycle_tests PRIVATE KYTY_BINDLESS_LIFECYCLE_TEST_ACCESS=1)
if(WIN32)
	# Explicit opt-in native GPU fixture: no game, input automation or default CTest run.
	add_kyty_full_emulator_test(dlss_presenter_gpu_tests "${KYTY_TESTS_DIR}/DlssPresenterGpuTests.cpp")
	target_compile_definitions(dlss_presenter_gpu_tests PRIVATE KYTY_DLSS_PRESENTER_GPU_TEST_ACCESS=1)
endif()
add_executable(shader_writer_history_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ShaderWriterHistoryTests.cpp")
target_include_directories(shader_writer_history_tests PRIVATE "${KYTY_SOURCE_DIR}")

# Public frontend translation regression for embedded vertex-fetch eligibility.
add_kyty_full_emulator_test(embedded_fetch_index_tests "${KYTY_TESTS_DIR}/EmbeddedFetchIndexTests.cpp")

add_executable(scalar_provenance_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ScalarProvenanceTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/hostMemory.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ReadLaneElimination.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/SrtWalker.cpp"
)
target_link_libraries(scalar_provenance_tests fmt::fmt)
target_include_directories(scalar_provenance_tests PRIVATE ${inc_headers})

add_executable(page_manager_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/PageManagerTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/pageManager.cpp"
)
target_link_libraries(page_manager_tests common)
target_include_directories(page_manager_tests PRIVATE ${inc_headers})

add_executable(archive_file_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ArchiveFileTests.cpp"
)
target_link_libraries(archive_file_tests common ZArchive::zarchive)
target_include_directories(archive_file_tests PRIVATE ${inc_headers})

add_executable(trophy_system_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/TrophySystemTests.cpp"
)
target_link_libraries(trophy_system_tests common)
target_include_directories(trophy_system_tests PRIVATE ${inc_headers})

add_executable(avplayer_file_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/AvPlayerFileTests.cpp"
	"${KYTY_SOURCE_DIR}/loader/timer.cpp"
)
target_link_libraries(avplayer_file_tests common FFmpeg::ffmpeg ZArchive::zarchive)
target_include_directories(avplayer_file_tests PRIVATE ${inc_headers})

add_executable(bit_array_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/BitArrayTests.cpp"
)
target_include_directories(bit_array_tests PRIVATE ${inc_headers})

add_executable(frame_statistics_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/FrameStatisticsTests.cpp"
)
target_include_directories(frame_statistics_tests PRIVATE ${inc_headers})

add_executable(renderdoc_capture_request_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/RenderDocCaptureRequestTests.cpp"
)
target_include_directories(renderdoc_capture_request_tests PRIVATE ${KYTY_SOURCE_DIR})
if(KYTY_CLANG_CL)
	target_compile_options(renderdoc_capture_request_tests PRIVATE /EHsc)
endif()

add_executable(dlss_contract_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/DlssContractTests.cpp")
target_link_libraries(dlss_contract_tests PRIVATE kyty_dlss)
add_executable(dlss_presentation_contract_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/DlssPresentationContractTests.cpp")
target_link_libraries(dlss_presentation_contract_tests PRIVATE kyty_dlss)
add_executable(dlss_temporal_association_tests EXCLUDE_FROM_ALL
    "${KYTY_TESTS_DIR}/DlssTemporalAssociationTests.cpp")
target_link_libraries(dlss_temporal_association_tests PRIVATE kyty_dlss)
add_executable(dlss_temporal_channels_tests EXCLUDE_FROM_ALL
    "${KYTY_TESTS_DIR}/DlssTemporalChannelsTests.cpp")
target_link_libraries(dlss_temporal_channels_tests PRIVATE kyty_dlss)
add_executable(dlss_temporal_image_role_tests EXCLUDE_FROM_ALL
    "${KYTY_TESTS_DIR}/DlssTemporalImageRoleTests.cpp")
target_include_directories(dlss_temporal_image_role_tests PRIVATE ${inc_headers})
target_link_libraries(dlss_temporal_image_role_tests PRIVATE Vulkan::Headers fmt::fmt)
if(WIN32)
	add_executable(dlss_vulkan_gpu_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/DlssVulkanGpuTests.cpp")
	target_link_libraries(dlss_vulkan_gpu_tests PRIVATE kyty_dlss nlohmann_json::nlohmann_json)
	if(KYTY_CLANG_CL)
		target_compile_options(dlss_vulkan_gpu_tests PRIVATE /EHsc)
	endif()
endif()
if(BUILD_TESTING)
	add_test(NAME dlss_temporal_contract COMMAND $<TARGET_FILE:dlss_contract_tests>)
	add_test(NAME dlss_presentation_contract COMMAND $<TARGET_FILE:dlss_presentation_contract_tests>)
    add_test(NAME dlss_temporal_association COMMAND $<TARGET_FILE:dlss_temporal_association_tests>)
    set_tests_properties(dlss_temporal_association PROPERTIES TIMEOUT 10)
    add_test(NAME dlss_temporal_channels COMMAND $<TARGET_FILE:dlss_temporal_channels_tests>)
    set_tests_properties(dlss_temporal_channels PROPERTIES TIMEOUT 10)
    add_test(NAME dlss_temporal_image_role COMMAND $<TARGET_FILE:dlss_temporal_image_role_tests>)
    set_tests_properties(dlss_temporal_image_role PROPERTIES TIMEOUT 10)
	set_tests_properties(dlss_temporal_contract PROPERTIES TIMEOUT 10)
	set_tests_properties(dlss_presentation_contract PROPERTIES TIMEOUT 10)
	if(NOT KYTY_DLSS)
		add_test(NAME dlss_disabled_no_sdk COMMAND $<TARGET_FILE:dlss_contract_tests> --expect-disabled)
		set_tests_properties(dlss_disabled_no_sdk PROPERTIES TIMEOUT 10)
	endif()
endif()

add_executable(mesh_dispatch_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/MeshDispatchTests.cpp"
)
target_include_directories(mesh_dispatch_tests PRIVATE ${inc_headers})

add_executable(shader_precompile_record_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ShaderPrecompileRecordTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/renderer/pipeline/shaderPrecompile.cpp"
)
target_link_libraries(shader_precompile_record_tests PRIVATE fmt::fmt xxhash)
target_include_directories(shader_precompile_record_tests PRIVATE ${inc_headers})

add_executable(lru_cache_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/LruCacheTests.cpp"
)
target_include_directories(lru_cache_tests PRIVATE ${inc_headers})

add_executable(ime_dialog_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ImeDialogTests.cpp"
	"${KYTY_SOURCE_DIR}/libs/imeCommon.cpp"
	"${KYTY_SOURCE_DIR}/libs/imeDialog.cpp"
)
target_include_directories(ime_dialog_tests PRIVATE ${inc_headers})

add_executable(safe_call_flags_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/SafeCallFlagsTests.cpp")
target_link_libraries(safe_call_flags_tests PRIVATE common xbyak::xbyak)
target_include_directories(safe_call_flags_tests PRIVATE ${inc_headers})

add_executable(memory_tracker_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/MemoryTrackerTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/pageManager.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/memoryTracker.cpp"
)
target_link_libraries(memory_tracker_tests fmt::fmt common)
target_include_directories(memory_tracker_tests PRIVATE ${inc_headers})

add_executable(shader_vertex_metadata_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ShaderVertexMetadataTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/hostMemory.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/shaderVertexMetadata.cpp"
)
target_include_directories(shader_vertex_metadata_tests PRIVATE ${inc_headers})

add_executable(resource_materialization_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ResourceMaterializationTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/gpu_format.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/hostMemory.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/SrtWalker.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/BindingLayout.cpp"
)
target_link_libraries(resource_materialization_tests fmt::fmt)
target_include_directories(resource_materialization_tests PRIVATE ${inc_headers})

add_executable(resource_tracking_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ResourceTrackingTests.cpp"
	"${KYTY_SOURCE_DIR}/graphics/guest_gpu/gpu_format.cpp"
	"${KYTY_SOURCE_DIR}/graphics/host_gpu/hostMemory.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/shaderPixelParameter.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/SrtWalker.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/ShaderInfoCollection.cpp"
	"${KYTY_SOURCE_DIR}/graphics/shader/recompiler/ir/passes/BindingLayout.cpp"
)
target_link_libraries(resource_tracking_tests fmt::fmt)
target_include_directories(resource_tracking_tests PRIVATE ${inc_headers})

add_executable(audio_out2_port_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/AudioOut2PortTests.cpp"
	"${KYTY_SOURCE_DIR}/libs/libAudio2.cpp"
	"${KYTY_SOURCE_DIR}/loader/timer.cpp"
)
target_link_libraries(audio_out2_port_tests common fmt::fmt)
target_include_directories(audio_out2_port_tests PRIVATE ${inc_headers})

add_executable(audio_diag_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/AudioDiagTests.cpp")
target_include_directories(audio_diag_tests PRIVATE ${inc_headers})

add_executable(audio_mix_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/AudioMixTests.cpp")
target_include_directories(audio_mix_tests PRIVATE ${inc_headers})

add_executable(audio_out_timing_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/AudioOutTimingTests.cpp")
target_link_libraries(audio_out_timing_tests common fmt::fmt SDL3::SDL3-static)
target_include_directories(audio_out_timing_tests PRIVATE ${inc_headers})

add_executable(ngs2_sampler_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/Ngs2SamplerTests.cpp"
	"${KYTY_SOURCE_DIR}/loader/timer.cpp"
)
target_link_libraries(ngs2_sampler_tests common fmt::fmt LibAtrac9)
target_include_directories(ngs2_sampler_tests PRIVATE ${inc_headers})

add_executable(pad_haptics_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/PadHapticsTests.cpp"
)
target_link_libraries(pad_haptics_tests common fmt::fmt SDL3::SDL3-static opus)
target_include_directories(pad_haptics_tests PRIVATE ${inc_headers})

add_executable(controller_settings_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ControllerSettingsTests.cpp"
)
target_link_libraries(controller_settings_tests common fmt::fmt SDL3::SDL3-static)
target_include_directories(controller_settings_tests PRIVATE ${inc_headers})

add_executable(save_data_memory_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/SaveDataMemoryTests.cpp"
)
target_link_libraries(save_data_memory_tests common fmt::fmt)
target_include_directories(save_data_memory_tests PRIVATE ${inc_headers})

add_executable(png_enc_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/PngEncTests.cpp")
target_link_libraries(png_enc_tests common fmt::fmt)
target_include_directories(png_enc_tests PRIVATE ${inc_headers})

add_executable(ces_tests EXCLUDE_FROM_ALL "${KYTY_TESTS_DIR}/CesTests.cpp")
target_link_libraries(ces_tests common fmt::fmt SDL3::SDL3-static)
target_include_directories(ces_tests PRIVATE ${inc_headers})

add_executable(save_data_dialog_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/SaveDataDialogTests.cpp"
	"${KYTY_SOURCE_DIR}/libs/dialog.cpp"
)
target_link_libraries(save_data_dialog_tests common fmt::fmt)
target_include_directories(save_data_dialog_tests PRIVATE ${inc_headers})

add_kyty_full_emulator_test(http_uri_parse_tests
	"${KYTY_TESTS_DIR}/HttpUriParseTests.cpp")

add_executable(event_queue_lifetime_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/EventQueueLifetimeTests.cpp"
	"${KYTY_SOURCE_DIR}/kernel/eventQueue.cpp"
	"${KYTY_SOURCE_DIR}/loader/timer.cpp"
)
target_link_libraries(event_queue_lifetime_tests common fmt::fmt)
target_include_directories(event_queue_lifetime_tests PRIVATE ${inc_headers})

add_kyty_full_emulator_test(kernel_file_system_tests
	"${KYTY_TESTS_DIR}/KernelFileSystemTests.cpp")
target_link_libraries(kernel_file_system_tests ZArchive::zarchive)

add_executable(sync_on_address_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/SyncOnAddressTests.cpp"
	"${KYTY_SOURCE_DIR}/kernel/syncOnAddress.cpp"
)
target_link_libraries(sync_on_address_tests common)
if(WIN32)
	target_link_libraries(sync_on_address_tests onecore)
endif()
target_include_directories(sync_on_address_tests PRIVATE ${inc_headers})

add_executable(host_thread_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/HostThreadTests.cpp"
)
target_link_libraries(host_thread_tests common)
target_include_directories(host_thread_tests PRIVATE ${inc_headers})

add_executable(bone_palette_staging_policy_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/BonePaletteStagingPolicyTests.cpp"
)
target_include_directories(bone_palette_staging_policy_tests PRIVATE ${inc_headers})

add_executable(image_page_table_tests EXCLUDE_FROM_ALL
	"${KYTY_TESTS_DIR}/ImagePageTableTests.cpp"
)
target_link_libraries(image_page_table_tests common fmt::fmt)
target_include_directories(image_page_table_tests PRIVATE ${inc_headers})

add_kyty_full_emulator_test(shader_recompiler_compute_tests "${KYTY_TESTS_DIR}/ShaderRecompilerComputeTests.cpp")

set(gpu_test_generated_dir "${PROJECT_BINARY_DIR}/gpu_test_shaders")
set(gpu_test_ms_depth_source
	"${gpu_tiler_shader_dir}/gpu_test_ms_depth.comp")
set(gpu_test_ms_depth_spv
	"${gpu_test_generated_dir}/gpu_test_ms_depth.spv")
set(gpu_test_ms_depth_header
	"${gpu_test_generated_dir}/gpu_test_ms_depth_spv.h")
add_custom_command(
	OUTPUT "${gpu_test_ms_depth_header}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${gpu_test_generated_dir}"
	COMMAND "${KYTY_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 -Os
	        -o "${gpu_test_ms_depth_spv}" "${gpu_test_ms_depth_source}"
	COMMAND ${CMAKE_COMMAND} -DINPUT=${gpu_test_ms_depth_spv}
	        -DOUTPUT=${gpu_test_ms_depth_header} -DSYMBOL=GPU_TEST_MS_DEPTH_SPV
	        -P "${KYTY_SOURCE_DIR}/embed_spirv.cmake"
	DEPENDS "${gpu_test_ms_depth_source}"
	VERBATIM
)
target_sources(shader_recompiler_compute_tests PRIVATE
	"${gpu_test_ms_depth_header}")

add_kyty_full_emulator_test(virtual_memory_allocation_tests "${KYTY_TESTS_DIR}/VirtualMemoryAllocationTests.cpp")
target_compile_definitions(virtual_memory_allocation_tests PRIVATE
	KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS=1)

# These tests use exceptions.
if(NOT KYTY_CLANG_CL)
	foreach(kyty_exception_test scalar_provenance_tests resource_tracking_tests
			virtual_memory_allocation_tests embedded_fetch_index_tests
			shader_batch_audit renderdoc_capture_request_tests)
		target_compile_options(${kyty_exception_test} PRIVATE -fexceptions)
	endforeach()
endif()

if(BUILD_TESTING)
	add_test(NAME emulator_user_name_cli
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --user-name 1234567890123456)
	add_test(NAME emulator_user_name_too_long
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --user-name 12345678901234567)
	set_tests_properties(emulator_user_name_too_long PROPERTIES WILL_FAIL TRUE)
	add_test(NAME emulator_present_mode_cli
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --present-mode Immediate)
	add_test(NAME emulator_present_mode_invalid
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --present-mode Invalid)
	set_tests_properties(emulator_present_mode_invalid PROPERTIES WILL_FAIL TRUE)
	add_test(NAME emulator_bda_sync_cli
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --bda-sync SelectiveChecked)
	add_test(NAME emulator_bda_sync_invalid
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --bda-sync Invalid)
	set_tests_properties(emulator_bda_sync_invalid PROPERTIES WILL_FAIL TRUE)
	add_test(NAME emulator_shader_precompile_cli
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --shader-precompile false)
	add_test(NAME emulator_shader_precompile_invalid
	         COMMAND $<TARGET_FILE:kyty_emulator> --help --shader-precompile Invalid)
	set_tests_properties(emulator_shader_precompile_invalid PROPERTIES WILL_FAIL TRUE)
	add_test(NAME ime_dialog COMMAND $<TARGET_FILE:ime_dialog_tests>)
	add_test(NAME shader_cfg COMMAND $<TARGET_FILE:shader_cfg_tests>)
	add_test(NAME embedded_fetch_index COMMAND $<TARGET_FILE:embedded_fetch_index_tests>)
	add_test(NAME embedded_fetch_index_neighbors
	         COMMAND $<TARGET_FILE:embedded_fetch_index_tests> neighbors)
	add_test(NAME shader_dx10_clamp_portable
	         COMMAND $<TARGET_FILE:embedded_fetch_index_tests> dx10-clamp-spirv)
	set_tests_properties(embedded_fetch_index embedded_fetch_index_neighbors
	                     shader_dx10_clamp_portable
	                     PROPERTIES TIMEOUT 30)
	add_test(NAME scalar_provenance COMMAND $<TARGET_FILE:scalar_provenance_tests>)
	add_test(NAME image_page_table COMMAND $<TARGET_FILE:image_page_table_tests>)
	add_test(NAME memory_tracker COMMAND $<TARGET_FILE:memory_tracker_tests>)
	add_test(NAME bone_palette_staging_policy COMMAND $<TARGET_FILE:bone_palette_staging_policy_tests>)
	add_test(NAME safe_call_flags COMMAND $<TARGET_FILE:safe_call_flags_tests>)
	set_tests_properties(safe_call_flags PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE)
	add_test(NAME page_manager COMMAND $<TARGET_FILE:page_manager_tests>)
	add_test(NAME archive_file COMMAND $<TARGET_FILE:archive_file_tests>)
	add_test(NAME trophy_system COMMAND $<TARGET_FILE:trophy_system_tests>)
	add_test(NAME avplayer_file COMMAND $<TARGET_FILE:avplayer_file_tests>)
	add_test(NAME bit_array COMMAND $<TARGET_FILE:bit_array_tests>)
	add_test(NAME frame_statistics COMMAND $<TARGET_FILE:frame_statistics_tests>)
	add_test(NAME renderdoc_capture_request COMMAND $<TARGET_FILE:renderdoc_capture_request_tests>)
	set_tests_properties(renderdoc_capture_request PROPERTIES TIMEOUT 10)
	add_test(NAME mesh_dispatch COMMAND $<TARGET_FILE:mesh_dispatch_tests>)
	add_test(NAME shader_precompile_record COMMAND $<TARGET_FILE:shader_precompile_record_tests>)
	add_test(NAME lru_cache COMMAND $<TARGET_FILE:lru_cache_tests>)
	add_test(NAME host_thread COMMAND $<TARGET_FILE:host_thread_tests>)
	add_test(NAME shader_vertex_metadata COMMAND $<TARGET_FILE:shader_vertex_metadata_tests>)
	add_test(NAME resource_materialization COMMAND $<TARGET_FILE:resource_materialization_tests>)
	add_test(NAME srt_read_residency COMMAND $<TARGET_FILE:resource_materialization_tests> --srt-residency-only)
	add_test(NAME materialization_diagnostic_observer
		COMMAND $<TARGET_FILE:resource_materialization_tests> --diagnostic-observer-only)
	set_tests_properties(materialization_diagnostic_observer PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	set_tests_properties(srt_read_residency PROPERTIES TIMEOUT 30)
	add_test(NAME resource_tracking COMMAND $<TARGET_FILE:resource_tracking_tests>)
	add_test(NAME event_queue_lifetime COMMAND $<TARGET_FILE:event_queue_lifetime_tests>)
	add_test(NAME kernel_file_system COMMAND $<TARGET_FILE:kernel_file_system_tests>)
	add_test(NAME sync_on_address COMMAND $<TARGET_FILE:sync_on_address_tests>)
	add_test(NAME audio_out2_port COMMAND $<TARGET_FILE:audio_out2_port_tests>)
	add_test(NAME audio_diag COMMAND $<TARGET_FILE:audio_diag_tests>)
	add_test(NAME audio_mix COMMAND $<TARGET_FILE:audio_mix_tests>)
	add_test(NAME audio_out_timing COMMAND $<TARGET_FILE:audio_out_timing_tests>)
	add_test(NAME ngs2_sampler COMMAND $<TARGET_FILE:ngs2_sampler_tests>)
	add_test(NAME pad_haptics COMMAND $<TARGET_FILE:pad_haptics_tests>)
	add_test(NAME controller_settings COMMAND $<TARGET_FILE:controller_settings_tests>)
	add_test(NAME save_data_memory COMMAND $<TARGET_FILE:save_data_memory_tests>)
	add_test(NAME png_enc COMMAND $<TARGET_FILE:png_enc_tests>)
	add_test(NAME ces COMMAND $<TARGET_FILE:ces_tests>)
	add_test(NAME save_data_dialog COMMAND $<TARGET_FILE:save_data_dialog_tests>)
	add_test(NAME http_uri_parse COMMAND $<TARGET_FILE:http_uri_parse_tests>)
	add_test(NAME shader_recompiler_compute COMMAND $<TARGET_FILE:shader_recompiler_compute_tests>)
	add_test(NAME texture_r8_snorm
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --r8-snorm-texture-only)
	add_test(NAME wave64_workgroup_grouping
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --wave64-workgroup-grouping-only)
	set_tests_properties(wave64_workgroup_grouping PROPERTIES TIMEOUT 30)
	set_tests_properties(texture_r8_snorm PROPERTIES TIMEOUT 30)
	add_test(NAME texture_bc_r64_layout
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --bc-r64-texture-layout-only)
	add_test(NAME texture_bc_r64_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --bc-r64-texture-gpu-only)
	set_tests_properties(texture_bc_r64_layout texture_bc_r64_gpu PROPERTIES TIMEOUT 30)
	add_test(NAME performance_memory
	         COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --performance-memory-only)
	add_test(NAME generic_formatted_conversion_path_cost
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --generic-formatted-conversion-path-cost)
	set_tests_properties(generic_formatted_conversion_path_cost PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
	add_test(NAME generic_formatted_dispatch_decision_cost
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --generic-formatted-dispatch-decision-cost)
	set_tests_properties(generic_formatted_dispatch_decision_cost PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
	add_test(NAME generic_channel_repetition_cost
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --generic-channel-repetition-cost)
	set_tests_properties(generic_channel_repetition_cost PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME driver_cache_native_roundtrip
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --driver-cache-native-roundtrip)
	set_tests_properties(driver_cache_native_roundtrip PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME user_game_preset_priority
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --user-game-preset-priority-only)
	add_test(NAME user_game_preset_users
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --user-game-preset-users-only)
	set_tests_properties(user_game_preset_priority user_game_preset_users PROPERTIES TIMEOUT 10)
	add_test(NAME armed_readback_window_cost
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --armed-readback-window-cost-gpu)
	set_tests_properties(armed_readback_window_cost PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME armed_readback_typed_neighbors
	         COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --armed-readback-typed-neighbors-gpu)
	set_tests_properties(armed_readback_typed_neighbors PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME ShaderRecompilerPm4CleanPacketReads
	         COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --pm4-clean-packet-reads)
	set_tests_properties(ShaderRecompilerPm4CleanPacketReads
	                     PROPERTIES TIMEOUT 45)
	add_test(NAME shader_precompile_gpu
	         COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --shader-precompile-only)
	add_test(NAME shader_recompiler_helper_quad
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --ds-append-helper-quad-only)
	add_test(NAME shader_recompiler_alignbyte
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --alignbyte-only)
	add_test(NAME graphics_pipeline_rasterization
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --polygon-mode-only)
	add_test(NAME graphics_draw_offsets
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --draw-offset-only)
	add_test(NAME virtual_memory_allocation
		COMMAND $<TARGET_FILE:virtual_memory_allocation_tests>)
	add_test(NAME guest_red_zone_patcher
		COMMAND $<TARGET_FILE:virtual_memory_allocation_tests> --red-zone-patcher-only)
	add_test(NAME command_scheduler_timeline
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --scheduler-only)
	add_test(NAME dispatch_recording_age_budget
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --dispatch-recording-age-budget-only)
	add_test(NAME recording_age_policy
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --recording-age-policy-only)
	add_test(NAME recording_age_policy_async
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --recording-age-policy-async-only)
	add_test(NAME recording_age_indirect
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --recording-age-indirect-only)
	set_tests_properties(dispatch_recording_age_budget recording_age_policy recording_age_policy_async recording_age_indirect
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE ENVIRONMENT "KYTY_RECORDING_AGE_US=1000")
	add_test(NAME recording_age_policy_disabled
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --recording-age-policy-disabled-only)
	set_tests_properties(recording_age_policy_disabled
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE ENVIRONMENT "KYTY_RECORDING_AGE_US=0")
	add_test(NAME stream_buffer_ring
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --stream-buffer-only)
	add_test(NAME command_stream_payloads
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --command-stream-only)
	add_test(NAME shader_recompiler_compute_hw_bounds
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --hardware-buffer-bounds)
	add_test(NAME gpu_command_lane
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --gpu-command-lane-only)
	add_test(NAME end_of_pipe_literal_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --eop-literal-ordering-gpu)
	add_test(NAME native_occlusion_predication_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --occlusion-native-cost-gpu)
	set_tests_properties(native_occlusion_predication_gpu PROPERTIES
		TIMEOUT 30 RUN_SERIAL TRUE ENVIRONMENT "KYTY_OCCLUSION_QUERIES=1")
	add_test(NAME fused_tiled_upload_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fused-tiled-upload-cost-gpu)
	add_test(NAME retired_image_byte_budget_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --retired-image-byte-budget-gpu)
	add_test(NAME retired_image_byte_budget_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --retired-image-byte-budget-neighbors-gpu)
	set_tests_properties(retired_image_byte_budget_gpu retired_image_byte_budget_neighbors_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME retired_image_mixed_size_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --retired-image-mixed-size-gpu)
	set_tests_properties(retired_image_mixed_size_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME fused_tiled_upload_2byte_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fused-tiled-upload-2byte-cost-gpu)
	set_tests_properties(fused_tiled_upload_2byte_cost PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME fused_tiled_upload_2byte_neighbors
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fused-tiled-upload-2byte-neighbors-gpu)
	add_test(NAME fused_tiled_upload_2byte_capability
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fused-tiled-upload-2byte-capability-gpu)
	set_tests_properties(fused_tiled_upload_2byte_neighbors fused_tiled_upload_2byte_capability
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME fused_tiled_upload_odd_offset_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fused-tiled-upload-odd-offset-gpu)
	set_tests_properties(fused_tiled_upload_cost fused_tiled_upload_odd_offset_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME wait_reg_mem_nonblocking_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-nonblocking-cost-gpu)
	add_test(NAME wait_reg_mem_queued_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-queued-neighbor-gpu)
	add_test(NAME wait_reg_mem_poll_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-poll-neighbors-gpu)
	add_test(NAME wait_reg_mem_nonblocking_async_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-nonblocking-cost-async-gpu)
	add_test(NAME foreign_readback_queued_state_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-queued-state-cost-gpu)
	add_test(NAME foreign_readback_ready_after_wait_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-after-wait-cost-gpu)
	set_tests_properties(foreign_readback_ready_after_wait_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
		ENVIRONMENT "KYTY_READBACK_READY_AFTER_WAIT=1")
	add_test(NAME foreign_readback_ready_mixed_range_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-mixed-range-neighbor-gpu)
	add_test(NAME foreign_readback_ready_unpublished_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-unpublished-neighbor-gpu)
	add_test(NAME foreign_readback_ready_new_writer_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-new-writer-neighbor-gpu)
	add_test(NAME foreign_readback_ready_dirty_image_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-dirty-image-neighbor-gpu)
	add_test(NAME foreign_readback_ready_pending_image_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-ready-pending-image-neighbor-gpu)
	set_tests_properties(foreign_readback_ready_mixed_range_gpu foreign_readback_ready_unpublished_gpu foreign_readback_ready_new_writer_gpu foreign_readback_ready_dirty_image_gpu foreign_readback_ready_pending_image_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE ENVIRONMENT "KYTY_READBACK_READY_AFTER_WAIT=1")
	add_test(NAME foreign_readback_queued_armed_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --foreign-readback-queued-armed-neighbor-gpu)
	add_test(NAME wait_reg_mem_ready_continuation_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-ready-continuation-cost-gpu)
	add_test(NAME wait_reg_mem_notification_lifetime_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --wait-reg-mem-notification-lifetime-gpu)
	add_test(NAME poll_exact_page_readback_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --poll-exact-page-readback-cost-gpu)
	add_test(NAME eager_adjacent_page_readback_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --eager-readback-adjacent-pages-cost-gpu)
	add_test(NAME gpu_read_memory_exact_pages_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --gpu-read-memory-exact-page-cost-gpu)
	add_test(NAME cpu_write_fault_window_cost_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --cpu-write-fault-window-cost-gpu)
	set_tests_properties(cpu_write_fault_window_cost_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME cpu_write_fault_image_window_guards_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --cpu-write-fault-image-window-guards-gpu)
	set_tests_properties(cpu_write_fault_image_window_guards_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME cpu_write_fault_window_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --cpu-write-fault-window-neighbors-gpu)
	set_tests_properties(cpu_write_fault_window_neighbors_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME cpu_write_fault_window_boundary_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --cpu-write-fault-window-boundary-gpu)
	set_tests_properties(cpu_write_fault_window_boundary_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME command_stream_idle_spin_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --command-stream-idle-spin-cost)
	set_tests_properties(command_stream_idle_spin_cost PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
	add_test(NAME repeated_gpu_dirty_state_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --repeated-gpu-dirty-state-cost-gpu)
	add_test(NAME repeated_gpu_dirty_state_neighbors
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --repeated-gpu-dirty-state-neighbors-gpu)
	set_tests_properties(repeated_gpu_dirty_state_cost repeated_gpu_dirty_state_neighbors
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME function_profile_cpu_smoke
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --function-profile-smoke)
	set_tests_properties(function_profile_cpu_smoke PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
	add_test(NAME gpu_read_memory_foreign_fault_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --gpu-read-memory-foreign-fault-locality-gpu)
	add_test(NAME gpu_read_memory_cross_owner_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --gpu-read-memory-cross-owner-gpu)
	set_tests_properties(gpu_read_memory_exact_pages_gpu gpu_read_memory_foreign_fault_gpu
		gpu_read_memory_cross_owner_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
		ENVIRONMENT "KYTY_GPU_READ_EXACT_PAGES=1")
	add_test(NAME gpu_read_memory_image_alias_guards_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --gpu-read-memory-image-alias-guards-gpu)
	set_tests_properties(gpu_read_memory_image_alias_guards_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
		ENVIRONMENT "KYTY_GPU_READ_EXACT_PAGES=1")
	add_test(NAME prepare_bda_upload_batch_cost_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --prepare-bda-upload-batch-cost-gpu)
	add_test(NAME prepare_bda_upload_batch_legacy_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --prepare-bda-upload-batch-cost-legacy-gpu)
	add_test(NAME prepare_bda_upload_batch_checked_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --prepare-bda-upload-batch-cost-checked-gpu)
	set_tests_properties(prepare_bda_upload_batch_cost_gpu prepare_bda_upload_batch_legacy_gpu
		prepare_bda_upload_batch_checked_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
		ENVIRONMENT "KYTY_BDA_UPLOAD_BATCH=1")
	add_test(NAME prepare_bda_upload_async_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --prepare-bda-upload-async-neighbors-gpu)
	add_test(NAME bda_upload_overlap_consumer_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bda-upload-overlap-consumer-gpu)
	set_tests_properties(prepare_bda_upload_async_neighbors_gpu bda_upload_overlap_consumer_neighbors_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE ENVIRONMENT "KYTY_BDA_UPLOAD_BATCH=1")
	add_test(NAME bda_upload_untouched_compute_neighbor_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --prepare-bda-upload-untouched-compute-gpu)
	set_tests_properties(bda_upload_untouched_compute_neighbor_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
		ENVIRONMENT "KYTY_BDA_UPLOAD_BATCH=1")
	add_test(NAME empty_pm4_tail_submit_cost_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --empty-pm4-tail-submit-cost-gpu)
	add_test(NAME empty_pm4_tail_required_work_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --empty-pm4-tail-required-work-neighbors-gpu)
	add_test(NAME empty_pm4_tail_diagnostic_work_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --empty-pm4-tail-diagnostic-work-gpu)
	set_tests_properties(empty_pm4_tail_submit_cost_gpu empty_pm4_tail_required_work_gpu
		empty_pm4_tail_diagnostic_work_gpu PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME image_covered_read_scope
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-covered-read-planner-cost-only)
	add_test(NAME image_new_read_stage
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-new-read-stage-planner-only)
	add_test(NAME image_partial_read_scopes
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-read-scope-partial-neighbors-only)
	add_test(NAME image_uploaded_read_stage_subset
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-read-stage-subset-cost-only)
	add_test(NAME compute_chain_visibility_off
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --compute-chain-visibility-only)
	add_test(NAME compute_chain_visibility_on
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --compute-chain-visibility-only)
	set_tests_properties(compute_chain_visibility_off PROPERTIES ENVIRONMENT "KYTY_COMPUTE_CHAIN=0")
	set_tests_properties(compute_chain_visibility_on PROPERTIES ENVIRONMENT "KYTY_COMPUTE_CHAIN=1")
	set_tests_properties(image_covered_read_scope image_new_read_stage image_partial_read_scopes
		image_uploaded_read_stage_subset compute_chain_visibility_off compute_chain_visibility_on
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	set_tests_properties(wait_reg_mem_nonblocking_gpu wait_reg_mem_queued_gpu
		wait_reg_mem_poll_neighbors_gpu wait_reg_mem_nonblocking_async_gpu
		foreign_readback_queued_state_gpu foreign_readback_queued_armed_gpu
		wait_reg_mem_ready_continuation_gpu wait_reg_mem_notification_lifetime_gpu
		poll_exact_page_readback_gpu eager_adjacent_page_readback_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME packed_buffer_offset_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --packed-buffer-offset-cost)
	add_test(NAME ngg_vertex_count_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --ngg-vertex-count-gpu)
	set_tests_properties(ngg_vertex_count_gpu PROPERTIES TIMEOUT 30)
	add_test(NAME ngg_vertex_entry_metadata
		COMMAND $<TARGET_FILE:shader_cfg_tests> --ngg-vertex-entry-only)
	set_tests_properties(ngg_vertex_entry_metadata PROPERTIES TIMEOUT 30)
	add_test(NAME packed_buffer_offset_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --packed-buffer-offset-gpu)
	set_tests_properties(packed_buffer_offset_cost packed_buffer_offset_gpu PROPERTIES TIMEOUT 30)
	add_test(NAME end_of_pipe_literal_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --eop-literal-neighbors-gpu)
	add_test(NAME end_of_pipe_literal_publication_cost
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --eop-literal-publication-cost)
	add_test(NAME formatted_buffer_static_numeric
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --formatted-buffer-static-numeric-measurement)
	set_tests_properties(formatted_buffer_static_numeric PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
	set_tests_properties(end_of_pipe_literal_publication_cost PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME end_of_pipe_literal_publication_span_neighbors
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --eop-literal-publication-span-neighbors)
	set_tests_properties(end_of_pipe_literal_publication_span_neighbors PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	if(WIN32)
		add_test(NAME native_thread_names_without_profiler
			COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --native-thread-names-only)
		set_tests_properties(native_thread_names_without_profiler PROPERTIES TIMEOUT 10)
	endif()
	add_test(NAME shader_writer_history COMMAND $<TARGET_FILE:shader_writer_history_tests>)
	add_test(NAME dispatch_snapshot_selector
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-selector-only)
	add_test(NAME dispatch_snapshot_job_selector
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-selector-only)
	add_test(NAME dispatch_snapshot_job_inventory
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-inventory-only)
	add_test(NAME dispatch_snapshot_job_inventory_frame
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-inventory-frame-only)
	add_test(NAME dispatch_snapshot_job_inventory_default
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-inventory-default-only)
	add_test(NAME dispatch_snapshot_job_inventory_cap
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-inventory-cap-only)
	set_tests_properties(dispatch_snapshot_job_inventory dispatch_snapshot_job_inventory_frame
		dispatch_snapshot_job_inventory_default dispatch_snapshot_job_inventory_cap PROPERTIES TIMEOUT 10)
	add_test(NAME dispatch_snapshot_job_selector_frame
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-selector-frame-only)
	add_test(NAME dispatch_snapshot_job_selector_default
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-job-selector-default-only)
	set_tests_properties(dispatch_snapshot_job_selector dispatch_snapshot_job_selector_frame
		dispatch_snapshot_job_selector_default PROPERTIES TIMEOUT 10)
	add_test(NAME dispatch_snapshot_selector_frame
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-selector-frame-only)
	add_test(NAME dispatch_snapshot_selector_default
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dispatch-snapshot-selector-default-only)
	set_tests_properties(dispatch_snapshot_selector dispatch_snapshot_selector_frame
		dispatch_snapshot_selector_default PROPERTIES TIMEOUT 10)
	add_test(NAME fault_buffer_protocol_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-buffer-protocol-gpu)
	add_test(NAME fault_buffer_protocol_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-buffer-protocol-neighbors-gpu)
	add_test(NAME fault_bitmap_initialization_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-bitmap-initialization-gpu)
	add_test(NAME fault_bitmap_initialization_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-bitmap-initialization-neighbors-gpu)
	add_test(NAME fault_first_binding_deferred_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-first-binding-deferred-gpu)
	add_test(NAME late_bda_image_fringe_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --late-bda-image-fringe-gpu)
	add_test(NAME first_use_bda_pointer_chain_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --first-use-bda-pointer-chain-gpu)
	add_test(NAME first_use_bda_pointer_chain_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --first-use-bda-pointer-chain-neighbors-gpu)
	add_test(NAME first_use_dma_base_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --first-use-dma-base-gpu)
	add_test(NAME first_use_dma_base_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --first-use-dma-base-neighbors-gpu)
	add_test(NAME dma_base_origins
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dma-base-origins-only)
	add_test(NAME dma_base_runtime_bounds
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --dma-base-runtime-bounds)
	add_test(NAME bc_alias_ownership_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bc-alias-ownership-gpu)
	add_test(NAME bc_alias_ownership_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bc-alias-ownership-neighbors-gpu)
	add_test(NAME bc_alias_tail_padding_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bc-alias-tail-padding-gpu)
	add_test(NAME copy_mip_destination_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --copy-mip-destination-gpu)
	add_test(NAME copy_mip_destination_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --copy-mip-destination-neighbors-gpu)
	add_test(NAME descriptor_phi_materialization
		COMMAND $<TARGET_FILE:resource_tracking_tests> --descriptor-phi-only)
	add_test(NAME descriptor_phi_neighbors
		COMMAND $<TARGET_FILE:resource_tracking_tests> --descriptor-phi-neighbors-only)
	add_test(NAME scalar_branch_descriptor_writes_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --scalar-branch-descriptor-writes-only)
	set_tests_properties(first_use_dma_base_gpu first_use_dma_base_neighbors_gpu
		dma_base_origins dma_base_runtime_bounds bc_alias_ownership_gpu
		bc_alias_ownership_neighbors_gpu bc_alias_tail_padding_gpu copy_mip_destination_gpu
		copy_mip_destination_neighbors_gpu descriptor_phi_materialization
		descriptor_phi_neighbors scalar_branch_descriptor_writes_gpu
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	set_tests_properties(first_use_bda_pointer_chain_gpu first_use_bda_pointer_chain_neighbors_gpu
		PROPERTIES TIMEOUT 30)
	add_test(NAME fault_first_binding_state_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --fault-first-binding-state-neighbors-gpu)
	set_tests_properties(end_of_pipe_literal_gpu end_of_pipe_literal_neighbors_gpu
		fault_buffer_protocol_gpu fault_buffer_protocol_neighbors_gpu
		fault_bitmap_initialization_gpu fault_bitmap_initialization_neighbors_gpu
		fault_first_binding_deferred_gpu fault_first_binding_state_neighbors_gpu late_bda_image_fringe_gpu
		PROPERTIES TIMEOUT 30)
	set_tests_properties(shader_writer_history PROPERTIES TIMEOUT 10)
	add_test(NAME pm4_context_state
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --context-state-only)
	add_test(NAME gpu_tiler
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --gpu-tiler-only)
	add_test(NAME wave64_strided_job_loop_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --wave64-strided-job-loop-only)
	set_tests_properties(wave64_strided_job_loop_gpu PROPERTIES TIMEOUT 30)

	add_test(NAME texture_cache_layered_image
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --layered-image-only)
	add_test(NAME host_image_allocation
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --host-image-allocation-only)
	add_test(NAME present_downscale
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --present-downscale-only)
	add_test(NAME texture_cache_image_views
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-view-cache-only)
	add_test(NAME texture_cache_storage_sampled
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --storage-sampled-only)
	add_test(NAME texture_cache_depth_readback
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --depth-readback-only)
	add_test(NAME compute_meta_clear_classification
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --compute-meta-clear-only)
	add_test(NAME buffer_cache_dirty_gc
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --buffer-cache-gc-only)
	add_test(NAME color_render_target_1d
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --rt1d-only)

	add_test(NAME texture_cache_image_overlap
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --image-overlap-only)
	add_test(NAME texture_cache_htile_clear
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --htile-clear-only)
	add_test(NAME bc_partial_buffer_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bc-partial-buffer-gpu)
	add_test(NAME bc_partial_buffer_neighbors_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --bc-partial-buffer-neighbors-gpu)
	add_test(NAME bc_partial_buffer_statistics_gpu
		COMMAND $<TARGET_FILE:bindless_lifecycle_tests> --drain-stats --bc-partial-buffer-neighbors-gpu)
	add_test(NAME texture_cache_htile_high_state
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --htile-high-state-only)
	add_test(NAME texture_cache_htile_high_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --htile-high-gpu-only)
	add_test(NAME texture_cache_htile_registration_neighbors
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --htile-registration-neighbors)
	add_test(NAME texture_cache_dcc_statistics_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --dcc-clear-stats-only)
	add_test(NAME gpu_counted_multi_draw_srd
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --gpu-counted-multi-draw-srd)
	add_test(NAME alpha_coverage_endpoints_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --alpha-coverage-endpoints)
	add_test(NAME alpha_coverage_early_neighbors_gpu
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --alpha-coverage-neighbors)
	add_test(NAME alpha_coverage_predicate_neighbors
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --alpha-coverage-predicate-neighbors)
	set_tests_properties(alpha_coverage_endpoints_gpu alpha_coverage_early_neighbors_gpu
		alpha_coverage_predicate_neighbors PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	set_tests_properties(bc_partial_buffer_gpu bc_partial_buffer_neighbors_gpu
		bc_partial_buffer_statistics_gpu texture_cache_htile_high_state
		texture_cache_htile_high_gpu texture_cache_htile_registration_neighbors
		texture_cache_dcc_statistics_gpu gpu_counted_multi_draw_srd
		PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
	add_test(NAME buffer_cache_ranges
		COMMAND $<TARGET_FILE:shader_recompiler_compute_tests> --buffer-cache-range-only)

	add_custom_target(kyty_tests DEPENDS
		ime_dialog_tests
		bindless_lifecycle_tests
		bone_palette_staging_policy_tests
		shader_writer_history_tests
		shader_cfg_tests
		embedded_fetch_index_tests
		scalar_provenance_tests
		image_page_table_tests
		memory_tracker_tests
		page_manager_tests
		archive_file_tests
		trophy_system_tests
		avplayer_file_tests
		bit_array_tests
		frame_statistics_tests
		renderdoc_capture_request_tests
		dlss_contract_tests
		dlss_presentation_contract_tests
		mesh_dispatch_tests
		shader_precompile_record_tests
		lru_cache_tests
		shader_vertex_metadata_tests
		resource_materialization_tests
		resource_tracking_tests
		event_queue_lifetime_tests
		kernel_file_system_tests
		sync_on_address_tests
		shader_recompiler_compute_tests
		virtual_memory_allocation_tests
		audio_diag_tests
		audio_mix_tests
		audio_out2_port_tests
		save_data_memory_tests
		audio_out_timing_tests
		ngs2_sampler_tests
		pad_haptics_tests
		controller_settings_tests
		save_data_memory_tests
		png_enc_tests
		ces_tests
		save_data_dialog_tests
		http_uri_parse_tests
	)
endif()

# Test Darwin signal-context restoration for SSE4a instructions.
if(APPLE AND (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR
		(NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")))
	add_executable(macos_sse4a_tests EXCLUDE_FROM_ALL
		"${KYTY_TESTS_DIR}/macosSse4aTests.cpp"
		"${KYTY_SOURCE_DIR}/loader/x64InstructionEmulator.cpp"
		"${KYTY_SOURCE_DIR}/loader/guestInstructionPatcher.cpp"
	)
	target_link_libraries(macos_sse4a_tests common fmt::fmt Zydis::Zydis xbyak::xbyak)
	target_include_directories(macos_sse4a_tests PRIVATE ${inc_headers})
	if(BUILD_TESTING)
		add_test(NAME macos_sse4a COMMAND $<TARGET_FILE:macos_sse4a_tests>)
		set_tests_properties(macos_sse4a PROPERTIES TIMEOUT 15)
		add_dependencies(kyty_tests macos_sse4a_tests)
	endif()
endif()

add_executable(kyty_emulator "${KYTY_SOURCE_DIR}/main.cpp" ${kyty_emulator_src})
if(KYTY_SHARE_BUILD AND KYTY_CLANG_CL)
	# No RTTI data, and no typeid in the standard library (std::function would name each lambda).
	target_compile_options(kyty_emulator PRIVATE /GR- /D_HAS_STATIC_RTTI=0)
	target_compile_options(common PRIVATE /GR- /D_HAS_STATIC_RTTI=0)
endif()
configure_macos_guest_address_space(kyty_emulator)

set(KYTY_TROPHY_UNLOCK_SOUND "${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/trophy-unlock.wav")
add_custom_target(kyty_trophy_sound
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kyty_emulator>/assets/sounds"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${KYTY_TROPHY_UNLOCK_SOUND}"
		"$<TARGET_FILE_DIR:kyty_emulator>/assets/sounds/trophy-unlock.wav"
	VERBATIM)
add_dependencies(kyty_emulator kyty_trophy_sound)
install(FILES "${KYTY_TROPHY_UNLOCK_SOUND}" DESTINATION assets/sounds)

target_link_libraries(kyty_emulator ${kyty_emulator_link_libraries})
if(KYTY_AFTERMATH)
	message(STATUS "Nsight Aftermath: ${KYTY_AFTERMATH_SDK}")
	target_compile_definitions(kyty_emulator PRIVATE KYTY_AFTERMATH=1)
	target_include_directories(kyty_emulator PRIVATE "${KYTY_AFTERMATH_SDK}/include")
	target_link_libraries(kyty_emulator "${KYTY_AFTERMATH_SDK}/lib/x64/GFSDK_Aftermath_Lib.x64.lib")
	add_custom_command(TARGET kyty_emulator POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${KYTY_AFTERMATH_SDK}/lib/x64/GFSDK_Aftermath_Lib.x64.dll"
		$<TARGET_FILE_DIR:kyty_emulator>/GFSDK_Aftermath_Lib.x64.dll)
	install(FILES "${KYTY_AFTERMATH_SDK}/lib/x64/GFSDK_Aftermath_Lib.x64.dll" DESTINATION .)
endif()
if (WIN32)
	target_link_libraries(kyty_emulator opengl32)
	target_link_libraries(kyty_emulator iphlpapi)
	target_link_libraries(kyty_emulator setupapi)
	target_link_libraries(kyty_emulator ws2_32)
endif()
if (KYTY_CLANG_CL)
	target_link_libraries(kyty_emulator onecore)
endif()
if (CLANG AND NOT KYTY_CLANG_CL)
	target_link_libraries(kyty_emulator pthread)
endif()
# dlopen/dlsym/dladdr for RenderDoc.
target_link_libraries(kyty_emulator ${CMAKE_DL_LIBS})
target_include_directories(kyty_emulator PRIVATE ${inc_headers})

clang_tidy_check(kyty_emulator "" "${check_headers}" "${inc_headers}")

include_what_you_use(kyty_emulator "${inc_headers}")

set(KYTY_EMULATOR_MAP_NAME "kyty_emulator_${KYTY_COMPILER_ID}_${KYTY_LINKER_ID}.map")

set(KYTY_EMULATOR_MAP_LINK_PATH "${CMAKE_CURRENT_BINARY_DIR}/${KYTY_EMULATOR_MAP_NAME}")
set(KYTY_EMULATOR_PDB_LINK_PATH "${CMAKE_CURRENT_BINARY_DIR}/kyty_emulator.pdb")

if(KYTY_CLANG_CL)
	set(KYTY_WINDOWS_HOST_IMAGE_BASE "" CACHE STRING "Optional native host PE image base")
	# Native host allocations must not claim the guest's fixed virtual ranges.
	# The no-ASLR process layout also prevents NGX's NVX device from starting
	# after the large guest placeholders were reserved (native regression).
	option(KYTY_WINDOWS_HOST_ASLR "Randomize native host image and allocations" ON)
	if(KYTY_WINDOWS_HOST_ASLR)
		set(kyty_host_dynamicbase /DYNAMICBASE /HIGHENTROPYVA)
	else()
		set(kyty_host_dynamicbase /DYNAMICBASE:NO)
	endif()
	target_link_options(kyty_emulator PRIVATE
		${kyty_host_dynamicbase}
		"/DEBUG:FULL"
		"/PDB:${KYTY_EMULATOR_PDB_LINK_PATH}"
		"/lldmap:${KYTY_EMULATOR_MAP_LINK_PATH}"
	)
	if(KYTY_WINDOWS_HOST_IMAGE_BASE)
		target_link_options(kyty_emulator PRIVATE "/BASE:${KYTY_WINDOWS_HOST_IMAGE_BASE}")
	endif()
	add_custom_command(TARGET kyty_emulator POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${KYTY_THIRD_PARTY_DIR}/winpthread/bin/libwinpthread-1.dll" $<TARGET_FILE_DIR:kyty_emulator>/libwinpthread-1.dll)
elseif(WIN32 OR LINUX)
	set_target_properties(kyty_emulator PROPERTIES LINK_FLAGS "${KYTY_LD_OPTIONS}")
	# LINK_FLAGS is a space-delimited string: a -Map path containing spaces would
	# be split into bogus arguments, so pass it as a discrete link option instead.
	target_link_options(kyty_emulator PRIVATE "-Wl,-Map=${KYTY_EMULATOR_MAP_LINK_PATH}")
endif()

if(LINUX)
    target_link_options(kyty_emulator PRIVATE
        $<$<CONFIG:Release>:-Wl,--strip-all>
    )
endif()

if(APPLE)
	# The emulator writes x86-64 trampolines/PLT stubs into memory and executes them, and
	# runs guest code under Rosetta. That requires the JIT / unsigned-executable-memory
	# entitlements, so re-sign the binary after every link (an unsigned/relinked binary
	# reverts to the hardened defaults and aborts when it first executes written code).
	add_custom_command(TARGET kyty_emulator POST_BUILD
		COMMAND codesign -s - --force --options runtime
			--entitlements "${KYTY_SOURCE_DIR}/macos_jit.entitlements"
			$<TARGET_FILE:kyty_emulator>
		COMMENT "Codesign kyty_emulator with JIT entitlements (macOS)")
endif()

install(TARGETS kyty_emulator DESTINATION .)
if(WIN32)
	# Colors the emulator's console window (common/consoleColors.h); found next to the emulator.
	add_executable(kyty_console "${KYTY_SOURCE_DIR}/consoleHelper/kytyConsole.cpp")
	add_dependencies(kyty_emulator kyty_console)
	install(TARGETS kyty_console DESTINATION .)
endif()
if(KYTY_BUILD_LAUNCHER)
	add_subdirectory("${KYTY_SOURCE_DIR}/launcher" "${CMAKE_CURRENT_BINARY_DIR}/launcher")

	if(LINUX)
		target_link_options(launcher PRIVATE $<$<CONFIG:Release>:-Wl,--strip-all>)
	endif()
endif()
if(KYTY_CLANG_CL)
	install(FILES "${KYTY_THIRD_PARTY_DIR}/winpthread/bin/libwinpthread-1.dll" DESTINATION .)
endif()
