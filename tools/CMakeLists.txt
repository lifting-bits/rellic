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

if(RELLIC_ENABLE_INSTALL)
  
  install(
    TARGETS
      ${RELLIC_DECOMP}
      ${RELLIC_HEADERGEN}
    EXPORT
      "${PROJECT_NAME}Targets"
    RUNTIME
      DESTINATION
        "bin"
  )
  
  #
  # IDA UI plugin
  #
  install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ida-rellic.py"
    DESTINATION "share/rellic"
  )

endif(RELLIC_ENABLE_INSTALL)
