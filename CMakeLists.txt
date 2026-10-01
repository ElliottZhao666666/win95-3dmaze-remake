cmake_minimum_required(VERSION 3.25)
project(Win95_3Dmaze_Remake LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(MSVC)
    add_compile_options(/W4 /EHsc /permissive- /utf-8 /DUNICODE /D_UNICODE)
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shader-bin")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")
find_program(FXC_EXECUTABLE fxc.exe
    PATHS
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
    NO_DEFAULT_PATH)
if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe was not found. Install the Windows 10 SDK.")
endif()

set(SHADER_OUTPUTS
    "${SHADER_BIN_DIR}/maze_vs.cso"
    "${SHADER_BIN_DIR}/maze_ps.cso"
    "${SHADER_BIN_DIR}/map_vs.cso"
    "${SHADER_BIN_DIR}/map_ps.cso")
add_custom_command(
    OUTPUT ${SHADER_OUTPUTS}
    COMMAND "${FXC_EXECUTABLE}" /nologo /T vs_5_0 /E VSMain /Fo "${SHADER_BIN_DIR}/maze_vs.cso" "${SHADER_DIR}/maze.hlsl"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ps_5_0 /E PSMain /Fo "${SHADER_BIN_DIR}/maze_ps.cso" "${SHADER_DIR}/maze.hlsl"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T vs_5_0 /E MapVS /Fo "${SHADER_BIN_DIR}/map_vs.cso" "${SHADER_DIR}/maze.hlsl"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ps_5_0 /E MapPS /Fo "${SHADER_BIN_DIR}/map_ps.cso" "${SHADER_DIR}/maze.hlsl"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/shader-bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SHADER_OUTPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/shader-bin"
    DEPENDS "${SHADER_DIR}/maze.hlsl")
add_custom_target(shader_assets DEPENDS ${SHADER_OUTPUTS})

set(EMBEDDED_ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/wall.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/floor.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ceiling.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/rat.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fin.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/start2.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/gl.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/dx.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/yb.jpg")
set_source_files_properties(Resource.rc PROPERTIES
    OBJECT_DEPENDS "${SHADER_OUTPUTS};${EMBEDDED_ASSETS}")

add_executable(3dmaze WIN32
    src/main.cpp
    src/MazeLogic.cpp
    src/MazeLogic.h
    src/MazeRenderer.cpp
    src/MazeRenderer.h
    Resource.rc
    resource.h
    ${SHADER_OUTPUTS})
add_dependencies(3dmaze shader_assets)
set_target_properties(3dmaze PROPERTIES OUTPUT_NAME "Win95-3Dmaze-Remake" SUFFIX ".scr")
target_include_directories(3dmaze PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(3dmaze PRIVATE d3d11 dxgi d3dcompiler comctl32 windowscodecs ole32 shell32 user32 gdi32 advapi32)

foreach(mode IN ITEMS c s p)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/run_${mode}.bat"
        INPUT "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_${mode}.bat.in")
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(maze_logic_tests tests/maze_logic_tests.cpp src/MazeLogic.cpp)
    target_include_directories(maze_logic_tests PRIVATE src)
    add_test(NAME maze_logic COMMAND maze_logic_tests)
endif()
