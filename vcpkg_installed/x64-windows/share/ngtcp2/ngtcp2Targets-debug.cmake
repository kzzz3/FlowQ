#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ngtcp2::ngtcp2" for configuration "Debug"
set_property(TARGET ngtcp2::ngtcp2 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(ngtcp2::ngtcp2 PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/ngtcp2.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/ngtcp2.dll"
  )

list(APPEND _cmake_import_check_targets ngtcp2::ngtcp2 )
list(APPEND _cmake_import_check_files_for_ngtcp2::ngtcp2 "${_IMPORT_PREFIX}/debug/lib/ngtcp2.lib" "${_IMPORT_PREFIX}/debug/bin/ngtcp2.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
