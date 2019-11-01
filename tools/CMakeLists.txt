#
# rellic-decomp
#

set(RELLIC_DECOMP ${PROJECT_NAME}-decomp-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_DECOMP}
  decomp/Decomp.cpp
)

target_link_libraries(${RELLIC_DECOMP} PRIVATE ${PROJECT_NAME})
target_link_libraries(${RELLIC_DECOMP} PRIVATE ${PROJECT_LIBRARIES})
target_include_directories(${RELLIC_DECOMP} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
target_compile_definitions(${RELLIC_DECOMP} PUBLIC ${PROJECT_DEFINITIONS})
target_compile_options(${RELLIC_DECOMP} PRIVATE ${PROJECT_CXXFLAGS})

#
# rellic-headergen
#

set(RELLIC_HEADERGEN ${PROJECT_NAME}-headergen-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_HEADERGEN}
  headergen/HeaderGen.cpp
)

target_link_libraries(${RELLIC_HEADERGEN} PRIVATE ${PROJECT_NAME})
target_link_libraries(${RELLIC_HEADERGEN} PRIVATE ${PROJECT_LIBRARIES})
target_include_directories(${RELLIC_HEADERGEN} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
target_compile_definitions(${RELLIC_HEADERGEN} PUBLIC ${PROJECT_DEFINITIONS})
target_compile_options(${RELLIC_HEADERGEN} PRIVATE ${PROJECT_CXXFLAGS})