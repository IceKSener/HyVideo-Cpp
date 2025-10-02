# 必须定义：SHADER_SRC, SHADER_COMP_HEADER
if(NOT DEFINED SHADER_SRC OR NOT EXISTS ${SHADER_SRC})
    message(FATAL_ERROR "SHADER_SRC is not defined or not found!")
endif()

if(NOT DEFINED SHADER_COMP_HEADER)
    message(FATAL_ERROR "SHADER_COMP_HEADER is not defined!")
endif()

file(READ ${SHADER_SRC} comp_data)

# 跳过注释，找到 #version
string(FIND "${comp_data}" "#version" version_start)
if(version_start EQUAL -1)
    message(WARNING "No #version found in ${SHADER_SRC}, using full content.")
else()
    string(SUBSTRING "${comp_data}" ${version_start} -1 comp_data)
endif()

# 压缩空格
string(REGEX REPLACE "\n +" "\n" comp_data "${comp_data}")

# 获取文件名（无扩展）
get_filename_component(SHADER_SRC_NAME_WE ${SHADER_SRC} NAME_WE)

# 写临时文件用于 HEX 读取
set(temp_txt ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SRC_NAME_WE}.text2hex.txt)
file(WRITE ${temp_txt} "${comp_data}")

# 读为 HEX
file(READ ${temp_txt} comp_data_hex HEX)

# 转为 0x__, 格式
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," comp_data_hex "${comp_data_hex}")

# 去掉末尾逗号
string(FIND "${comp_data_hex}" "," pos REVERSE)
if(pos GREATER -1)
    string(SUBSTRING "${comp_data_hex}" 0 ${pos} comp_data_hex)
endif()

# 写头文件
file(WRITE ${SHADER_COMP_HEADER} 
    "/* Auto-generated from ${SHADER_SRC} */\n"
    "static const char ${SHADER_SRC_NAME_WE}_comp_data[] = {${comp_data_hex}};\n")

# 清理临时文件
file(REMOVE ${temp_txt})