cmake_minimum_required(VERSION 3.13)
project(runtests)
set(SOURCE_FILES main.c test_fcgi_parser.c test_fcgi_params_parser.c test_parse_script_filename.c asserts.c ../fcgi_params_parser.c ../fcgi_parser.c )
add_executable(runtests ${SOURCE_FILES})
target_link_libraries(runtests)

include(CTest)
set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")
add_test(NAME FcgiParserTest COMMAND runtests test_fcgi_parser)
add_test(NAME FcgiParamsParserTest COMMAND runtests test_fcgi_params_parser)
add_test(NAME ParseScriptFilenameTest COMMAND runtests test_parse_script_filename)
add_test(NAME FcgiParserValgrindTest COMMAND valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all ./runtests test_fcgi_parser)
add_test(NAME FcgiParamsParserValgrindTest COMMAND valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all ./runtests test_fcgi_params_parser)
add_test(NAME ParseScriptFilenameValgrindTest COMMAND valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all ./runtests test_parse_script_filename)
