# Debian packages of PSIM (step 1.3, ADR-034). One package per installable component; the same
# package installs on Debian 12 and Astra Linux 1.8 (glibc 2.36, libc++ linked statically, ADR-032).
# Built by `task package:build` (cpack in the release preset), checked by `task package:test`.
#
# A component is declared next to its target: install(... COMPONENT <name>) and
# psim_package(<name> SUMMARY <text>). The root CMakeLists.txt calls psim_finalize_packaging().

# psim_package(<component> SUMMARY <text>): Debian package psim-<component>.
function(psim_package component)
  cmake_parse_arguments(ARG "" "SUMMARY" "" ${ARGN})
  set_property(GLOBAL APPEND PROPERTY PSIM_PACKAGES "${component}")
  set_property(GLOBAL PROPERTY PSIM_PACKAGE_SUMMARY_${component} "${ARG_SUMMARY}")
endfunction()

macro(psim_finalize_packaging)
  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_VENDOR "PSIM Platform")
  set(CPACK_PACKAGE_CONTACT "PSIM Platform <psim@localhost>")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
  set(CPACK_DEBIAN_PACKAGE_SECTION misc)
  set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  # Only glibc is linked dynamically; 2.36 is the oldest target (Debian 12, Astra Linux 1.8).
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.36)")
  get_property(psim_packages GLOBAL PROPERTY PSIM_PACKAGES)
  set(CPACK_COMPONENTS_ALL ${psim_packages})
  foreach(component IN LISTS psim_packages)
    string(TOUPPER "${component}" upper)
    get_property(summary GLOBAL PROPERTY PSIM_PACKAGE_SUMMARY_${component})
    set(CPACK_DEBIAN_${upper}_PACKAGE_NAME "psim-${component}")
    set(CPACK_COMPONENT_${upper}_DESCRIPTION "${summary}")
  endforeach()
  include(CPack)
endmacro()
