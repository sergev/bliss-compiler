cmake_minimum_required(VERSION 3.13)

project(blissc VERSION 0.5.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

find_package(LLVM 17.0 REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

include_directories(${blissc_BINARY_DIR}/include ${CMAKE_SOURCE_DIR}/include)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wpedantic -Werror -Wshadow)
endif()

add_library(libdriver STATIC lib/driver/driver.c)
add_library(libfrontend STATIC
  lib/frontend/charfuncs.c
  lib/frontend/declarations.c
  lib/frontend/execfuncs.c
  lib/frontend/expr_control.c
  lib/frontend/expression.c
  lib/frontend/lexeme.c
  lib/frontend/lexer.c
  lib/frontend/libgen.c
  lib/frontend/listings.c
  lib/frontend/macros.c
  lib/frontend/nametable.c
  lib/frontend/parser.c
  lib/frontend/scanner.c
  lib/frontend/structures.c
  lib/frontend/switches.c
  lib/frontend/symbols.c)
add_library(libllvmgen STATIC
  lib/llvmgen/llvm_ctrlexpgen.c
  lib/llvmgen/llvm_execfuncgen.c
  lib/llvmgen/llvm_expgen.c
  lib/llvmgen/llvm_gencode.c
  lib/llvmgen/llvm_machines.c
  lib/llvmgen/llvm_opexpgen.c
  lib/llvmgen/llvm_symgen.c
  lib/llvmgen/llvm_builtins_x86.c
  lib/llvmgen/llvm_helper.cpp)
target_include_directories(libllvmgen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_options(libllvmgen
  PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas ${LLVM_CXXFLAGS}>
  PRIVATE $<$<COMPILE_LANGUAGE:C>:${LLVM_CFLAGS}>)
add_library(libsupport STATIC
  lib/support/fileio.c
  lib/support/logging.c
  lib/support/statcodes.c
  lib/support/strings.c
  lib/support/utils.c)
add_executable(blissc driver/blissc.c)
target_link_libraries(blissc
  libdriver
  libfrontend
  libsupport
  libllvmgen
  ${LLVM_LIBRARIES}
  ${LLVM_SYSTEM_LIBS})
target_link_options(blissc PUBLIC ${LLVM_LDFLAGS})
configure_file(${CMAKE_SOURCE_DIR}/cmake/config.h.in ${blissc_BINARY_DIR}/include/blissc/config.h @ONLY)

if (${Python3_FOUND})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_custom_target(testharness ALL
        COMMAND $<TARGET_FILE:blissc> --library -o ${CMAKE_BINARY_DIR}/tests/testharness.lib ${CMAKE_SOURCE_DIR}/tests/testharness.req
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        DEPENDS blissc)

# Real-world definition files from MX, compiled as libraries in dependency order
set(MX_LIBS fields md5def md5 status qp charconv accounting ipc router x509 ssl mx_old_lcldefs)
set(MX_LIB_FILES)
set(MX_PREV_LIB)
foreach(lib ${MX_LIBS})
  string(TOUPPER ${lib} LIBNAME)
  set(out ${CMAKE_BINARY_DIR}/tests/${LIBNAME}.lib)
  add_custom_command(OUTPUT ${out}
          COMMAND $<TARGET_FILE:blissc> --library -I ${CMAKE_BINARY_DIR}/tests -o ${out} ${CMAKE_SOURCE_DIR}/tests/mx/${lib}.r32
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
          DEPENDS blissc ${CMAKE_SOURCE_DIR}/tests/mx/${lib}.r32 ${MX_PREV_LIB})
  list(APPEND MX_LIB_FILES ${out})
  set(MX_PREV_LIB ${out})
endforeach()
add_custom_target(mxlibs ALL DEPENDS ${MX_LIB_FILES})

add_custom_target(check
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/runtests.py" --blissc=$<TARGET_FILE:blissc> --cc="${CMAKE_C_COMPILER}" "${CMAKE_SOURCE_DIR}/tests"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        DEPENDS testharness mxlibs blissc)

# One CTest test per .bli file
enable_testing()
file(GLOB_RECURSE BLISS_TESTS RELATIVE ${CMAKE_SOURCE_DIR}/tests CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.bli)
foreach(test ${BLISS_TESTS})
  string(REGEX REPLACE "\\.bli$" "" name ${test})
  add_test(NAME ${name}
           COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/runtests.py" --blissc=$<TARGET_FILE:blissc> --cc=${CMAKE_C_COMPILER} "${CMAKE_SOURCE_DIR}/tests/${test}"
           WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endforeach()
else()
  message(No Python3 - omitting compiler tests)
endif()
