cmake_minimum_required(VERSION 3.13)
set(CMAKE_BUILD_TYPE Debug)

project(std-fpm)
find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-3.0)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

set(SOURCE_FILES main.c fcgi_params_parser.c fcgi_parser.c fcgitypes.c log.c buf.c process_pool.c fcgi_process.c fd_ctx.c fdutils.c debug_utils.c debug_utils.h)
add_executable(std-fpm ${SOURCE_FILES})

add_definitions(${GLIB_CFLAGS_OTHER})
target_link_libraries(std-fpm ${GLIB_LIBRARIES})
