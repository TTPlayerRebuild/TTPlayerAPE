cmake_minimum_required(VERSION 3.24)
project(TtpApe LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_ape requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(cmake/mac.cmake)
include(cmake/version.cmake)
add_library(ttp_ape SHARED src/plugin.cpp src/reader.cpp src/encoder.cpp src/settings.cpp src/settings.rc
  src/ttp_ape.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_ape ttp_ape_version)
target_include_directories(ttp_ape PUBLIC include PRIVATE src "${mac_source}/Source/Shared" "${mac_source}/Source/MACLib")
target_compile_definitions(ttp_ape PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  PLATFORM_WINDOWS_XP _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_ape PRIVATE /W4 /permissive- /EHsc /GR- /Os /Gy /Gw /GF /utf-8 /fp:precise /arch:SSE2)
target_link_libraries(ttp_ape PRIVATE MAC ole32 uuid shlwapi)
target_link_options(ttp_ape PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE)
set_target_properties(ttp_ape PROPERTIES PREFIX "" OUTPUT_NAME "ttp_ape")
add_custom_command(TARGET ttp_ape POST_BUILD COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py" "$<TARGET_FILE:ttp_ape>"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
  --report "$<TARGET_FILE_DIR:ttp_ape>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_ape RUNTIME DESTINATION AddIn COMPONENT Runtime)
