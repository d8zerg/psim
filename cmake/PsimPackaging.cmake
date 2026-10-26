# Debian 13 amd64 packages of PSIM (ADR-034). One package per installable component.
# Built by `task package:build` (cpack in the release preset), checked by `task package:test`.
#
# A component is declared next to its target: install(... COMPONENT <name>) and
# psim_package(<name> SUMMARY <text>). The root CMakeLists.txt calls psim_finalize_packaging().

# psim_package(<component> SUMMARY <text>): Debian package psim-<component>.
function(psim_package component)
  cmake_parse_arguments(ARG "" "SUMMARY" "DEPENDS" ${ARGN})
  set_property(GLOBAL APPEND PROPERTY PSIM_PACKAGES "${component}")
  set_property(GLOBAL PROPERTY PSIM_PACKAGE_SUMMARY_${component} "${ARG_SUMMARY}")
  set_property(GLOBAL PROPERTY PSIM_PACKAGE_DEPENDS_${component} "${ARG_DEPENDS}")
endfunction()

macro(psim_finalize_packaging)
  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_VENDOR "Open-Source PSIM Platform")
  set(CPACK_PACKAGE_CONTACT "Open-Source PSIM Platform <psim@localhost>")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
  set(CPACK_DEBIAN_PACKAGE_SECTION misc)
  set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  # Derive shared-library dependencies and symbol versions from the actual Debian 13 binaries.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  get_property(psim_packages GLOBAL PROPERTY PSIM_PACKAGES)
  set(CPACK_COMPONENTS_ALL ${psim_packages})
  foreach(component IN LISTS psim_packages)
    string(TOUPPER "${component}" upper)
    get_property(summary GLOBAL PROPERTY PSIM_PACKAGE_SUMMARY_${component})
    set(CPACK_DEBIAN_${upper}_PACKAGE_NAME "psim-${component}")
    set(CPACK_COMPONENT_${upper}_DESCRIPTION "${summary}")
    get_property(dependencies GLOBAL PROPERTY PSIM_PACKAGE_DEPENDS_${component})
    set(package_dependencies "")
    foreach(dependency IN LISTS dependencies)
      list(APPEND package_dependencies "psim-${dependency} (= ${PROJECT_VERSION})")
    endforeach()
    if(package_dependencies)
      list(JOIN package_dependencies ", " CPACK_DEBIAN_${upper}_PACKAGE_DEPENDS)
    endif()
  endforeach()
  include(CPack)
endmacro()
