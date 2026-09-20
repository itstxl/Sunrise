cmake_minimum_required(VERSION 3.31)
project(Sunrise C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(XWIN_SDK_VERSION "10.0.26100" CACHE STRING "xwin SDK Version")

option(SUNRISE_BUILD_CLIENT "Build the injected Sunrise client DLL" ON)
option(SUNRISE_BUILD_REALM "Build Sunrise Realm headless services" ON)
option(SUNRISE_BUILD_TESTS "Build Sunrise Realm unit tests" ON)

if(SUNRISE_BUILD_CLIENT)

file(GLOB_RECURSE SUNRISE_SOURCES CONFIGURE_DEPENDS
    "Sunrise/src/*.cpp"
    "Sunrise/src/*.h"
)
file(GLOB_RECURSE VENDOR_SOURCES CONFIGURE_DEPENDS
    "Sunrise/vendor/detours/*.cpp"
    "Sunrise/vendor/detours/*.h"
    "Sunrise/vendor/imgui/*.cpp"
    "Sunrise/vendor/imgui/*.h"
    "Sunrise/vendor/lua/*.c"
    "Sunrise/vendor/lua/*.h"
    "Sunrise/vendor/sqlite/*.c"
    "Sunrise/vendor/sqlite/*.h"
)
# Lua builds as C++ here, matching the MSVC project's CompileAsCpp setting.
file(GLOB_RECURSE LUA_SOURCES CONFIGURE_DEPENDS
    "Sunrise/vendor/lua/*.c"
)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS
    "Sunrise/resources/*.rc"
    "Sunrise/resources/*.h"
    "Sunrise/resources/*.json"
    "Sunrise/resources/*.sql"
)

add_library(steam_api64 SHARED
    ${SUNRISE_SOURCES}
    ${VENDOR_SOURCES}
    ${RESOURCE_FILES}
)

set_property(TARGET steam_api64 PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported)
if(lto_supported)
    set_property(TARGET steam_api64 APPEND PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()
target_include_directories(steam_api64
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/src"
)

# sunrise.rc names its payloads relative to itself, so its directory has to be on the include path.
target_include_directories(steam_api64
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/resources"
)

target_include_directories(steam_api64 SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/vendor/detours"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/vendor/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/vendor/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/vendor/lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sunrise/vendor/sqlite"
)

if(DEFINED XWIN_DIR)
    target_include_directories(steam_api64 SYSTEM PRIVATE
        "${XWIN_DIR}/sdk/Include/${XWIN_SDK_VERSION}/um"
        "${XWIN_DIR}/sdk/Include/${XWIN_SDK_VERSION}/shared"
    )
endif()

target_compile_definitions(steam_api64 PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WINSOCK_DEPRECATED_NO_WARNINGS
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    IMGUI_USER_CONFIG="core/ui/imgui_user_config.h"
)

if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # The vendored Lua interpreter throws, so its error path needs C++ unwinding.
    target_compile_options(steam_api64 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)

    # Clang warnings are more strict the MSVC I guess, this only works on real clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND STRICT_WARNING_FLAGS
            -Wno-unused-const-variable
            -Wno-unused-function
            -Wno-braced-scalar-init
            -Wno-missing-braces
            -Wno-missing-designated-field-initializers
            -Wno-cast-function-type-mismatch
        )
    endif()

    set_source_files_properties(${SUNRISE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${STRICT_WARNING_FLAGS}"
    )
    set_source_files_properties(${VENDOR_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/W3"
    )
endif()

if(CMAKE_HOST_LINUX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(steam_api64 PRIVATE
        $<$<CONFIG:Debug>:/clang:-gdwarf>
        $<$<CONFIG:Debug>:/clang:-g>
    )

    target_link_options(steam_api64 PRIVATE
        $<$<CONFIG:Debug>:/DEBUG:DWARF>
    )
endif()

target_link_libraries(steam_api64 PRIVATE
    kernel32 bcrypt user32 gdi32 dwmapi
    d3dcompiler shell32 ws2_32 synchronization
    windowscodecs
)

set_target_properties(steam_api64 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/x64/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/x64/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/x64/$<CONFIG>"
)

set_source_files_properties(Sunrise/vendor/sqlite/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS "SQLITE_OMIT_LOAD_EXTENSION;SQLITE_THREADSAFE=1"
)
endif()

if(SUNRISE_BUILD_REALM OR SUNRISE_BUILD_TESTS)
    add_library(sunrise_realm_protocol STATIC
        Realm/src/control_protocol.cpp
    )
    set_property(TARGET sunrise_realm_protocol PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(sunrise_realm_protocol
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Realm/include"
    )
    target_compile_definitions(sunrise_realm_protocol PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(sunrise_realm_protocol PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(sunrise_realm_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_library(sunrise_realm_activity STATIC
        Realm/src/activity_host_state.cpp
        Realm/src/ffa_match.cpp
    )
    set_property(TARGET sunrise_realm_activity PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(sunrise_realm_activity PUBLIC sunrise_realm_protocol)
    target_include_directories(sunrise_realm_activity
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Realm/include"
    )
    if(MSVC)
        target_compile_options(sunrise_realm_activity PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(sunrise_realm_activity PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(SUNRISE_BUILD_REALM)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The first Sunrise Realm hosts are Windows executables. "
            "Use linux-to-win-toolchain.cmake, or disable SUNRISE_BUILD_REALM for native tests.")
    endif()

    add_library(sunrise_realm_windows STATIC
        Realm/src/windows/console_runtime.cpp
        Realm/src/windows/control_transport.cpp
        Realm/src/windows/frame_authentication.cpp
    )
    set_property(TARGET sunrise_realm_windows PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(sunrise_realm_windows PUBLIC sunrise_realm_protocol ws2_32 bcrypt)
    target_include_directories(sunrise_realm_windows PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Realm/src")
    target_compile_definitions(sunrise_realm_windows PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
    if(MSVC)
        target_compile_options(sunrise_realm_windows PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(sunrise_realm_windows PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_executable(sunrise-realm Realm/src/realm_main.cpp)
    add_executable(sunrise-activity-host Realm/src/activity_host_main.cpp)
    foreach(target sunrise-realm sunrise-activity-host)
        target_link_libraries(${target} PRIVATE sunrise_realm_windows sunrise_realm_activity)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Realm/src")
        target_compile_definitions(${target} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
        )
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/x64/$<CONFIG>"
        )
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endforeach()
endif()

if(SUNRISE_BUILD_TESTS)
    enable_testing()
    add_executable(sunrise-realm-protocol-tests
        Realm/tests/control_protocol_tests.cpp
    )
    set_property(TARGET sunrise-realm-protocol-tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(sunrise-realm-protocol-tests PRIVATE sunrise_realm_protocol)
    if(MSVC)
        target_compile_options(sunrise-realm-protocol-tests PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(sunrise-realm-protocol-tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME sunrise-realm-protocol COMMAND sunrise-realm-protocol-tests)

    add_executable(sunrise-realm-activity-tests
        Realm/tests/activity_host_state_tests.cpp
        Realm/tests/ffa_match_tests.cpp
    )
    set_property(TARGET sunrise-realm-activity-tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(sunrise-realm-activity-tests PRIVATE sunrise_realm_activity)
    if(MSVC)
        target_compile_options(sunrise-realm-activity-tests PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(sunrise-realm-activity-tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME sunrise-realm-activity COMMAND sunrise-realm-activity-tests)
endif()
