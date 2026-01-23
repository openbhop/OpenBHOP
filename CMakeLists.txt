cmake_minimum_required(VERSION 3.20)

# ------------------------------------------------------------------------------
# 1. Project Setup
# ------------------------------------------------------------------------------
project(bh LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Emscripten requires GNU extensions for EM_ASM macros in C code
if(EMSCRIPTEN)
    set(CMAKE_C_EXTENSIONS ON)
else()
    set(CMAKE_C_EXTENSIONS OFF)
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Web / Emscripten
# ------------------------------------------------------------------------------
set(BH_EMSCRIPTEN OFF)
if(EMSCRIPTEN)
    set(BH_EMSCRIPTEN ON)
endif()

# ------------------------------------------------------------------------------
# 2. Dependencies (SDL3 & RmlUi)
# ------------------------------------------------------------------------------

# --- SDL3 ---
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

# Emscripten does not support dynamic linking in the traditional sense.
if(BH_EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/sdl3/CMakeLists.txt")
    add_subdirectory(extern/sdl3)
    get_directory_property(SDL3_TARGETS DIRECTORY extern/sdl3 BUILDSYSTEM_TARGETS)
    foreach(T ${SDL3_TARGETS})
        if(TARGET ${T})
            set_target_properties(${T} PROPERTIES FOLDER "ThirdParty/SDL3")
        endif()
    endforeach()
else()
    find_package(SDL3 CONFIG REQUIRED)
endif()

# --- RmlUi ---
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS   OFF CACHE BOOL "" FORCE)
set(RMLUI_INSTALL OFF CACHE BOOL "" FORCE)

if(BH_EMSCRIPTEN)
    # RmlUi uses CMake's standard BUILD_SHARED_LIBS. Web builds should be static.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/interface/CMakeLists.txt")
    add_subdirectory(extern/interface)
    get_directory_property(RMLUI_TARGETS DIRECTORY extern/interface BUILDSYSTEM_TARGETS)
    if (RMLUI_TARGETS)
        set_target_properties(${RMLUI_TARGETS} PROPERTIES FOLDER "ThirdParty/RmlUi")
    endif()
endif()

# ------------------------------------------------------------------------------
# 3. Internal Tools
# ------------------------------------------------------------------------------
if(BH_EMSCRIPTEN)
    # When cross-compiling we can't run a tool built with the target compiler.
    # Use a small Python reflection helper instead.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(BH_SPIRV_REFLECT_CMD ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/bh_spirv_reflect.py")
    set(BH_SPIRV_REFLECT_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bh_spirv_reflect.py")
else()
    add_executable(bh_spirv_reflect tools/bh_spirv_reflect.c)
    set_target_properties(bh_spirv_reflect PROPERTIES
        FOLDER "Build Tools"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )
    set(BH_SPIRV_REFLECT_CMD $<TARGET_FILE:bh_spirv_reflect>)
    set(BH_SPIRV_REFLECT_DEPS bh_spirv_reflect)
endif()

# ------------------------------------------------------------------------------
# 4. Shader Pipeline (deterministic + no regen churn)
# ------------------------------------------------------------------------------
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/compiled_shaders")

# Require DXC
find_program(DXC_EXECUTABLE NAMES dxc dxc.exe)
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "DXC (dxc.exe) not found in PATH")
endif()

set(DXC_COMMON_ARGS -I "${SHADER_SRC_DIR}")

# Helper function: add one HLSL shader (compile to SPIR-V then reflect to JSON)
function(add_hlsl_shader in_file profile entry)
    get_filename_component(fname "${in_file}" NAME)
    string(REGEX REPLACE "\\.hlsl$" ".spv" spv_name "${fname}")
    string(REGEX REPLACE "\\.hlsl$" ".json" json_name "${fname}")

    set(spv "${SHADER_OUT_DIR}/${spv_name}")
    set(json "${SHADER_OUT_DIR}/${json_name}")

    add_custom_command(
        OUTPUT "${spv}" "${json}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMAND "${DXC_EXECUTABLE}" ${DXC_COMMON_ARGS}
                -T "${profile}" -E "${entry}" -spirv
                -Fo "${spv}" "${in_file}"
        COMMAND ${BH_SPIRV_REFLECT_CMD} "${spv}" "${json}"
        MAIN_DEPENDENCY "${in_file}"
        DEPENDS ${BH_SPIRV_REFLECT_DEPS}
        COMMENT "Compile+reflect ${in_file} -> ${spv_name}, ${json_name}"
        VERBATIM
    )

    # Accumulate into a global property we can read back
    set_property(GLOBAL APPEND PROPERTY BH_SHADER_PRODUCTS "${spv};${json}")
endfunction()

# NOTE: GLOB *without* CONFIGURE_DEPENDS to avoid forcing CMake regeneration
file(GLOB SHADER_VERTS "${SHADER_SRC_DIR}/*.vert.hlsl")
file(GLOB SHADER_FRAGS "${SHADER_SRC_DIR}/*.frag.hlsl")
file(GLOB SHADER_COMPS "${SHADER_SRC_DIR}/*.comp.hlsl")

foreach(SRC ${SHADER_VERTS})
    add_hlsl_shader("${SRC}" "vs_6_0" "vs_main")
endforeach()
foreach(SRC ${SHADER_FRAGS})
    add_hlsl_shader("${SRC}" "ps_6_0" "ps_main")
endforeach()
foreach(SRC ${SHADER_COMPS})
    # Adjust entry if you use something else than "main"
    add_hlsl_shader("${SRC}" "cs_6_0" "main")
endforeach()

# Create a single target that produces all shaders, but keep it OUT of ALL.
get_property(BH_SHADER_PRODUCTS GLOBAL PROPERTY BH_SHADER_PRODUCTS)
add_custom_target(bh_shaders DEPENDS ${BH_SHADER_PRODUCTS})
set_target_properties(bh_shaders PROPERTIES FOLDER "Build Tools")

# ------------------------------------------------------------------------------
# 5. Main Executable
# ------------------------------------------------------------------------------
file(GLOB_RECURSE BH_SOURCES "src/*.c" "src/*.cpp" "src/*.h")

add_executable(bh ${BH_SOURCES})

target_include_directories(bh PRIVATE "src" "extern/interface/Backends")
target_link_libraries(bh PRIVATE SDL3::SDL3 rmlui_core)

# Desktop OpenGL backend support (loaded at runtime via SDL_GL_GetProcAddress).
# Emscripten builds use the SDL_gpu path and do not require system OpenGL.
if(NOT BH_EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    target_link_libraries(bh PRIVATE OpenGL::GL)
endif()

get_property(BH_SHADER_PRODUCTS GLOBAL PROPERTY BH_SHADER_PRODUCTS)
set_source_files_properties(${BH_SHADER_PRODUCTS} PROPERTIES GENERATED TRUE)
target_sources(bh PRIVATE ${BH_SHADER_PRODUCTS})

if (MSVC)
    target_compile_options(bh PRIVATE /W4 /WX-)
    target_compile_definitions(bh PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(bh PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------------------
# 5b. Emscripten output (index.html + asset packaging)
# ------------------------------------------------------------------------------
if(BH_EMSCRIPTEN)
    # Have emcc emit index.html as the primary output.
    set_target_properties(bh PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")

    # Enable the virtual filesystem and package assets into it.
    # Assets are preloaded to /assets to match BH_App's asset_root on the web.
    target_link_options(bh PRIVATE
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
		"SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
		"SHELL:-sSTACK_SIZE=2097152"

        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${SHADER_OUT_DIR}@/assets/shaders/compiled"
    )
endif()

# ------------------------------------------------------------------------------
# 6. Post-Build: Copy runtime DLLs & assets (pure post-link step)
# ------------------------------------------------------------------------------
if(BH_EMSCRIPTEN)
    # Web builds preload assets via emcc link options above.
elseif (WIN32)
    add_custom_command(TARGET bh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:bh>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rmlui_core> $<TARGET_FILE_DIR:bh>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:freetype>   $<TARGET_FILE_DIR:bh>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:bh>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_OUT_DIR}"
                "$<TARGET_FILE_DIR:bh>/assets/shaders/compiled"
        COMMENT "Copying DLLs, assets, and compiled shaders..."
    )
else()
    # On non-Windows desktop platforms, ensure assets (including OpenGL GLSL)
    # and compiled shader reflections are copied next to the executable.
    add_custom_command(TARGET bh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:bh>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_OUT_DIR}"
                "$<TARGET_FILE_DIR:bh>/assets/shaders/compiled"
        COMMENT "Copying assets and compiled shaders..."
    )
endif()

# ------------------------------------------------------------------------------
# 7. Solution organization
# ------------------------------------------------------------------------------
set_target_properties(bh PROPERTIES FOLDER "App")

# (Optional) Show shader sources in the IDE for easy editing
set(ALL_SHADER_SOURCES ${SHADER_VERTS} ${SHADER_FRAGS} ${SHADER_COMPS})
source_group(TREE "${SHADER_SRC_DIR}" PREFIX "Shaders" FILES ${ALL_SHADER_SOURCES})
target_sources(bh PRIVATE ${ALL_SHADER_SOURCES})
set_source_files_properties(${ALL_SHADER_SOURCES}
    PROPERTIES HEADER_FILE_ONLY TRUE VS_TOOL_OVERRIDE "Text")

