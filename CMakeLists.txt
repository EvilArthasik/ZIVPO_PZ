cmake_minimum_required(VERSION 3.21)
project(TraySampleApp LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TraySampleApp uses Win32 service/RPC APIs and must be built on Windows")
endif()


set(TRAYAPP_MIDL_SEARCH_DIRS)

if(DEFINED ENV{WindowsSDK_ExecutablePath})
    file(TO_CMAKE_PATH "$ENV{WindowsSDK_ExecutablePath}" TRAYAPP_WINDOWS_SDK_EXECUTABLE_PATH)
    list(APPEND TRAYAPP_MIDL_SEARCH_DIRS "${TRAYAPP_WINDOWS_SDK_EXECUTABLE_PATH}")
endif()

set(TRAYAPP_WINDOWS_SDK_ROOTS)

if(DEFINED ENV{WindowsSdkDir})
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" TRAYAPP_WINDOWS_SDK_DIR)
    list(APPEND TRAYAPP_WINDOWS_SDK_ROOTS "${TRAYAPP_WINDOWS_SDK_DIR}")
endif()

if(DEFINED ENV{ProgramFiles\(x86\)})
    file(TO_CMAKE_PATH "$ENV{ProgramFiles\(x86\)}" TRAYAPP_PROGRAM_FILES_X86)
    list(APPEND TRAYAPP_WINDOWS_SDK_ROOTS "${TRAYAPP_PROGRAM_FILES_X86}/Windows Kits/10")
endif()

list(APPEND TRAYAPP_WINDOWS_SDK_ROOTS "C:/Program Files (x86)/Windows Kits/10")
list(REMOVE_DUPLICATES TRAYAPP_WINDOWS_SDK_ROOTS)

set(TRAYAPP_MIDL_HOST_ARCHES)

foreach(TRAYAPP_ARCH_VALUE IN ITEMS
        "${CMAKE_HOST_SYSTEM_PROCESSOR}"
        "$ENV{PROCESSOR_ARCHITECTURE}"
        "$ENV{PROCESSOR_ARCHITEW6432}"
        "${CMAKE_VS_PLATFORM_NAME}"
        "arm64"
        "x64"
        "x86")
    if(TRAYAPP_ARCH_VALUE)
        string(TOLOWER "${TRAYAPP_ARCH_VALUE}" TRAYAPP_ARCH_VALUE_LOWER)

        if(TRAYAPP_ARCH_VALUE_LOWER STREQUAL "amd64")
            set(TRAYAPP_ARCH_VALUE_LOWER "x64")
        elseif(TRAYAPP_ARCH_VALUE_LOWER STREQUAL "x86_64")
            set(TRAYAPP_ARCH_VALUE_LOWER "x64")
        elseif(TRAYAPP_ARCH_VALUE_LOWER STREQUAL "i386")
            set(TRAYAPP_ARCH_VALUE_LOWER "x86")
        endif()

        list(APPEND TRAYAPP_MIDL_HOST_ARCHES "${TRAYAPP_ARCH_VALUE_LOWER}")
    endif()
endforeach()

list(REMOVE_DUPLICATES TRAYAPP_MIDL_HOST_ARCHES)

foreach(TRAYAPP_WINDOWS_SDK_ROOT IN LISTS TRAYAPP_WINDOWS_SDK_ROOTS)
    set(TRAYAPP_WINDOWS_SDK_BIN_DIRS)

    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        list(APPEND
            TRAYAPP_WINDOWS_SDK_BIN_DIRS
            "${TRAYAPP_WINDOWS_SDK_ROOT}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
        )
    endif()

    file(GLOB TRAYAPP_DISCOVERED_WINDOWS_SDK_BIN_DIRS
        LIST_DIRECTORIES true
        "${TRAYAPP_WINDOWS_SDK_ROOT}/bin/10.*"
    )

    if(TRAYAPP_DISCOVERED_WINDOWS_SDK_BIN_DIRS)
        list(SORT TRAYAPP_DISCOVERED_WINDOWS_SDK_BIN_DIRS COMPARE NATURAL ORDER DESCENDING)
        list(APPEND TRAYAPP_WINDOWS_SDK_BIN_DIRS ${TRAYAPP_DISCOVERED_WINDOWS_SDK_BIN_DIRS})
    endif()

    foreach(TRAYAPP_WINDOWS_SDK_BIN_DIR IN LISTS TRAYAPP_WINDOWS_SDK_BIN_DIRS)
        foreach(TRAYAPP_MIDL_HOST_ARCH IN LISTS TRAYAPP_MIDL_HOST_ARCHES)
            list(APPEND TRAYAPP_MIDL_SEARCH_DIRS "${TRAYAPP_WINDOWS_SDK_BIN_DIR}/${TRAYAPP_MIDL_HOST_ARCH}")
        endforeach()

        list(APPEND TRAYAPP_MIDL_SEARCH_DIRS "${TRAYAPP_WINDOWS_SDK_BIN_DIR}")
    endforeach()
endforeach()

list(REMOVE_DUPLICATES TRAYAPP_MIDL_SEARCH_DIRS)

find_program(MIDL_EXECUTABLE
    NAMES midl.exe midl
    HINTS ${TRAYAPP_MIDL_SEARCH_DIRS}
    REQUIRED
)

set(TRAYAPP_TARGET_ARCH "")

if(CMAKE_VS_PLATFORM_NAME)
    string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" TRAYAPP_TARGET_ARCH)
elseif(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" TRAYAPP_TARGET_ARCH)
elseif(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TRAYAPP_TARGET_ARCH)
endif()

if(TRAYAPP_TARGET_ARCH STREQUAL "arm64")
    set(TRAYAPP_MIDL_ENV arm64)
elseif(TRAYAPP_TARGET_ARCH STREQUAL "x64" OR
       TRAYAPP_TARGET_ARCH STREQUAL "amd64" OR
       TRAYAPP_TARGET_ARCH STREQUAL "x86_64")
    set(TRAYAPP_MIDL_ENV amd64)
elseif(TRAYAPP_TARGET_ARCH STREQUAL "win32" OR
       TRAYAPP_TARGET_ARCH STREQUAL "x86" OR
       TRAYAPP_TARGET_ARCH STREQUAL "i386")
    set(TRAYAPP_MIDL_ENV win32)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TRAYAPP_MIDL_ENV amd64)
else()
    set(TRAYAPP_MIDL_ENV win32)
endif()

set(TRAYAPP_RPC_IDL ${CMAKE_CURRENT_SOURCE_DIR}/src/rpc/tray_control.idl)
set(TRAYAPP_RPC_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/rpc)
set(TRAYAPP_RPC_HEADER ${TRAYAPP_RPC_GENERATED_DIR}/tray_control.h)
set(TRAYAPP_RPC_CLIENT_STUB ${TRAYAPP_RPC_GENERATED_DIR}/tray_control_c.c)
set(TRAYAPP_RPC_SERVER_STUB ${TRAYAPP_RPC_GENERATED_DIR}/tray_control_s.c)

add_custom_command(
    OUTPUT
        ${TRAYAPP_RPC_HEADER}
        ${TRAYAPP_RPC_CLIENT_STUB}
        ${TRAYAPP_RPC_SERVER_STUB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TRAYAPP_RPC_GENERATED_DIR}
    COMMAND ${MIDL_EXECUTABLE}
        /nologo
        /char signed
        /env ${TRAYAPP_MIDL_ENV}
        /out ${TRAYAPP_RPC_GENERATED_DIR}
        /h tray_control.h
        /client stub
        /server stub
        ${TRAYAPP_RPC_IDL}
    DEPENDS ${TRAYAPP_RPC_IDL}
    VERBATIM
)

set_source_files_properties(
    ${TRAYAPP_RPC_HEADER}
    ${TRAYAPP_RPC_CLIENT_STUB}
    ${TRAYAPP_RPC_SERVER_STUB}
    PROPERTIES
        GENERATED TRUE
        LANGUAGE C
)

add_custom_target(TrayRpcGenerated
    DEPENDS
        ${TRAYAPP_RPC_HEADER}
        ${TRAYAPP_RPC_CLIENT_STUB}
        ${TRAYAPP_RPC_SERVER_STUB}
)

add_library(TrayRpcClientStub STATIC ${TRAYAPP_RPC_CLIENT_STUB})
add_library(TrayRpcServerStub STATIC ${TRAYAPP_RPC_SERVER_STUB})

add_dependencies(TrayRpcClientStub TrayRpcGenerated)
add_dependencies(TrayRpcServerStub TrayRpcGenerated)

target_include_directories(TrayRpcClientStub PRIVATE ${TRAYAPP_RPC_GENERATED_DIR})
target_include_directories(TrayRpcServerStub PRIVATE ${TRAYAPP_RPC_GENERATED_DIR})

add_executable(TraySampleApp WIN32
    main.cpp
    src/gui/rpc_client.cpp
    src/gui/service_guard.cpp
)

add_executable(TraySampleService WIN32
    src/service/service_main.cpp
    src/service/auth_license_manager.cpp
    src/service/antivirus_database_store.cpp
    src/service/antivirus_engine.cpp
    src/service/antivirus_manager.cpp
)

add_dependencies(TraySampleApp TrayRpcClientStub)
add_dependencies(TraySampleService TrayRpcServerStub)

target_include_directories(TraySampleApp PRIVATE
    src
    ${TRAYAPP_RPC_GENERATED_DIR}
)

target_include_directories(TraySampleService PRIVATE
    src
    ${TRAYAPP_RPC_GENERATED_DIR}
)

target_compile_features(TraySampleApp PRIVATE cxx_std_17)
target_compile_features(TraySampleService PRIVATE cxx_std_17)
target_compile_definitions(TraySampleApp PRIVATE UNICODE _UNICODE)
target_compile_definitions(TraySampleService PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(TraySampleApp PRIVATE /utf-8 /W4 /permissive-)
    target_compile_options(TraySampleService PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(TraySampleApp PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(TraySampleService PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(TraySampleApp PRIVATE TrayRpcClientStub shell32 advapi32 rpcrt4 user32 ole32)
target_link_libraries(TraySampleService PRIVATE TrayRpcServerStub advapi32 rpcrt4 userenv wtsapi32 winhttp bcrypt crypt32)
