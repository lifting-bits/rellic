#
# rellic-unittest
#

set(RELLIC_UNITTEST ${PROJECT_NAME}-unittest-${RELLIC_LLVM_VERSION})

add_executable(${RELLIC_UNITTEST}
  AST/ASTBuilder.cpp
)

target_link_libraries(${RELLIC_UNITTEST} PRIVATE
    ${PROJECT_NAME}
    thirdparty_doctest
)

add_project_properties(${RELLIC_UNITTEST})

set(RELLIC_UNITTEST ${RELLIC_UNITTEST} PARENT_SCOPE)