#
# rellic-decomp
#

set(RELLIC_DECOMP ${PROJECT_NAME}-decomp-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_DECOMP}
  decomp/Decomp.cpp
)

target_link_libraries(${RELLIC_DECOMP} PRIVATE ${PROJECT_NAME})
add_project_properties(${RELLIC_DECOMP})

set(RELLIC_DECOMP ${RELLIC_DECOMP} PARENT_SCOPE)

install(TARGETS ${RELLIC_DECOMP} DESTINATION "bin")

#
# rellic-headergen
#

set(RELLIC_HEADERGEN ${PROJECT_NAME}-headergen-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_HEADERGEN}
  headergen/HeaderGen.cpp
)

target_link_libraries(${RELLIC_HEADERGEN} PRIVATE ${PROJECT_NAME})
add_project_properties(${RELLIC_HEADERGEN})

install(TARGETS ${RELLIC_HEADERGEN} DESTINATION "bin")

#
# rellic-layoutgen
#

set(RELLIC_LAYOUTGEN ${PROJECT_NAME}-layoutgen-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_LAYOUTGEN}
  layoutgen/LayoutGen.cpp
)

target_link_libraries(${RELLIC_LAYOUTGEN} PRIVATE ${PROJECT_NAME})
add_project_properties(${RELLIC_LAYOUTGEN})

set(RELLIC_LAYOUTGEN ${RELLIC_LAYOUTGEN} PARENT_SCOPE)

install(TARGETS ${RELLIC_LAYOUTGEN} DESTINATION "bin")

#
# IDA UI plugin
#

install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ida-rellic.py"
  DESTINATION "share/rellic"
)
