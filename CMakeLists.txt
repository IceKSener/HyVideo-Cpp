cmake_minimum_required(VERSION 3.10.0)
project(HyVideo VERSION 0.1.0 LANGUAGES C CXX)

SET (CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)

pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
include_directories(${AVFORMAT_INCLUDE_DIRS} ${AVCODEC_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS})

set(NCNN_DIR "W:/ncnn/build_mw/install/lib/cmake/ncnn")
find_package(NCNN REQUIRED)
# include_directories(${NCNN_INCLUDE_DIRS})

file(GLOB other_cpp src/*.cpp src/rife/*.cpp)

# add_definitions(-D__STDC_CONSTANT_MACROS)
# set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -fopenmp)
# link_libraries(avcodec avformat avutil)
# link_libraries(ncnn SPIRV)

add_executable(HyVideo HyVideo.cpp ${other_cpp})
target_link_libraries(HyVideo ncnn)

# 查找所有 .comp 文件
file(GLOB COMP_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "include/rife/comps/*.comp")
# 收集生成的头文件
set(GENERATED_HEADERS)
# 遍历每个 .comp 文件
foreach(comp_file IN LISTS COMP_FILES)
    # 输入文件绝对路径
    set(SHADER_SRC_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${comp_file})
    # 输出头文件
    get_filename_component(fname_we ${comp_file} NAME_WE)
    set(SHADER_COMP_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/rife/comps/${fname_we}.comp.hex.h)
    # 加入生成列表
    list(APPEND GENERATED_HEADERS ${SHADER_COMP_HEADER})
    # 调用脚本转换
    add_custom_command(
        OUTPUT ${SHADER_COMP_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DSHADER_SRC=${SHADER_SRC_ABS}
            -DSHADER_COMP_HEADER=${SHADER_COMP_HEADER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader2header.cmake
        DEPENDS ${SHADER_SRC_ABS}
        COMMENT "Converting ${comp_file} → ${fname_we}.comp.hex.h"
        VERBATIM
    )
endforeach()
add_custom_target(GenerateShaderHeaders
    DEPENDS ${GENERATED_HEADERS}
)
# 确保在编译前生成头文件
add_dependencies(HyVideo GenerateShaderHeaders)



include(CTest)
enable_testing()

include(CPack)