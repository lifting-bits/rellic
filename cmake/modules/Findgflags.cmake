include("${CMAKE_CURRENT_LIST_DIR}/utils.cmake")

set(RELLIC_GFLAGS_LOCATION "/usr" CACHE FILEPATH "gflags install directory")

set(gflags_library_list
  "gflags"
)

message(STATUS "Attempting to locate: gflags (hints: RELLIC_GFLAGS_LOCATION=\"${RELLIC_GFLAGS_LOCATION}\")")

locateLibrary(
  NAME "gflags"
  HINT "${RELLIC_GFLAGS_LOCATION}"
  LIBRARIES ${gflags_library_list}
  MAIN_INCLUDE "gflags/gflags.h"
)
