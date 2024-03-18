cmake_minimum_required(VERSION 2.8)
set(CMAKE_BUILD_TYPE Debug)
option(DEBUG_LOG "Produce debug log" OFF)

project(std-fpm C)
find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

set(SOURCE_FILES 
   src/main.c
   src/fcgi_params_parser.c
   src/fcgi_parser.c
   src/fcgitypes.c
   src/log.c
   src/buf.c
   src/process_pool.c
   src/fcgi_process.c
   src/fd_ctx.c
   src/fdutils.c
   src/debug_utils.c
   src/fcgi_writer.c
   src/config.c)

if(DEBUG_LOG)
   add_compile_options(-DDEBUG_LOG)
endif(DEBUG_LOG)

add_compile_options(-std=gnu99 -Wpointer-sign)
add_executable(std-fpm ${SOURCE_FILES})

add_definitions(${GLIB_CFLAGS_OTHER})
target_link_libraries(std-fpm ${GLIB_LIBRARIES} -levent)

set(CMAKE_INSTALL_PREFIX "")
install(TARGETS std-fpm DESTINATION usr/sbin/)
install(FILES conf/std-fpm.conf DESTINATION etc/)
install(FILES systemd/system/std-fpm.service DESTINATION etc/systemd/system)
install(DIRECTORY DESTINATION run/std-fpm/)
install(DIRECTORY DESTINATION run/std-fpm/pool/)
