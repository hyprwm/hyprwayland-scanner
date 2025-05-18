cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} VERSION)

project(
  hyprwayland-scanner
  DESCRIPTION "A hyprland version of wayland-scanner in and for C++"
  VERSION ${VERSION})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(PREFIX ${CMAKE_INSTALL_PREFIX})

set(CMAKE_MESSAGE_LOG_LEVEL "STATUS")

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring hyprwayland-scanner in Debug with CMake")
  add_compile_definitions(HYPRLAND_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprwayland-scanner in Release with CMake")
endif()

# configure
set(CMAKE_CXX_STANDARD 23)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-value
                    -Wno-missing-field-initializers -Wno-narrowing)

add_compile_definitions(SCANNER_VERSION="${VERSION}")

configure_file(hyprwayland-scanner.pc.in hyprwayland-scanner.pc @ONLY)

# dependencies
message(STATUS "Checking deps...")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET pugixml)

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(hyprwayland-scanner ${SRCFILES})
target_link_libraries(hyprwayland-scanner PRIVATE rt Threads::Threads
                                                  PkgConfig::deps)

configure_package_config_file(
  hyprwayland-scanner-config.cmake.in hyprwayland-scanner-config.cmake
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hyprwayland-scanner"
  PATH_VARS CMAKE_INSTALL_BINDIR)
write_basic_package_version_file(
  "hyprwayland-scanner-config-version.cmake"
  VERSION "${VERSION}"
  ARCH_INDEPENDENT
  COMPATIBILITY AnyNewerVersion)

# Installation
install(TARGETS hyprwayland-scanner)
install(FILES ${CMAKE_BINARY_DIR}/hyprwayland-scanner.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES ${CMAKE_BINARY_DIR}/hyprwayland-scanner-config.cmake
              ${CMAKE_BINARY_DIR}/hyprwayland-scanner-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hyprwayland-scanner)
