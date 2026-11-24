cmake_minimum_required(VERSION 3.20)
project(DriverSource VERSION 0.1.5 LANGUAGES C RC)

if(NOT WIN32)
    message(FATAL_ERROR "Use a Windows compiler or a Windows cross-compilation toolchain.")
endif()

add_executable(DriverSource WIN32 native/main.c native/driver_backup.c native/device_db.c native/hardware_database.c native/wua_callbacks.c native/app.rc)
set_target_properties(DriverSource PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES)
target_include_directories(DriverSource PRIVATE native)
target_link_libraries(DriverSource PRIVATE setupapi cfgmgr32 ole32 oleaut32 comctl32
    shell32 shlwapi advapi32 user32 gdi32 uuid winhttp)

if(MSVC)
    set_property(TARGET DriverSource PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(DriverSource PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(DriverSource PRIVATE "$<$<COMPILE_LANGUAGE:C>:/utf-8;/W4;/WX>")
    target_link_options(DriverSource PRIVATE /MANIFEST:NO)
else()
    target_compile_options(DriverSource PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-finput-charset=UTF-8>")
    target_link_options(DriverSource PRIVATE -municode)
endif()

add_custom_target(DriverDatabase ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/database" "$<TARGET_FILE_DIR:DriverSource>/database")

add_dependencies(DriverSource DriverDatabase)
