cmake_minimum_required(VERSION 3.14)
project(kitten LANGUAGES C)

# Git version - always regenerate on each build
# Generate default version_git.h at configure time (in case build fails)
file(WRITE ${CMAKE_BINARY_DIR}/version_git.h "#define GIT_VERSION \"unknown\"\n")

find_package(Git)
if(GIT_FOUND)
    set(GIT_CMD ${GIT_EXECUTABLE})
else()
    set(GIT_CMD "git")
endif()

add_custom_target(version_git ALL
    COMMAND sh ${CMAKE_SOURCE_DIR}/scripts/update_version.sh ${CMAKE_BINARY_DIR}/version_git.h ${GIT_CMD}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Updating git version..."
)
include_directories(${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/log)
include_directories(${PROJECT_SOURCE_DIR}/subprojects/glad/include)
include_directories(${PROJECT_SOURCE_DIR}/subprojects/inih_dep)

# ------------------------------------------------------------------------------
# Auto-generate the GL hook forwarder list from the glad GL header at build
# time. scripts/gen_glhooks.py parses glad/gl.h and emits glHooks_list.h, which
# glHooks.c includes. GLX functions are bridged separately in glxBridge.c and are
# not handled by this generator.
# ------------------------------------------------------------------------------
find_program(PYTHON_EXECUTABLE NAMES python python3)
if(NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR "Python is required to generate glHooks_list.h from the glad headers")
endif()

set(GLAD_GL_H ${PROJECT_SOURCE_DIR}/subprojects/glad/include/glad/gl.h)
set(GEN_GLHOOKS_H ${CMAKE_BINARY_DIR}/glHooks_list.h)

add_custom_command(
    OUTPUT ${GEN_GLHOOKS_H}
    COMMAND ${PYTHON_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/gen_glhooks.py
            ${GLAD_GL_H} ${GEN_GLHOOKS_H}
    DEPENDS ${GLAD_GL_H} ${PROJECT_SOURCE_DIR}/scripts/gen_glhooks.py
    COMMENT "Generating glHooks_list.h from glad GL header"
    VERBATIM
)
add_custom_target(gen_glhooks DEPENDS ${GEN_GLHOOKS_H})

# ------------------------------------------------------------------------------
# Third-party dependencies (fetched from GitHub)
# ------------------------------------------------------------------------------

# MinHook - API hooking library (source fetched from GitHub, built by us)
# Note: minhook's repo has no CMakeLists.txt, so we build it manually.
include(FetchContent)
cmake_policy(SET CMP0169 OLD)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
endif()

file(GLOB_RECURSE MINHOOK_SRC ${minhook_SOURCE_DIR}/src/*.c)
set(MINHOOK_C_FLAGS -fPIC -m32)
add_library(minhook STATIC ${MINHOOK_SRC})
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
target_compile_options(minhook PRIVATE ${MINHOOK_C_FLAGS})

# inih - INI file parser (vendored)
set(INIH_C_FLAGS -fPIC -m32)
add_library(inih STATIC subprojects/inih_dep/ini.c)
target_include_directories(inih PUBLIC subprojects/inih_dep)
target_compile_options(inih PRIVATE ${INIH_C_FLAGS})


enable_language(RC)

# Gather all source files
file(GLOB SRC_FILES
    src/init.c
    src/main.c
    src/games/*.c
    src/games/idas/*.c
    src/cmd.c
    src/audio/*.c
    src/config/*.c
    src/graphics/*.c
    src/hooks/*.c
    src/devices/*.c
    src/input/*.c
    src/log/*.c
    src/utils/*.c
    src/elfLoader/*.c
    src/elfLoader/pthread/*.c
    subprojects/glad/src/gl.c
)

# Executable target
add_executable(${PROJECT_NAME} ${SRC_FILES})

add_dependencies(${PROJECT_NAME} version_git)
add_dependencies(${PROJECT_NAME} gen_glhooks)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _WIN32_WINNT=0x0600
    WINVER=0x0600
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -fpermissive
    -Wno-unused-parameter
    -Wunused-variable
    -Wno-unused-but-set-variable
    -Wimplicit-fallthrough=0
    -Wstrict-aliasing=0
)

# Increase stack size to 4MB to prevent stack overflow on startup
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug build: emit debug symbols, disable LTO/strip and optimization so the
    # binary is directly debuggable with gdb.
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0)
    target_link_options(${PROJECT_NAME} PRIVATE
        -static
        -Wl,--stack,4194304
        -Wl,--large-address-aware
    )
else()
    target_link_options(${PROJECT_NAME} PRIVATE
        -static
        -flto=auto
        -s
        -Wl,--stack,4194304
        -Wl,--large-address-aware
    )
endif()

target_link_directories(${PROJECT_NAME} PRIVATE "libs")

# Link dependencies
target_link_libraries(${PROJECT_NAME} minhook -static-libgcc -static-libstdc++ xaudio2_9 xapofx gdi32 winmm ole32 ws2_32 opengl32 glu32 pthread psapi xinput dinput8 dxgi dxguid inih iphlpapi mfplat mf mfreadwrite propsys uuid gnurx tre intl iconv dbghelp imm32)