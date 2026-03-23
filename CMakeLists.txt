cmake_minimum_required(VERSION 3.10.0)
project(HyVideo VERSION 0.1.0 LANGUAGES C CXX)

# set(CMAKE_VERBOSE_MAKEFILE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

SET (project_dir ${CMAKE_CURRENT_LIST_DIR})
SET (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${project_dir}/bin)
SET (CMAKE_CXX_STANDARD 17)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_definitions("$<$<CONFIG:Debug>:DEBUG>")

include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)
find_package(ncnn REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
link_directories(${FFMPEG_LIBRARY_DIRS})
include_directories(${FFMPEG_INCLUDE_DIRS})

file(GLOB_RECURSE other_cpp CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE rife_cpp src/rife/*.cpp)
file(GLOB_RECURSE realcugan_cpp src/realcugan/*.cpp)
add_executable(HyVideo HyVideo.cpp ${other_cpp})
# add_executable(Test Test.cpp)
# add_executable(Mid_Rife Mid_Rife.cpp ${rife_cpp})

target_link_libraries(HyVideo ${FFMPEG_LIBRARIES} ncnn)
# target_link_libraries(Test ${FFMPEG_LIBRARIES} ncnn)
# target_sources(Test PRIVATE src/Common.cpp)
# target_link_libraries(Mid_Rife ${FFMPEG_LIBRARIES} ncnn)
# target_sources(Mid_Rife PRIVATE src/Common.cpp)


# 查找所有 .comp 文件
file(GLOB RIFE_COMP_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "include/rife/comps/*.comp")
file(GLOB REALCUGAN_COMP_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "include/realcugan/comps/*.comp")
list(APPEND COMP_FILES ${RIFE_COMP_FILES} ${REALCUGAN_COMP_FILES})
# 收集生成的头文件
set(GENERATED_HEADERS)
# 遍历每个 .comp 文件
foreach(comp_file IN LISTS COMP_FILES)
    # 输入文件绝对路径
    set(SHADER_SRC_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${comp_file})
    # 输出头文件
    get_filename_component(fname_we ${comp_file} NAME_WE)
    get_filename_component(fname_dir ${comp_file} DIRECTORY)
    set(SHADER_COMP_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/${fname_dir}/${fname_we}.comp.hex.h)
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
        COMMENT "Converting ${comp_file} to ${fname_we}.comp.hex.h"
        VERBATIM
    )
endforeach()
add_custom_target(GenerateShaderHeaders
    DEPENDS ${GENERATED_HEADERS}
)
# 确保在编译前生成shader头文件
add_dependencies(HyVideo GenerateShaderHeaders)

# 构造时将configs复制到目录下
add_custom_command(
    TARGET HyVideo  # 关联到 HyVideo 目标
    PRE_BUILD     # 执行时机：编译目标前
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${project_dir}/configs  # 源目录
            ${project_dir}/bin/configs  # 目标目录
    COMMENT "Copying configs directory to bin/configs"  # 构建时打印提示
)