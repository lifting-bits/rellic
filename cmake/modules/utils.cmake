function(locateLibrary)
  cmake_parse_arguments(
    PARSE_ARGV
    0
    "LOCATELIBRARY"
    ""
    "NAME;HINT"
    "LIBRARIES;MAIN_INCLUDE"
  )

  add_library("${LOCATELIBRARY_NAME}" INTERFACE)

  # Import the (sub)libraries
  foreach(library ${LOCATELIBRARY_LIBRARIES})
    set(target_name "${LOCATELIBRARY_NAME}_${library}")

    set(location_name "${target_name}_lib_location")
    find_library("${location_name}"
      NAMES "${library}"
      PATHS "${LOCATELIBRARY_HINT}"
      PATH_SUFFIXES "lib"
    )

    if("${${location_name}}" STREQUAL "${location_name}-NOTFOUND")
      message(FATAL_ERROR "Failed to locate the following library: ${library}")
    endif()

    add_library("${target_name}" UNKNOWN IMPORTED GLOBAL)
    set_target_properties("${target_name}" PROPERTIES
      IMPORTED_LOCATION "${${location_name}}"
    )

    target_link_libraries("${LOCATELIBRARY_NAME}" INTERFACE
      "${target_name}"
    )

    message(STATUS "Found: ${${location_name}}")
  endforeach()

  # Locate the include header
  set(location_name "${target_name}_header_location")
  find_path("${location_name}"
    NAMES "${LOCATELIBRARY_MAIN_INCLUDE}"
    PATHS "${LOCATELIBRARY_HINT}"
    PATH_SUFFIXES "include"
  )

  if("${${location_name}}" STREQUAL "${location_name}-NOTFOUND")
    message(FATAL_ERROR "Failed to locate the following header file: ${library}")
  endif()

  message(STATUS "Found: ${${location_name}}")

  target_include_directories("${LOCATELIBRARY_NAME}" INTERFACE
    "${${location_name}}"
  )

  set("${LOCATELIBRARY_NAME}_FOUND" true PARENT_SCOPE)
endfunction()
