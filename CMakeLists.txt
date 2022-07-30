#工具链
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

# CMake 最低版本号要求
cmake_minimum_required (VERSION 3.10.2)

# 项目信息
project (demux)

# 源文件路径
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DIR_SRCS)

# 头文件路径
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR})

# 链接库路径
# LINK_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR})

# 指定生成目标
add_executable(demux ${DIR_SRCS})

# 链接库文件名
# target_link_libraries(test_miss libmiss.so pthread.so libsodium.a libjson-c.so)
