add_executable(pcscript_tests test_main.cpp)
target_link_libraries(pcscript_tests PRIVATE pcscript_core)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(pcscript_tests PRIVATE Threads::Threads)
endif()

target_compile_definitions(pcscript_tests PRIVATE PCS_TEST_DIR="${CMAKE_SOURCE_DIR}/tests" PCS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
add_test(NAME pcscript_tests COMMAND pcscript_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
