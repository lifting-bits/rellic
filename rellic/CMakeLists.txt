add_library(${PROJECT_NAME} STATIC
  AST/Compat/Stmt.cpp
  AST/Compat/Expr.cpp
  
  AST/CXXToCDecl.cpp
  AST/InferenceRule.cpp
  AST/DeadStmtElim.cpp
  AST/CondBasedRefine.cpp
  AST/ExprCombine.cpp
  AST/GenerateAST.cpp
  AST/IRToASTVisitor.cpp
  AST/LoopRefine.cpp
  AST/NestedCondProp.cpp
  AST/NestedScopeCombiner.cpp
  AST/Util.cpp
  AST/Z3CondSimplify.cpp
  AST/Z3ConvVisitor.cpp
  
  BC/Util.cpp
  BC/Compat/Value.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LIBRARIES})
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PROJECT_DEFINITIONS})
target_compile_options(${PROJECT_NAME} PRIVATE ${PROJECT_CXXFLAGS})