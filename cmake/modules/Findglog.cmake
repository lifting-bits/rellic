include("${CMAKE_CURRENT_LIST_DIR}/utils.cmake")

set(RELLIC_GLOG_LOCATION "/usr" CACHE FILEPATH "glog install directory")

set(glog_library_list
  "glog"
)

message(STATUS "Attempting to locate: glog (hints: RELLIC_GLOG_LOCATION=\"${RELLIC_GLOG_LOCATION}\")")

locateLibrary(
  NAME "glog" # Compatibility name for upstream real glog import
  HINT "${RELLIC_GLOG_LOCATION}"
  LIBRARIES ${glog_library_list}
  MAIN_INCLUDE "glog/logging.h"
)
