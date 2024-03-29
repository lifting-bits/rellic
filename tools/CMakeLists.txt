#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

#
# rellic-decomp
#

set(RELLIC_DECOMP "${PROJECT_NAME}-decomp")

add_executable(${RELLIC_DECOMP}
  "decomp/Decomp.cpp"
)

target_link_libraries(${RELLIC_DECOMP}
  PRIVATE
    "${PROJECT_NAME}_cxx_settings"
    "${PROJECT_NAME}"
    gflags::gflags
)

set(RELLIC_DECOMP "${RELLIC_DECOMP}" PARENT_SCOPE)

#
# rellic-headergen
#

set(RELLIC_HEADERGEN "${PROJECT_NAME}-headergen")

add_executable(${RELLIC_HEADERGEN}
  "headergen/HeaderGen.cpp"
)

target_link_libraries(${RELLIC_HEADERGEN}
  PRIVATE
    "${PROJECT_NAME}_cxx_settings"
    "${PROJECT_NAME}"
    gflags::gflags
)

set(RELLIC_HEADERGEN "${RELLIC_HEADERGEN}" PARENT_SCOPE)

#
# rellic-xref
#

set(RELLIC_XREF "${PROJECT_NAME}-xref")

add_executable(${RELLIC_XREF}
  "xref/DeclPrinter.cpp"
  "xref/StmtPrinter.cpp"
  "xref/TypePrinter.cpp"
  "xref/Xref.cpp"
)

find_path(CPP_HTTPLIB_INCLUDE_DIRS "httplib.h")
target_include_directories(${RELLIC_XREF} PRIVATE ${CPP_HTTPLIB_INCLUDE_DIRS})

target_link_libraries(${RELLIC_XREF}
  PRIVATE
    "${PROJECT_NAME}_cxx_settings"
    "${PROJECT_NAME}"
    gflags::gflags
)

set(RELLIC_XREF "${RELLIC_XREF}" PARENT_SCOPE)

#
# rellic-repl
#

include(FetchContent)
FetchContent_Declare(linenoise
  GIT_REPOSITORY https://github.com/antirez/linenoise.git
  GIT_TAG        97d2850af13c339369093b78abe5265845d78220
)
FetchContent_MakeAvailable(linenoise)

add_library(linenoise STATIC
  "${linenoise_SOURCE_DIR}/linenoise.c"
)
target_include_directories(linenoise
  PUBLIC
    "${linenoise_SOURCE_DIR}"
)

set(RELLIC_REPL "${PROJECT_NAME}-repl")

add_executable(${RELLIC_REPL}
  "repl/Repl.cpp"
)

target_link_libraries(${RELLIC_REPL}
  PRIVATE
    "${PROJECT_NAME}_cxx_settings"
    "${PROJECT_NAME}"
    gflags::gflags
    linenoise
)

set(RELLIC_REPL "${RELLIC_REPL}" PARENT_SCOPE)

#
# rellic-dec2hex
#
set(RELLIC_DEC2HEX "${PROJECT_NAME}-dec2hex")

add_executable(${RELLIC_DEC2HEX}
  "dec2hex/dec2hex.cpp"
)

target_link_libraries(${RELLIC_DEC2HEX}
  PRIVATE
    "${PROJECT_NAME}_cxx_settings"
    "${PROJECT_NAME}"
    gflags::gflags
)

set(RELLIC_DEC2HEX "${RELLIC_DEC2HEX}" PARENT_SCOPE)

if(RELLIC_ENABLE_INSTALL)
  
  install(
    TARGETS
      ${RELLIC_DECOMP}
      ${RELLIC_HEADERGEN}
      ${RELLIC_XREF}
      ${RELLIC_REPL}
    EXPORT
      "${PROJECT_NAME}Targets"
    RUNTIME
      DESTINATION
        ${CMAKE_INSTALL_BINDIR}
  )

  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/xref/www/index.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/xref/www/main.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/xref/www/style.css"
    DESTINATION
      "var/www/html"
  )
  
  #
  # IDA UI plugin
  #
  install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ida-rellic.py"
    DESTINATION "share/rellic"
  )

endif(RELLIC_ENABLE_INSTALL)
