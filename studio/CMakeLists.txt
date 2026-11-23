# PCScript Studio - the portable core of the IDE (editor model, formatter, autocomplete, accounts, settings, extensions,
# program runner). It contains no GUI code, so it builds and is tested on every platform; the Windows GUI lives in ../editor.
add_library(pcscript_studio_core STATIC
    json.cpp settings.cpp theme.cpp scan.cpp structure.cpp symbols.cpp formatter.cpp completion.cpp document.cpp
    sha256.cpp files.cpp accounts.cpp profile.cpp extensions.cpp runner.cpp)
target_include_directories(pcscript_studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pcscript_studio_core PUBLIC pcscript_core)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(pcscript_studio_core PUBLIC Threads::Threads)
endif()

add_executable(studio_tests tests/main.cpp tests/tests_text.cpp tests/tests_doc.cpp tests/tests_system.cpp)
target_link_libraries(studio_tests PRIVATE pcscript_studio_core)
target_compile_definitions(studio_tests PRIVATE
    PCS_TEST_DIR="${CMAKE_SOURCE_DIR}/tests"
    PCS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    PCS_EXTENSIONS_DIR="${CMAKE_SOURCE_DIR}/extensions")
add_test(NAME studio_tests COMMAND studio_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
