# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GETBIBLESWORD_PROJECT_VERSION LIMIT_COUNT 1)
if(NOT GETBIBLESWORD_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain one semantic version")
endif()

project(
  getBibleSword
  VERSION "${GETBIBLESWORD_PROJECT_VERSION}"
  DESCRIPTION "Lossless deterministic NDJSON extraction for CrossWire SWORD modules"
  HOMEPAGE_URL "https://github.com/getbible/getbiblesword"
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

option(GETBIBLESWORD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GETBIBLESWORD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(
  GETBIBLESWORD_ENABLE_CONFORMANCE_TESTS
  "Build and run the complete CrossWire driver conformance corpus"
  OFF
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GetBibleSwordWarnings)

find_package(PkgConfig 1.8 REQUIRED)
pkg_check_modules(SWORD REQUIRED IMPORTED_TARGET "sword>=1.9.0")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/getbiblesword/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/getbiblesword/version.hpp"
  @ONLY
)

add_library(getbiblesword_core STATIC
  src/annotation.cpp
  src/base64.cpp
  src/byte_value.cpp
  src/ndjson_writer.cpp
  src/sha256.cpp
)
add_library(getbiblesword::core ALIAS getbiblesword_core)
target_compile_features(getbiblesword_core PUBLIC cxx_std_20)
target_include_directories(getbiblesword_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
getbiblesword_set_warnings(getbiblesword_core)
getbiblesword_enable_sanitizers(getbiblesword_core)

add_library(getbiblesword_sword STATIC
  src/artifact.cpp
  src/sword_extractor.cpp
)
add_library(getbiblesword::sword ALIAS getbiblesword_sword)
target_compile_features(getbiblesword_sword PUBLIC cxx_std_20)
target_include_directories(getbiblesword_sword
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_include_directories(getbiblesword_sword SYSTEM PRIVATE ${SWORD_INCLUDE_DIRS})
target_link_libraries(getbiblesword_sword PUBLIC getbiblesword::core PkgConfig::SWORD)
getbiblesword_set_warnings(getbiblesword_sword)
getbiblesword_enable_sanitizers(getbiblesword_sword)

add_executable(getbiblesword src/main.cpp)
target_compile_features(getbiblesword PRIVATE cxx_std_20)
target_link_libraries(getbiblesword PRIVATE getbiblesword::sword)
getbiblesword_set_warnings(getbiblesword)
getbiblesword_enable_sanitizers(getbiblesword)

install(TARGETS getbiblesword RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  PROGRAMS tools/getbiblesword_v1.py
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME getbiblesword-v1
)
install(FILES LICENSE README.md SECURITY.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs/ schema/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(
  FILES
    tests/corpus/manifest.json
    tests/fixtures/dictionary.imp
    tests/fixtures/general-book.imp
    tests/fixtures/modules.conf
    tests/fixtures/plate.txt
    tests/fixtures/verse.imp
    tests/fixtures/verse.osis.xml
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/conformance"
)

if(BUILD_TESTING)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  add_executable(getbiblesword_tests tests/test_main.cpp)
  target_compile_features(getbiblesword_tests PRIVATE cxx_std_20)
  target_link_libraries(getbiblesword_tests PRIVATE getbiblesword::core)
  getbiblesword_set_warnings(getbiblesword_tests)
  getbiblesword_enable_sanitizers(getbiblesword_tests)
  add_test(NAME getbiblesword.unit COMMAND getbiblesword_tests)
  add_test(NAME getbiblesword.contract COMMAND getbiblesword contract)
  set_tests_properties(getbiblesword.contract PROPERTIES
    PASS_REGULAR_EXPRESSION "getbiblesword.ndjson/v1"
  )
  add_test(
    NAME getbiblesword.validator-unit
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validator_test.py"
  )

  if(GETBIBLESWORD_ENABLE_CONFORMANCE_TESTS)
    pkg_get_variable(SWORD_PREFIX sword prefix)
    find_program(SWORD_IMP2LD imp2ld HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_IMP2GBS imp2gbs HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_IMP2VS imp2vs HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_OSIS2MOD osis2mod HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    add_executable(getbiblesword_corpus_writer tests/corpus_writer.cpp)
    target_compile_features(getbiblesword_corpus_writer PRIVATE cxx_std_20)
    target_include_directories(getbiblesword_corpus_writer SYSTEM PRIVATE ${SWORD_INCLUDE_DIRS})
    target_link_libraries(getbiblesword_corpus_writer PRIVATE PkgConfig::SWORD)
    getbiblesword_set_warnings(getbiblesword_corpus_writer)
    getbiblesword_enable_sanitizers(getbiblesword_corpus_writer)
    add_test(
      NAME getbiblesword.sword-integration
      COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh"
        "$<TARGET_FILE:getbiblesword>"
        "${SWORD_IMP2LD}"
        "${SWORD_IMP2GBS}"
        "${SWORD_IMP2VS}"
        "${SWORD_OSIS2MOD}"
        "$<TARGET_FILE:getbiblesword_corpus_writer>"
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/getbiblesword_v1.py"
    )
    set_tests_properties(getbiblesword.sword-integration PROPERTIES TIMEOUT 900)
  endif()
endif()
