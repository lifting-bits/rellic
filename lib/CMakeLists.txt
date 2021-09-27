add_library(${PROJECT_NAME} STATIC
  AST/Compat/ASTContext.cpp
  AST/Compat/Mangle.cpp
  AST/Compat/Stmt.cpp
  
  AST/ASTBuilder.cpp
  AST/CXXToCDecl.cpp
  AST/InferenceRule.cpp
  AST/DeadStmtElim.cpp
  AST/CondBasedRefine.cpp
  AST/ExprCombine.cpp
  AST/GenerateAST.cpp
  AST/IRToASTVisitor.cpp
  AST/LoopRefine.cpp
  AST/NestedCondProp.cpp
  AST/NestedScopeCombine.cpp
  AST/Util.cpp
  AST/Z3CondSimplify.cpp
  AST/Z3ConvVisitor.cpp
  AST/ReachBasedRefine.cpp
  
  BC/Util.cpp
  BC/Compat/Value.cpp
)

#define the RellicVersion project
add_subdirectory(Version)

#link version information
target_link_libraries(${PROJECT_NAME} PRIVATE RellicVersion)

add_project_properties(${PROJECT_NAME})
