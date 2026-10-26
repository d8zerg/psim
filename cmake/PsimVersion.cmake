find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/version.py"
  OUTPUT_VARIABLE psim_version_json OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(JSON PSIM_VERSION GET "${psim_version_json}" version)
string(JSON PSIM_NUMERIC_VERSION GET "${psim_version_json}" numeric)
string(JSON PSIM_DEBIAN_VERSION GET "${psim_version_json}" debian)
string(JSON PSIM_IMAGE_VERSION GET "${psim_version_json}" image)
string(JSON PSIM_GIT_COMMIT GET "${psim_version_json}" revision)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/tools/version.py" "${CMAKE_SOURCE_DIR}/.git/HEAD")
file(GLOB_RECURSE psim_git_refs CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/.git/refs/tags/*" "${CMAKE_SOURCE_DIR}/.git/refs/heads/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${psim_git_refs})
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/packed-refs")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/packed-refs")
endif()
