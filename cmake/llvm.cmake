#
# Copyright (c) 2021-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

function(find_llvm target_name hint_dir)
  
  find_package(llvm CONFIG REQUIRED HINTS "${hint_dir}")
  
  add_library("${target_name}" INTERFACE)
  target_include_directories("${target_name}" SYSTEM INTERFACE
    $<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}>
  )
  target_compile_definitions("${target_name}" INTERFACE
    ${LLVM_DEFINITIONS}
  )
  
  # Go find only the static libraries of LLVM, and link against those.
  foreach(LLVM_LIB IN LISTS LLVM_AVAILABLE_LIBS)
    get_target_property(LLVM_LIB_TYPE ${LLVM_LIB} TYPE)
    if(LLVM_LIB_TYPE STREQUAL "STATIC_LIBRARY")
      list(APPEND LLVM_LIBRARIES "${LLVM_LIB}")
    endif()
  endforeach()
  
  # These are out-of-order in `LLVM_AVAILABLE_LIBS` and should always be last.
  list(REMOVE_ITEM LLVM_LIBRARIES LLVMMC LLVMCore LLVMSupport)
  list(APPEND LLVM_LIBRARIES LLVMMC LLVMCore LLVMSupport)
  
  target_link_libraries("${target_name}" INTERFACE
    ${LLVM_LIBRARIES}
  )

endfunction(find_llvm)