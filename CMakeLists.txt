cmake_minimum_required(VERSION 3.13)
project(std-fpm)
set(SOURCE_FILES main.c fcgi_params_parser.c fcgi_parser.c fcgitypes.c log.c buf.c process_pool.c fcgi_process.c)
add_executable(std-fpm ${SOURCE_FILES})
