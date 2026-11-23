# PCScript Studio (pcscript-studio.exe): the Windows IDE. Pure Win32 + GDI, no third-party libraries.
add_executable(pcscript-studio WIN32
    main.cpp app_core.cpp app_ui.cpp app_run.cpp app_selftest.cpp
    editor_view.cpp widget.cpp dialogs.cpp win_util.cpp
    studio.rc)
target_include_directories(pcscript-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pcscript-studio PRIVATE pcscript_studio_core
    comctl32 comdlg32 shell32 ole32 uuid uxtheme dwmapi user32 gdi32 advapi32)
target_compile_definitions(pcscript-studio PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
    # studio.manifest is embedded by studio.rc (common controls v6, per-monitor DPI, UTF-8), so the linker must not add its own.
    target_link_options(pcscript-studio PRIVATE /MANIFEST:NO)
elseif(MINGW)
    target_link_options(pcscript-studio PRIVATE -municode -mwindows)
endif()

# Keep everything the IDE looks for next to the executable (the installer lays out the same structure).
add_custom_command(TARGET pcscript-studio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/extensions $<TARGET_FILE_DIR:pcscript-studio>/extensions
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/docs $<TARGET_FILE_DIR:pcscript-studio>/docs
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:pcscript-studio>/examples)

# Headless self-test of the real GUI code (creates the windows, types, runs programs, saves, switches themes ...).
add_test(NAME studio_gui_selftest
    COMMAND pcscript-studio --selftest
            --data-dir ${CMAKE_BINARY_DIR}/selftest-data --docs-dir ${CMAKE_BINARY_DIR}/selftest-docs
            --selftest-log ${CMAKE_BINARY_DIR}/selftest.log)
