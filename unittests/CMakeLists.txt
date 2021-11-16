#
# rellic-unittest
#

set(RELLIC_UNITTEST ${PROJECT_NAME}-unittest-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_UNITTEST}
  AST/ASTBuilder.cpp
  AST/StructGenerator.cpp
  AST/Util.cpp
  AST/Z3ConvVisitor.cpp
  UnitTest.cpp
)

add_project_properties(${RELLIC_UNITTEST})

target_link_libraries(${RELLIC_UNITTEST} PRIVATE
    ${PROJECT_NAME}
    thirdparty_doctest
)

target_compile_options(${RELLIC_UNITTEST} PRIVATE -fexceptions)

add_test(
  NAME test_${RELLIC_UNITTEST}
  COMMAND "$<TARGET_FILE:${RELLIC_UNITTEST}>"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)