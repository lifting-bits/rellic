#
# rellic-unittest
#

set(RELLIC_UNITTEST ${PROJECT_NAME}-unittest)

add_executable(${RELLIC_UNITTEST}
  AST/ASTBuilder.cpp
  AST/StructGenerator.cpp
  AST/Util.cpp
  UnitTest.cpp
)

target_link_libraries(${RELLIC_UNITTEST} PRIVATE
  "${PROJECT_NAME}_cxx_settings"
  "${PROJECT_NAME}"
  z3::libz3
  doctest::doctest
)

target_compile_options(${RELLIC_UNITTEST} PRIVATE -fexceptions)

add_test(
  NAME test_${RELLIC_UNITTEST}
  COMMAND "$<TARGET_FILE:${RELLIC_UNITTEST}>"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
