cmake_minimum_required(VERSION 3.14)
project(test-amx)

include(CheckCXXCompilerFlag)

enable_testing()

if(UNIX)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
add_definitions(-std=c++11)
elseif(COMPILER_SUPPORTS_CXX0X)
add_definitions(-std=c++0x)
endif()
endif()

CHECK_CXX_COMPILER_FLAG("-march=sapphirerapids" COMPILER_SUPPORTS_MARCH_SPR)
if(COMPILER_SUPPORTS_MARCH_SPR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=sapphirerapids  -mamx-tile  -mamx-int8  -mamx-bf16")
else()
  message(FATAL_ERROR "Please upgrade your compiler to support SPR fetaures (GCC11+")
endif()


add_executable(test-amx ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(test-amx m )
