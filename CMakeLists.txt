cmake_minimum_required(VERSION 3.14)
project(chronloss LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# -----------------------
# GLFW
# -----------------------
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.8
)

set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

# -----------------------
# GLAD
# -----------------------
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)

set(GLAD_PROFILE "core")
set(GLAD_API "gl=4.6")
set(GLAD_GENERATOR "c")

FetchContent_MakeAvailable(glad)

# -----------------------
# OpenGL
# -----------------------
find_package(OpenGL REQUIRED)

# -----------------------
# Source files (auto)
# -----------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
  src/*.cpp
  engine/**/*.cpp
)

add_executable(chronloss ${SOURCES})

if (WIN32)
  target_compile_definitions(chronloss PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# -----------------------
# Include directories
# -----------------------
target_include_directories(chronloss PRIVATE
  ${CMAKE_SOURCE_DIR}
)

# -----------------------
# Link libraries
# -----------------------
target_link_libraries(chronloss PRIVATE
  glad
  glfw
  OpenGL::GL
)

# -----------------------
# Copy Assets to Build Dir
# -----------------------
add_custom_command(TARGET chronloss POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
    "$<TARGET_FILE_DIR:chronloss>/engine/assets"
    COMMENT "Copying assets to build directory..."
)