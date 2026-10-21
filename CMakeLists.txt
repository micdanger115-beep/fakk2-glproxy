cmake_minimum_required(VERSION 3.16)
project(fakk2_glproxy VERSION 0.1.0 LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "Nur fuer Windows-Ziele. Unter Linux: -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "F.A.K.K. 2 ist 32-Bit: mit Visual Studio '-A Win32' bzw. der i686-MinGW-Toolchain bauen.")
endif()

option(FAKK2_GLPROXY_TESTS "Testprogramme bauen" ON)

# DLL und Testprogramme landen im selben Ordner, damit Windows dort die Proxy-opengl32.dll findet.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# .def-Datei aus der Exportliste erzeugen: jeder Export <name> zeigt auf den Thunk proxy_<name>.
set(EXPORTS_INC ${CMAKE_CURRENT_SOURCE_DIR}/src/gl_exports.inc)
set(DEF_FILE ${CMAKE_CURRENT_BINARY_DIR}/opengl32.def)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EXPORTS_INC})
file(STRINGS ${EXPORTS_INC} export_lines REGEX "^X\\(")
set(def_content "LIBRARY opengl32\nEXPORTS\n")
set(export_count 0)
foreach(line IN LISTS export_lines)
    string(REGEX REPLACE "^X\\(([A-Za-z0-9_]+),.*$" "\\1" name "${line}")
    string(APPEND def_content "    ${name} = proxy_${name}\n")
    math(EXPR export_count "${export_count} + 1")
endforeach()
file(GENERATE OUTPUT ${DEF_FILE} CONTENT "${def_content}")
message(STATUS "fakk2-glproxy: ${export_count} Exporte")

add_library(opengl32 SHARED
    src/proxy.c
    src/hooks.c
    src/log.c
    ${DEF_FILE}
)
target_compile_definitions(opengl32 PRIVATE
    FAKK2_GLPROXY_VERSION="${PROJECT_VERSION}"
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
)
set_target_properties(opengl32 PROPERTIES
    OUTPUT_NAME opengl32
    PREFIX ""
    C_STANDARD 99
)

if(MSVC)
    # Statische CRT: keine VC-Redistributable auf dem Spielerechner noetig.
    set_property(TARGET opengl32 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(opengl32 PRIVATE /W3)
else()
    target_compile_options(opengl32 PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
    # Ohne @N-Dekoration exportieren, keine MinGW-Laufzeit-DLLs mitschleppen.
    target_link_options(opengl32 PRIVATE -static-libgcc -Wl,--kill-at -Wl,--enable-stdcall-fixup)
    set_target_properties(opengl32 PROPERTIES IMPORT_PREFIX "lib" IMPORT_SUFFIX ".dll.a")
endif()
target_link_libraries(opengl32 PRIVATE user32 kernel32)

install(TARGETS opengl32 RUNTIME DESTINATION .)
install(FILES fakk2_glproxy.ini README.md DESTINATION .)

if(FAKK2_GLPROXY_TESTS)
    # export_check: prueft, dass alle Exporte vorhanden sind und auf Thunks zeigen.
    add_executable(export_check tests/export_check.c)
    target_include_directories(export_check PRIVATE src)
    # smoke_gl: erstellt einen GL-Kontext, zeichnet ein paar Frames und prueft das Log.
    add_executable(smoke_gl tests/smoke_gl.c)
    target_link_libraries(smoke_gl PRIVATE gdi32 user32)
    foreach(t export_check smoke_gl)
        if(MSVC)
            set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            target_link_options(${t} PRIVATE -static-libgcc)
        endif()
    endforeach()
endif()
