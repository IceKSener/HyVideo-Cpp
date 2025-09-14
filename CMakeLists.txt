cmake_minimum_required(VERSION 3.10.0)
project(HyVideo VERSION 0.1.0 LANGUAGES C CXX)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB other_cpp src/*.cpp)
add_definitions(-D__STDC_CONSTANT_MACROS)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -fopenmp)
link_libraries(avcodec)
link_libraries(avformat)
link_libraries(avutil)
add_executable(HyVideo HyVideo.cpp ${other_cpp})

include(CTest)
enable_testing()

include(CPack)