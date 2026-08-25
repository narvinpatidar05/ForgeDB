cmake_minimum_required(VERSION 3.10)
project(mydb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(mydb
    src/main.cpp
    src/row_codec.cpp
    src/page.cpp
    src/page_manager.cpp
    src/buffer_pool.cpp
    src/storage.cpp
)
