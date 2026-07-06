cmake_minimum_required(VERSION 3.19)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Main)

if(ANDROID)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_DEBUG_POSTFIX "")
endif()

# use custom file to find libraries
if(WIN32)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
endif()

# we need the find scripts for older Linux distros
if(UNIX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake_unix")
endif()

# VMA finder script
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake_vma")

# Sanitizer finder scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake_san")

# optimize for speed on Clang or GCC
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  #set(CMAKE_CXX_FLAGS "-O3 -fsanitize=address,undefined -static-libasan -g")
  set(CMAKE_CXX_FLAGS "-O3 -g")
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message("-- Using ccache")
  set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif(CCACHE_FOUND)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# get ImGui, no build needed (only the files)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG        v1.92.7
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(imgui)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
  add_subdirectory(${imgui_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

# get ImGuiFileDialog, also files only
FetchContent_Declare(
  filedialog
  GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog
  GIT_TAG        v0.6.8
  GIT_SHALLOW 1
)

# ugly hack to have ImGui available when the file dialog source getch fetched
get_property(current_compile_options DIRECTORY PROPERTY COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS -I ${imgui_SOURCE_DIR})
FetchContent_MakeAvailable(filedialog)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS ${current_compile_options})
unset(current_compile_options)

FetchContent_GetProperties(filedialog)
if(NOT imgui_POPULATED)
  FetchContent_Populate(filedialog)
  add_subdirectory(${filedialog_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

# get STB image reader
FetchContent_Declare(
  stbi
  GIT_REPOSITORY https://github.com/nothings/stb
  GIT_TAG        master
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(stbi)

FetchContent_GetProperties(stbi)
if(NOT stbi_POPULATED)
  FetchContent_Populate(stbi)
  add_subdirectory(${stbi_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

# imnodes
set(IMNODES_IMGUI_TARGET_NAME imgui)
FetchContent_Declare(
  imnodes
  GIT_REPOSITORY https://github.com/Nelarius/imnodes
  GIT_TAG        b2ec254
  GIT_SHALLOW 0
)

get_property(current_compile_options DIRECTORY PROPERTY COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS -I ${imgui_SOURCE_DIR})
FetchContent_MakeAvailable(imnodes)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS ${current_compile_options})
unset(current_compile_options)

FetchContent_GetProperties(imnodes)
if(NOT imnodes_POPULATED)
  FetchContent_Populate(imnodes)
  add_subdirectory(${imnodes_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

# get vk-bootstrap
include(FetchContent)
if(ANDROID)
  set(VK_BOOTSTRAP_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
  vk_bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
  # with Ubuntu, v1.3.296 and newer broke compile in PFN_vkCmdSetDepthClampRangeEXT
  GIT_TAG        v1.3.302
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(vk_bootstrap)

FetchContent_GetProperties(vk_bootstrap)
if(NOT vk_bootstrap_POPULATED)
  FetchContent_Populate(vk_bootstrap)
  add_subdirectory(${vk_bootstrap_SOURCE_DIR} ${vk_bootstrap_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# get VMA
# Skip downloading if Vulkan SDK has been found (always fetch on Android)
if (ANDROID OR NOT DEFINED ENV{VK_SDK_PATH})
  include(FetchContent)
  FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG        v3.3.0
    GIT_SHALLOW 1
  )

  FetchContent_MakeAvailable(vma)

  FetchContent_GetProperties(vma)
  if(NOT vma_POPULATED)
    FetchContent_Populate(vma)
    add_subdirectory(${vma_SOURCE_DIR} EXCLUDE_FROM_ALL)
  endif()

  # set variable that will be used later
  set(VMA_INCLUDE_DIR ${vma_SOURCE_DIR}/include)
endif()

# yaml-cpp and GLM via FetchContent on Android/Windows
if(ANDROID OR (WIN32 AND NOT MINGW))
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW 1
  )

  FetchContent_MakeAvailable(yaml-cpp)

  FetchContent_GetProperties(yaml-cpp)
  if(NOT yaml-cpp_POPULATED)
    FetchContent_Populate(yaml-cpp)
    add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()

  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG        1.0.3
    GIT_SHALLOW 1
  )

  FetchContent_MakeAvailable(glm)

  FetchContent_GetProperties(glm)
  if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
    add_subdirectory(${glm_SOURCE_DIR} EXCLUDE_FROM_ALL)
  endif()

  set(GLM_INCLUDE_DIR ${glm_SOURCE_DIR})
endif()

if(WIN32 AND NOT MINGW AND NOT ANDROID)
  FetchContent_Declare(
    glfw3
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
    GIT_SHALLOW 1
  )

  FetchContent_MakeAvailable(glfw3)

  FetchContent_GetProperties(glfw3)
  if(NOT glfw3_POPULATED)
    FetchContent_Populate(glfw3)
    add_subdirectory(${glfw3_SOURCE_DIR} ${glfw3_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()

  set(GLFW3_INCLUDE_DIR ${glfw3_SOURCE_DIR}/include)
  set(GLFW3_LIBRARY "${glfw3_BINARY_DIR}/src/$<CONFIGURATION>/glfw3.lib")
endif()

if(ANDROID)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp
    GIT_TAG v5.4.3
    GIT_SHALLOW 1
  )
  FetchContent_MakeAvailable(assimp)
endif()

# OpenXR SDK
## Steam VR only supports OpenXR AP 1.0
FetchContent_Declare(
  openxr
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK
  GIT_TAG release-1.1.57
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(openxr)

FetchContent_GetProperties(openxr)
if(NOT openxr_POPULATED)
  FetchContent_Populate(openxr)
  add_subdirectory(${openxr_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()


file(GLOB SOURCES
  window/*.cpp
  tools/*.cpp
  vulkan/*.cpp
  model/*.cpp
  octree/*.cpp
  graphnodes/*.cpp
  vr/*.cpp
  ${vk_bootstrap_SOURCE_DIR}/*.cpp
)

file(GLOB IMGUI_SOURCES ${imgui_SOURCE_DIR}/*.cpp)
file(GLOB FILEDIALOG_SOURCES ${filedialog_SOURCE_DIR}/*.cpp)

if(ANDROID)
  list(APPEND SOURCES
    platform/AndroidMain.cpp
    platform/AssetPaths.cpp
    vulkan/UserInterface_android.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    ${IMGUI_SOURCES}
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imnodes_SOURCE_DIR}/imnodes.cpp
  )
  list(REMOVE_ITEM SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/AudioManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/UserInterface.cpp
  )
else()
  list(APPEND SOURCES
    Main.cpp
    ${IMGUI_SOURCES}
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${FILEDIALOG_SOURCES}
    ${imnodes_SOURCE_DIR}/imnodes.cpp
  )
endif()

if(ANDROID)
  add_library(${PROJECT_NAME} SHARED ${SOURCES})
else()
  add_executable(${PROJECT_NAME} ${SOURCES})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC window tools vulkan model octree graphnodes vr platform)

if(ANDROID)
  target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imnodes_SOURCE_DIR}
    ${stbi_SOURCE_DIR} ${yaml-cpp_SOURCE_DIR} ${vk_bootstrap_SOURCE_DIR}/src ${VMA_INCLUDE_DIR})
else()
  target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
    ${filedialog_SOURCE_DIR} ${stbi_SOURCE_DIR} ${yaml-cpp_SOURCE_DIR} ${imnodes_SOURCE_DIR}
    ${vk_bootstrap_SOURCE_DIR}/src ${vma_SOURCE_DIR}/include)
endif()

find_package(Vulkan REQUIRED)

if(ANDROID)
  set(ASSIMP_INCLUDE_DIR ${assimp_SOURCE_DIR}/include)
  set(ASSIMP_LIBRARY assimp)
  set(ASSIMP_ZLIB_LIBRARY z)
  include_directories(${GLM_INCLUDE_DIR} ${ASSIMP_INCLUDE_DIR} ${yaml-cpp_SOURCE_DIR}/include
    ${VMA_SOURCE_DIR}/include ${ANDROID_NDK}/sources/android/native_app_glue)
else()
  find_package(glfw3 3.3 REQUIRED)
  find_package(glm REQUIRED)
  find_package(assimp REQUIRED)
  find_package(yaml-cpp REQUIRED)
  find_package(vma REQUIRED)
  find_package(SDL2 REQUIRED)
  find_package(SDL2_mixer REQUIRED)

  include_directories(${GLFW3_INCLUDE_DIR} ${GLM_INCLUDE_DIRS} ${ASSIMP_INCLUDE_DIR} ${YAML_CPP_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${VMA_SOURCE_DIR}/vma)
endif()


# compile shaders
file(GLOB GLSL_SOURCE_FILES
  shader/*.frag
  shader/*.vert
  shader/*.comp
)

if(ANDROID AND NOT Vulkan_GLSLC_EXECUTABLE)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_NDK_HOST_TAG "windows-x86_64")
    set(_GLSLC_SUFFIX ".exe")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_NDK_HOST_TAG "linux-x86_64")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
      set(_NDK_HOST_TAG "darwin-arm64")
    else()
      set(_NDK_HOST_TAG "darwin-x86_64")
    endif()
  endif()
  if(_NDK_HOST_TAG)
    set(Vulkan_GLSLC_EXECUTABLE "${ANDROID_NDK}/shader-tools/${_NDK_HOST_TAG}/glslc${_GLSLC_SUFFIX}")
    message(STATUS "Android shader build using ${Vulkan_GLSLC_EXECUTABLE}")
  endif()
endif()

if(Vulkan_GLSLC_EXECUTABLE)
  message("Using glslc to compile shaders")
  foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_SOURCE_DIR}/shader/${FILE_NAME}.spv")
    if(ANDROID)
      set(_GLSLC_FLAGS -DANDROID_XR_MONO=1 -I${CMAKE_SOURCE_DIR}/shader)
    else()
      set(_GLSLC_FLAGS -I${CMAKE_SOURCE_DIR}/shader)
    endif()
    add_custom_command(
      OUTPUT ${SPIRV}
      COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${_GLSLC_FLAGS} -o ${SPIRV} ${GLSL}
      DEPENDS ${GLSL} ${CMAKE_SOURCE_DIR}/shader/xr_view.glsl)
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
  endforeach(GLSL)
elseif (Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
  message("Using glslangValidator to compile shaders")
  foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_SOURCE_DIR}/shader/${FILE_NAME}.spv")
    add_custom_command(
      OUTPUT ${SPIRV}
      COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V -o ${SPIRV} ${GLSL}
      DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
  endforeach(GLSL)
endif()

add_custom_target(
  Shaders
  DEPENDS ${SPIRV_BINARY_FILES}
)
add_dependencies(${PROJECT_NAME} Shaders)

if(NOT ANDROID)
add_custom_command(TARGET Shaders POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/shader"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/shader"
)

# copy textures
file(GLOB TEX_SOURCE_FILES
  textures/*
)

add_custom_target(
  Textures
  DEPENDS ${TEX_SOURCE_FILES}
)
add_dependencies(${PROJECT_NAME} Textures)

add_custom_command(TARGET Textures POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/textures"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/textures"
)

# copy assets
file(GLOB ASSET_FILES
  assets/*
)

add_custom_target(
  Assets
  DEPENDS ${ASSET_FILES}
)

add_dependencies(${PROJECT_NAME} Assets)

add_custom_command(TARGET Assets POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/assets"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/assets"
)

# copy config files
file(GLOB CONFIG_FILE
  config/*
)

add_custom_target(
  ConfigFile
  DEPENDS ${CONFIG_FILE}
)

add_dependencies(${PROJECT_NAME} ConfigFile)

add_custom_command(TARGET ConfigFile POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/config"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/config"
)

# copy imgui.ini
file(GLOB IMGUI_INI imgui.ini)

add_custom_target(
  ImGuiIniFile
  DEPENDS ${IMGUI_INI}
)

add_dependencies(${PROJECT_NAME} ImGuiIniFile)

add_custom_command(TARGET ImGuiIniFile POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/imgui.ini"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/imgui.ini"
)

# copy controls.txt
file(GLOB CONTROLS_TXT controls.txt)

add_custom_target(
  ControlsTxtFile
  DEPENDS ${CONTROLS_TXT}
)

add_dependencies(${PROJECT_NAME} ControlsTxtFile)

add_custom_command(TARGET ControlsTxtFile POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  "$<TARGET_PROPERTY:${PROJECT_NAME},SOURCE_DIR>/controls.txt"
  "$<TARGET_PROPERTY:${PROJECT_NAME},BINARY_DIR>/$<CONFIGURATION>/controls.txt"
)
endif()

# Copy SDL2 DLLs
if (WIN32 AND NOT MINGW AND NOT ANDROID)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
   "${SDL2_INCLUDE_DIR}/../lib/x64/SDL2.dll"
   "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    VERBATIM)

  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
   "${SDL2_MIXER_INCLUDE_DIRS}/../lib/x64/SDL2_mixer.dll"
   "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    VERBATIM)
 endif()

# FindAssimp.cmake sets ASSIMP_LIBRARY and ASSIMP_ZLIB_LIBRARY/FindGLFW3 set GLF3_LIBRARY under Windows, reuse the value for Linux
if(UNIX)
  set(ASSIMP_LIBRARY assimp)
  set(ASSIMP_ZLIB_LIBRARY z)
  set(GLFW3_LIBRARY glfw)
endif()

# enable experimental GLM functions globally, plus add ImGui define for imnodes etc
add_definitions(-DGLM_ENABLE_EXPERIMENTAL -DIMGUI_DEFINE_MATH_OPERATORS)

# allow ImGuiFileDialog dialogs to be closed with ESC
add_definitions(-DUSE_DIALOG_EXIT_WITH_KEY -DIGFD_EXIT_KEY=ImGuiKey_Escape)

# Fix "wrong" depth in Vulkan
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)

if(WIN32)
  # SDL defines its own main(), conflicts on Windows
  add_definitions(-DSDL_MAIN_HANDLED)
endif()

if(ANDROID)
  target_compile_definitions(${PROJECT_NAME} PRIVATE XR_USE_PLATFORM_ANDROID)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    ${ASSIMP_LIBRARY}
    ${ASSIMP_ZLIB_LIBRARY}
    Vulkan::Vulkan
    yaml-cpp::yaml-cpp
    vk-bootstrap::vk-bootstrap
    openxr_loader
    android
    log
    dl)
elseif(MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    ${ASSIMP_LIBRARY}
    ${ASSIMP_ZLIB_LIBRARY}
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARIES}
    Vulkan::Vulkan
    yaml-cpp::yaml-cpp
    vk-bootstrap::vk-bootstrap
    openxr_loader)
else()
  if(yaml-cpp_VERSION VERSION_LESS "0.8")
    target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLFW3_LIBRARY}
    ${ASSIMP_LIBRARY}
    ${ASSIMP_ZLIB_LIBRARY}
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARIES}
    Vulkan::Vulkan
    yaml-cpp
    vk-bootstrap::vk-bootstrap
    openxr_loader
    stdc++ m)
  else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLFW3_LIBRARY}
    ${ASSIMP_LIBRARY}
    ${ASSIMP_ZLIB_LIBRARY}
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARIES}
    Vulkan::Vulkan
    yaml-cpp::yaml-cpp
    vk-bootstrap::vk-bootstrap
    openxr_loader
    stdc++ m)
  endif()
endif()
