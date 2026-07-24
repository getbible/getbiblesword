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
  LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(GETBIBLESWORD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GETBIBLESWORD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(GETBIBLESWORD_BUILD_SHARED_LIBRARY "Build the stable libgetbiblesword C ABI" ON)
option(
  GETBIBLESWORD_ENABLE_CONFORMANCE_TESTS
  "Build and run the complete CrossWire driver conformance corpus"
  OFF
)
set(
  GETBIBLESWORD_SWORD_PROVIDER
  "BUNDLED"
  CACHE STRING
  "SWORD engine provider: BUNDLED requires the pinned static 1.9.0 build; SYSTEM uses pkg-config"
)
set_property(
  CACHE GETBIBLESWORD_SWORD_PROVIDER
  PROPERTY STRINGS BUNDLED SYSTEM
)
if(GETBIBLESWORD_ENABLE_CONFORMANCE_TESTS
   AND NOT GETBIBLESWORD_BUILD_SHARED_LIBRARY)
  message(FATAL_ERROR
    "The conformance suite verifies CLI/C ABI parity and therefore requires "
    "GETBIBLESWORD_BUILD_SHARED_LIBRARY=ON."
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GetBibleSwordWarnings)

find_package(PkgConfig 1.8 REQUIRED)
if(GETBIBLESWORD_SWORD_PROVIDER STREQUAL "BUNDLED")
  pkg_check_modules(SWORD REQUIRED "sword=1.9.0")
  find_file(
    GETBIBLESWORD_SWORD_STATIC_ARCHIVE
    NAMES libsword.a
    PATHS ${SWORD_STATIC_LIBRARY_DIRS} ${SWORD_LIBRARY_DIRS}
    NO_DEFAULT_PATH
  )
  if(NOT GETBIBLESWORD_SWORD_STATIC_ARCHIVE)
    message(FATAL_ERROR
      "GETBIBLESWORD_SWORD_PROVIDER=BUNDLED requires the pinned PIC static "
      "SWORD 1.9.0 archive. Run scripts/build-sword.sh and add its "
      "lib/pkgconfig directory to PKG_CONFIG_PATH, or explicitly configure "
      "GETBIBLESWORD_SWORD_PROVIDER=SYSTEM."
    )
  endif()

  set(GETBIBLESWORD_SWORD_STATIC_DEPENDENCIES ${SWORD_STATIC_LIBRARIES})
  list(REMOVE_ITEM GETBIBLESWORD_SWORD_STATIC_DEPENDENCIES sword)
  add_library(getbiblesword_sword_engine INTERFACE)
  target_include_directories(
    getbiblesword_sword_engine
    SYSTEM INTERFACE
      ${SWORD_INCLUDE_DIRS}
  )
  target_compile_options(
    getbiblesword_sword_engine
    INTERFACE
      ${SWORD_CFLAGS_OTHER}
  )
  target_link_directories(
    getbiblesword_sword_engine
    INTERFACE
      ${SWORD_STATIC_LIBRARY_DIRS}
  )
  target_link_libraries(
    getbiblesword_sword_engine
    INTERFACE
      "${GETBIBLESWORD_SWORD_STATIC_ARCHIVE}"
      ${GETBIBLESWORD_SWORD_STATIC_DEPENDENCIES}
  )
  target_link_options(
    getbiblesword_sword_engine
    INTERFACE
      ${SWORD_STATIC_LDFLAGS_OTHER}
  )
elseif(GETBIBLESWORD_SWORD_PROVIDER STREQUAL "SYSTEM")
  pkg_check_modules(SWORD REQUIRED IMPORTED_TARGET "sword>=1.9.0")
  add_library(getbiblesword_sword_engine INTERFACE)
  target_link_libraries(getbiblesword_sword_engine INTERFACE PkgConfig::SWORD)
else()
  message(FATAL_ERROR
    "GETBIBLESWORD_SWORD_PROVIDER must be BUNDLED or SYSTEM, got: "
    "${GETBIBLESWORD_SWORD_PROVIDER}"
  )
endif()
add_library(getbiblesword::sword-engine ALIAS getbiblesword_sword_engine)

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
set_target_properties(getbiblesword_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
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
set_target_properties(getbiblesword_sword PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(getbiblesword_sword
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_include_directories(getbiblesword_sword SYSTEM PRIVATE ${SWORD_INCLUDE_DIRS})
target_link_libraries(
  getbiblesword_sword
  PUBLIC getbiblesword::core
  PRIVATE getbiblesword::sword-engine
)
getbiblesword_set_warnings(getbiblesword_sword)
getbiblesword_enable_sanitizers(getbiblesword_sword)

add_executable(getbiblesword src/main.cpp)
target_compile_features(getbiblesword PRIVATE cxx_std_20)
target_link_libraries(
  getbiblesword
  PRIVATE
    getbiblesword::sword
    getbiblesword::sword-engine
)
getbiblesword_set_warnings(getbiblesword)
getbiblesword_enable_sanitizers(getbiblesword)

install(TARGETS getbiblesword RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(GETBIBLESWORD_BUILD_SHARED_LIBRARY)
  add_library(getbiblesword_shared SHARED
    src/c_api.cpp
    src/callback_streambuf.cpp
  )
  add_library(getBibleSword::getBibleSword ALIAS getbiblesword_shared)
  target_compile_features(getbiblesword_shared PRIVATE cxx_std_20)
  target_compile_definitions(
    getbiblesword_shared
    PRIVATE GETBIBLESWORD_BUILDING_SHARED_LIBRARY
  )
  target_include_directories(getbiblesword_shared
    PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/generated"
  )
  target_link_libraries(getbiblesword_shared PRIVATE getbiblesword::sword)
  set_target_properties(getbiblesword_shared PROPERTIES
    OUTPUT_NAME getbiblesword
    EXPORT_NAME getBibleSword
    VERSION "${PROJECT_VERSION}"
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(getbiblesword_shared PRIVATE
      "LINKER:--exclude-libs,ALL"
      "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/getbiblesword.map"
    )
  endif()
  getbiblesword_set_warnings(getbiblesword_shared)
  getbiblesword_enable_sanitizers(getbiblesword_shared)

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/getbiblesword.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/getbiblesword.pc"
    @ONLY
  )
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/getBibleSwordConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/getBibleSwordConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/getBibleSword"
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/getBibleSwordConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
  )

  install(
    TARGETS getbiblesword_shared
    EXPORT getBibleSwordTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )
  install(
    FILES
      include/getbiblesword/c_api.h
      include/getbiblesword/export.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/getbiblesword"
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/getbiblesword.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
  )
  install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/getBibleSwordConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/getBibleSwordConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/getBibleSword"
  )
  install(
    EXPORT getBibleSwordTargets
    FILE getBibleSwordTargets.cmake
    NAMESPACE getBibleSword::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/getBibleSword"
  )
endif()

install(
  PROGRAMS tools/getbiblesword_v1.py
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME getbiblesword-v1
)
install(
  FILES AGENTS.md LICENSE README.md SECURITY.md llms.txt
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(DIRECTORY docs schema DESTINATION "${CMAKE_INSTALL_DOCDIR}")
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

  if(GETBIBLESWORD_BUILD_SHARED_LIBRARY)
    find_package(Threads REQUIRED)

    add_executable(getbiblesword_c_api_tests tests/test_c_api.c)
    target_compile_features(getbiblesword_c_api_tests PRIVATE c_std_11)
    target_link_libraries(
      getbiblesword_c_api_tests
      PRIVATE getBibleSword::getBibleSword Threads::Threads
    )
    getbiblesword_set_warnings(getbiblesword_c_api_tests)
    getbiblesword_enable_sanitizers(getbiblesword_c_api_tests)
    add_test(
      NAME getbiblesword.c-api
      COMMAND getbiblesword_c_api_tests
    )

    add_executable(
      getbiblesword_c_api_exception_tests
      tests/test_c_api_exceptions.cpp
    )
    target_compile_features(
      getbiblesword_c_api_exception_tests
      PRIVATE cxx_std_20
    )
    target_link_libraries(
      getbiblesword_c_api_exception_tests
      PRIVATE getBibleSword::getBibleSword
    )
    getbiblesword_set_warnings(getbiblesword_c_api_exception_tests)
    getbiblesword_enable_sanitizers(getbiblesword_c_api_exception_tests)
    add_test(
      NAME getbiblesword.c-api-exceptions
      COMMAND getbiblesword_c_api_exception_tests
    )

    add_executable(getbiblesword_c_api_driver tests/c_api_driver.c)
    target_compile_features(getbiblesword_c_api_driver PRIVATE c_std_11)
    target_link_libraries(
      getbiblesword_c_api_driver
      PRIVATE getBibleSword::getBibleSword Threads::Threads
    )
    getbiblesword_set_warnings(getbiblesword_c_api_driver)
    getbiblesword_enable_sanitizers(getbiblesword_c_api_driver)

    add_test(
      NAME getbiblesword.c-api-parity
      COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/c_api_parity.sh"
        "$<TARGET_FILE:getbiblesword>"
        "$<TARGET_FILE:getbiblesword_c_api_driver>"
    )
    add_test(
      NAME getbiblesword.abi-symbols
      COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-abi.sh"
        "$<TARGET_FILE:getbiblesword_shared>"
        "$<TARGET_FILE:getbiblesword>"
        "${GETBIBLESWORD_SWORD_PROVIDER}"
    )
  endif()

  if(GETBIBLESWORD_ENABLE_CONFORMANCE_TESTS)
    pkg_get_variable(SWORD_PREFIX sword prefix)
    find_program(SWORD_IMP2LD imp2ld HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_IMP2GBS imp2gbs HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_IMP2VS imp2vs HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    find_program(SWORD_OSIS2MOD osis2mod HINTS "${SWORD_PREFIX}/bin" REQUIRED)
    add_executable(getbiblesword_corpus_writer tests/corpus_writer.cpp)
    target_compile_features(getbiblesword_corpus_writer PRIVATE cxx_std_20)
    target_include_directories(getbiblesword_corpus_writer SYSTEM PRIVATE ${SWORD_INCLUDE_DIRS})
    target_link_libraries(
      getbiblesword_corpus_writer
      PRIVATE getbiblesword::sword-engine
    )
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
        "$<TARGET_FILE:getbiblesword_c_api_driver>"
    )
    set_tests_properties(getbiblesword.sword-integration PROPERTIES TIMEOUT 900)
  endif()
endif()
