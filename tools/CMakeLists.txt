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

#
# rellic-headergen
#

set(RELLIC_HEADERGEN ${PROJECT_NAME}-headergen-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_HEADERGEN}
  headergen/HeaderGen.cpp
)

target_link_libraries(${RELLIC_HEADERGEN} PRIVATE ${PROJECT_NAME})
add_project_properties(${RELLIC_HEADERGEN})