cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_HOME_DIRECTORY}/cmake ${CMAKE_HOME_DIRECTORY}/GG/cmake)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.7 CACHE STRING "Set the minimum OSX deployment target version")
set(CMAKE_OSX_ARCHITECTURES i386 CACHE STRING "Set the architecture the universal binaries for OSX should be built for")

set(CMAKE_CONFIGURATION_TYPES Debug Release)
IF(NOT CMAKE_BUILD_TYPE)
  MESSAGE(STATUS "Setting build type to 'Release' as none was specified.")
  SET(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  SET_PROPERTY(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
ENDIF(NOT CMAKE_BUILD_TYPE)

message(STATUS "Build type CMAKE_BUILD_TYPE set to ${CMAKE_BUILD_TYPE}")


##
## Global project configuration
##

project(FreeOrion)

include(GNUInstallDirs)
include(UseCodeCoverage)
include(TargetDependencies)

set(FreeOrion_VERSION 0.4.6+)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build the testing tree." OFF)
option(BUILD_PACKAGING "Build the installer/binary package" OFF)
option(FO_LINK_STATIC_BOOST_LIBS "Link static boost libraries." ${APPLE})


if(BUILD_TESTING)
    message( STATUS "Building Tests")
    enable_testing()
    enable_coverage()

    if(NOT TARGET unittest)
        add_custom_target(unittest
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMENT "Run tests for ${CMAKE_PROJECT_NAME}"
        )
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(WIN32)
    # Location of the FreeOrionSDK root
    set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/..")
    # Search dependency DLLs inside FreeOrionSDK
    target_dependencies_add_search_path("${CMAKE_PREFIX_PATH}/bin")
    set(CMAKE_INSTALL_BINDIR ".")
    set(FreeOrion_INSTALL_LIBDIR ".")
endif()

if(APPLE)
    # Location of the FreeOrionSDK root
    set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/Xcode/dep/")
    set(CMAKE_FRAMEWORK_PATH "${CMAKE_SOURCE_DIR}/Xcode/dep/Frameworks")
    set(CMAKE_PROGRAM_PATH "${CMAKE_FRAMEWORK_PATH}/Python.framework/Versions/Current/bin")
endif()

if(UNIX)
    set(FreeOrion_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}/freeorion")
endif()

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${FreeOrion_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_compile_options(
    # Enable (almost) all warnings on compilers
    # https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html#index-Wall-316
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    # http://clang.llvm.org/docs/DiagnosticsReference.html#wall
    $<$<CXX_COMPILER_ID:Clang>:-Wall>
    # https://msdn.microsoft.com/en-us/library/thxezb7y.aspx#Anchor_1
    $<$<CXX_COMPILER_ID:MSVC>:/W4>

    # Disable "'<param>': unreferenced formal parameter" warning
    $<$<CXX_COMPILER_ID:MSVC>:/wd4100>
    # Disable "'<class>::<member>': class '<member-type>' needs to have dll-interface to be used by clients of class '<class>'" warning
    $<$<CXX_COMPILER_ID:MSVC>:/wd4251>
    # Disable "'<func>': unreferenced local function has been removed" warning
    $<$<CXX_COMPILER_ID:MSVC>:/wd4505>
    # Disable "declaration of '<type>' hides global declaration" warning
    $<$<CXX_COMPILER_ID:MSVC>:/wd4459>
    # Disable "function '<func>' marked as __forceinline not inlined" warning
    $<$<CXX_COMPILER_ID:MSVC>:/wd4714>

    # Enable C++ Exception unwind mechanics with DLL support
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

set_property(DIRECTORY APPEND
    PROPERTY COMPILE_DEFINITIONS
        # Set minimum Windows target version to WindowsXP
        # https://msdn.microsoft.com/en-us/library/aa383745.aspx
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=_WIN32_WINNT_WINXP>
        # Default to unicode variants when using Win32 API
        # https://msdn.microsoft.com/en-us/library/dybsewaf.aspx
        $<$<PLATFORM_ID:Windows>:_UNICODE>
        # Disable "'<func>' was declared deprecated" warning
        # https://msdn.microsoft.com/en-us/library/ttcz0bys.aspx
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
        # Disable '<func>': Function call with parameters that may be unsafe" warning
        # https://msdn.microsoft.com/en-us/library/ttcz0bys.aspx
        $<$<PLATFORM_ID:Windows>:_SCL_SECURE_NO_WARNINGS>
        # Disable '<func>': The POSIX name for this item is deprecated." warning
        # https://msdn.microsoft.com/en-us/library/ttcz0bys.aspx
        $<$<PLATFORM_ID:Windows>:_CRT_NONSTDC_NO_WARNINGS>

        # Define platform specific macros
        $<$<PLATFORM_ID:Windows>:FREEORION_WIN32>
        $<$<PLATFORM_ID:Darwin>:FREEORION_MACOSX>
        $<$<PLATFORM_ID:Linux>:FREEORION_LINUX>
        $<$<PLATFORM_ID:FreeBSD>:FREEORION_FREEBSD>
)

if(WIN32 AND MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
endif()

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
    set(CMAKE_ARCHIVE_LINKER_FLAGS "${CMAKE_ARCHIVE_LINKER_FLAGS} -stdlib=libc++")
endif()

if(UNIX AND NOT APPLE)
    add_definitions(-DENABLE_BINRELOC)
endif()


##
## Collect project dependencies.
##

set(MINIMUM_PYTHON_VERSION 2.7)
set(MINIMUM_BOOST_VERSION 1.56.0)

find_package(Threads)
find_package(PythonInterp ${MINIMUM_PYTHON_VERSION} REQUIRED)
find_package(PythonLibs ${MINIMUM_PYTHON_VERSION} REQUIRED)
find_package(Boost ${MINIMUM_BOOST_VERSION}
    COMPONENTS
        date_time
        filesystem
        iostreams
        locale
        log
        regex
        serialization
        python
        signals
        system
        thread
    REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenGL REQUIRED)

if(NOT OPENGL_GLU_FOUND)
    message(FATAL_ERROR "OpenGL GLU library not found.")
endif()

find_package(SDL REQUIRED)
find_package(OpenAL REQUIRED)
find_package(Ogg REQUIRED)
find_package(Vorbis REQUIRED)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(ICONV_LIBRARY iconv)
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()

set(BUILD_DEVEL_PACKAGE OFF CACHE INTERNAL "Disables installation of GiGi development files." FORCE)
set(_ORIG_CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
set(CMAKE_INSTALL_LIBDIR "${FreeOrion_INSTALL_LIBDIR}")
add_subdirectory(GG)
set(CMAKE_INSTALL_LIBDIR ${_ORIG_CMAKE_INSTALL_LIBDIR})
unset(_ORIG_CMAKE_INSTALL_LIBDIR)

set_property(DIRECTORY APPEND
    PROPERTY COMPILE_DEFINITIONS
        # Disable Boost auto-linking
        # http://www.boost.org/doc/libs/1_59_0/libs/config/doc/html/index.html#boost_config.configuring_boost_for_your_platform.user_settable_options
        BOOST_ALL_NO_LINK

        # Boost iostreams doesn't honor BOOST_ALL_NO_LINK when linking zlib.
        # It also tries to link by default to the boost in-source zlib librar,
        # which we don't want.
        # http://www.boost.org/doc/libs/1_59_0/boost/iostreams/detail/config/zlib.hpp
        $<$<PLATFORM_ID:Windows>:BOOST_ZLIB_BINARY=zlib.lib>

        # Link all boost libraries dynamically unless the user requests
        # otherwise.
        # http://www.boost.org/doc/libs/1_59_0/libs/config/doc/html/index.html#boost_config.configuring_boost_for_your_platform.user_settable_options
        $<$<NOT:$<BOOL:${FO_LINK_STATIC_BOOST_LIBS}>>:BOOST_ALL_DYN_LINK>

        # with boost 1.61 some boost::optional internals were changed. However
        # boost::spirit relies on some API the old implementation provided.
        # This define enables the usage of the old boost::optional
        # implementation.  Boost upstream tracks this bug as #12349
        $<$<VERSION_GREATER:${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION},1.60>:BOOST_OPTIONAL_CONFIG_USE_OLD_IMPLEMENTATION_OF_OPTIONAL>

        # We don't need localized output of Boost date_time and not setting
        # the define causes the inclusion of code, which contains std::tolower.
        # This however causes a macro substitutions caused by the libpython
        # headers, which in turn breaks the build.  Python 2.7.13 should have
        # fixed this with python bug #10910
        $<$<PLATFORM_ID:Darwin>:BOOST_DATE_TIME_NO_LOCALE>
        $<$<PLATFORM_ID:FreeBSD>:BOOST_DATE_TIME_NO_LOCALE>
)

link_directories(
    ${Boost_LIBRARY_DIRS}
)


##
## Define main project targets.
##

# To run the version generation every compile we need to deferr the
# execution to a separate target and the existing python command
add_custom_target(freeorionversion
    COMMAND
    "${PYTHON_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/cmake/make_versioncpp.py"
    "${CMAKE_SOURCE_DIR}"
    "CMake"
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/util/Version.cpp
    PROPERTIES
    GENERATED TRUE
)


add_library(freeorioncommon "")

if(WIN32)
    set_property(TARGET freeorioncommon
        PROPERTY
        OUTPUT_NAME Common
    )
endif()

target_compile_options(freeorioncommon
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-ftemplate-depth=512>
        $<$<CXX_COMPILER_ID:AppleClang>:-ftemplate-depth=512>
)

if(APPLE)
    set_target_properties(freeorioncommon
        PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()

target_compile_definitions(freeorioncommon
    PRIVATE
    -DFREEORION_BUILD_COMMON
)

target_include_directories(freeorioncommon SYSTEM
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${Boost_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/GG
        ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(freeorioncommon
    ${Boost_DATE_TIME_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_IOSTREAMS_LIBRARY}
    ${Boost_LOG_LIBRARY}
    ${Boost_REGEX_LIBRARY}
    ${Boost_SERIALIZATION_LIBRARY}
    ${Boost_SIGNALS_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${ZLIB_LIBRARIES}
    ${CORE_FOUNDATION_LIBRARY}
)

add_dependencies(freeorioncommon freeorionversion)


add_library(freeorionparseobj OBJECT "")

target_include_directories(freeorionparseobj SYSTEM
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/GG
)

set_property(TARGET freeorionparseobj
    PROPERTY
    POSITION_INDEPENDENT_CODE ON
)

if(WIN32)
    set_property(TARGET freeorionparse
        PROPERTY
        OUTPUT_NAME Parsers
    )
endif()

target_compile_options(freeorionparseobj
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-ftemplate-depth=512>
        $<$<CXX_COMPILER_ID:AppleClang>:-ftemplate-depth=512>
        $<$<AND:$<NOT:$<BOOL:${BUILD_TESTING}>>,$<CXX_COMPILER_ID:GNU>>:-O3>
)

target_compile_definitions(freeorionparseobj
    PRIVATE
        -DNDEBUG
        -DFREEORION_BUILD_PARSE
)


add_library(freeorionparse $<TARGET_OBJECTS:freeorionparseobj>)

target_compile_options(freeorionparse
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
)

if(APPLE)
    # Xcode doesn't build freeorionparse because it doesn't have any
    # source code files associated and only links the freeorionparselib
    # objects into a shared library.  Adding an empty file fixes this.
    # https://cmake.org/cmake/help/v3.0/command/add_library.html
    file(WRITE xcode_dummy.cpp "")
    target_sources(freeorionparse
        PRIVATE
            xcode_dummy.cpp
    )

    set_target_properties(freeorionparse
        PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()


add_executable(freeoriond "")

if(WIN32)
    set_property(TARGET freeoriond
        PROPERTY
            OUTPUT_NAME FreeOrionD
    )
endif()

target_compile_options(freeoriond
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
)

target_compile_definitions(freeoriond
    PRIVATE
        -DFREEORION_BUILD_SERVER
)

target_include_directories(freeoriond SYSTEM
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/GG
        ${PYTHON_INCLUDE_PATH}
)

target_link_libraries(freeoriond
    freeorioncommon
    freeorionparse
    ${PYTHON_LIBRARIES}
    ${Boost_PYTHON_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)

target_dependencies_copy_to_build(freeoriond)
target_dependent_data_symlink_to_build(freeoriond ${PROJECT_SOURCE_DIR}/default)


add_executable(freeorionca "")

if(WIN32)
    set_property(TARGET freeorionca
        PROPERTY
            OUTPUT_NAME FreeOrionCA
    )
endif()

target_compile_options(freeorionca
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
)

target_compile_definitions(freeorionca
    PRIVATE
        -DFREEORION_BUILD_AI
)

target_include_directories(freeorionca SYSTEM
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_PATH}
        ${ZLIB_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/GG
)

target_link_libraries(freeorionca
    freeorioncommon
    freeorionparse
    ${PYTHON_LIBRARIES}
    ${Boost_PYTHON_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)

target_dependencies_copy_to_build(freeorionca)
target_dependent_data_symlink_to_build(freeorionca ${PROJECT_SOURCE_DIR}/default)


add_executable(freeorion "")

if(WIN32)
    set_property(TARGET freeorion
        PROPERTY
            OUTPUT_NAME FreeOrion
    )
    if(MSVC AND (CMAKE_VERSION VERSION_GREATER "3.6"))
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PROPERTY
                VS_STARTUP_PROJECT freeorion
        )
    endif()
endif()

target_compile_options(freeorion
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:Clang>:-fvisibility=hidden>
        $<$<CXX_COMPILER_ID:AppleClang>:-fvisibility=hidden>
)

target_compile_definitions(freeorion
    PRIVATE
        -DFREEORION_BUILD_HUMAN
)

target_include_directories(freeorion SYSTEM
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIRS}
        ${SDL_INCLUDE_DIRS}
        ${OPENAL_INCLUDE_DIR}
        ${OGG_INCLUDE_DIRS}
        ${VORBIS_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/GG
)

if(WIN32)
    target_sources(freeorion
        PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/FreeOrion.ico
    )
endif()

target_link_libraries(freeorion
    freeorioncommon
    freeorionparse
    GiGi
    GiGiSDL
    ${OPENGL_gl_LIBRARY}
    ${OPENGL_glu_LIBRARY}
    ${OPENAL_LIBRARY}
    ${SDL_LIBRARIES}
    ${OGG_LIBRARIES}
    ${VORBIS_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${Boost_LOCALE_LIBRARY}
    ${ICONV_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)

target_dependencies_copy_to_build(freeorion)
target_dependent_data_symlink_to_build(freeorion ${PROJECT_SOURCE_DIR}/default)


##
## Recurse into sources.
##

add_subdirectory(AI)
add_subdirectory(client)
add_subdirectory(combat)
add_subdirectory(Empire)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(python)
add_subdirectory(server)
add_subdirectory(UI)
add_subdirectory(universe)
add_subdirectory(util)
add_subdirectory(check)
add_subdirectory(doc)


##
## Install targets and package project.
##

install(
    TARGETS
        freeorioncommon
        freeorionparse
    LIBRARY DESTINATION ${FreeOrion_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT COMPONENT_FREEORION
)

install(
    TARGETS
        freeoriond
        freeorionca
        freeorion
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT COMPONENT_FREEORION
)

install(
    DIRECTORY default/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/freeorion/default
    COMPONENT COMPONENT_FREEORION_DATA
    PATTERN "*~" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)

install(
    FILES
    ${CMAKE_SOURCE_DIR}/freeorion.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT COMPONENT_FREEORION
)

foreach(SIZE 16 24 32 64 128 256)
    install(
        FILES
        ${CMAKE_SOURCE_DIR}/default/data/art/icons/FO_Icon_${SIZE}x${SIZE}.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${SIZE}x${SIZE}/apps/
        RENAME freeorion.png
        COMPONENT COMPONENT_FREEORION
    )
endforeach()

if(WIN32)
    install(
        FILES
        OpenAL32.dll
        boost_date_time-vc90-mt-1_44.dll
        boost_filesystem-vc90-mt-1_44.dll
        boost_iostreams-vc90-mt-1_44.dll
        boost_python-vc90-mt-1_44.dll
        boost_regex-vc90-mt-1_44.dll
        boost_serialization-vc90-mt-1_44.dll
        boost_signals-vc90-mt-1_44.dll
        boost_system-vc90-mt-1_44.dll
        boost_thread-vc90-mt-1_44.dll
        glew32.dll
        libexpat.dll
        libogg.dll
        libpng13.dll
        libvorbis.dll
        libvorbisfile.dll
        python26.dll
        wrap_oal.dll
        z.dll
        zlib1.dll
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT COMPONENT_FREEORION_WIN32_RUNTIME_DEPS
    )
endif()

set(CPACK_PACKAGE_VERSION ${FreeOrion_VERSION})

set(CPACK_PACKAGE_VENDOR "FreeOrion Community")
set(CPACK_PACKAGE_CONTACT http://freeorion.org/forum)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "FreeOrion is a free, open source, turn-based space empire and galactic conquest (4X) computer game being designed and built by the FreeOrion project. FreeOrion is inspired by the tradition of the Master of Orion games, but is not a clone or remake of that series or any other game.")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_HOME_DIRECTORY}/default/COPYING)
if(BUILD_PACKAGING)
    set(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
    if(WIN32)
        set(PACKAGE_FILE_SYSTEM_NAME win32)
    else()
        set(PACKAGE_FILE_SYSTEM_NAME ${CPACK_SYSTEM_NAME})
    endif()
    set(CPACK_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${PACKAGE_FILE_SYSTEM_NAME}")
    string(TOLOWER ${CPACK_PACKAGE_FILE_NAME} CPACK_PACKAGE_FILE_NAME)
    set(CPACK_MONOLITHIC_INSTALL ON)
endif()

# NSIS-specific settings
set(CPACK_NSIS_COMPRESSOR bzip2)
set(CPACK_NSIS_URL_INFO_ABOUT http://freeorion.org)
string(REPLACE "/" "\\\\" cmake_home_directory_with_native_windows_path_separators ${CMAKE_HOME_DIRECTORY})

# RPM-specific settings
set(CPACK_RPM_PACKAGE_LICENSE GPL)
set(CPACK_RPM_PACKAGE_REQUIRES)
# TODO

# Deb-specific settings
set(CPACK_DEBIAN_PACKAGE_SECTION games)
# TODO

if(UNIX)
    set(CPACK_GENERATOR "TGZ;TBZ2")
    find_program(RPMBUILD rpmbuild)
    if(RPMBUILD)
        list(APPEND CPACK_GENERATOR RPM)
    endif()
    set(RPMBUILD ${RPMBUILD} CACHE INTERNAL "")
    find_program(DPKG dpkg)
    if(DPKG)
        list(APPEND CPACK_GENERATOR DEB)
    endif()
    set(DPKG ${DPKG} CACHE INTERNAL "")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")
endif()


########################################
# Source Packaging                     #
########################################

if(UNIX)
    set(CPACK_SOURCE_GENERATOR "TGZ")
elseif(WIN32)
    set(CPACK_SOURCE_GENERATOR "ZIP")
endif()

set(CPACK_SOURCE_IGNORE_FILES
    "~$"
    "\\\\.asm$"
    "\\\\.bz2$"
    "/CMakeCache\\\\.txt$"
    "/CMakeFiles/"
    "/cmake_install\\\\.cmake$"
    "/CPackConfig.cmake$"
    "/_CPack_Packages/"
    "/CPackSourceConfig.cmake$"
    "/CTestTestfile\\\\.cmake$"
    "\\\\.dll$"
    "\\\\.exe$"
    "\\\\.exp$"
    "/freeorion$"
    "/freeorionca$"
    "/freeoriond$"
    "GG/GG/Config.h$"
    "\\\\.git/"
    "\\\\.gz$"
    "\\\\.lib$"
    "/Makefile$"
    "\\\\.pc$"
    "\\\\.pdb$"
    "\\\\.pyc$"
    "/Release/"
    "\\\\.so$"
    "\\\\.swp$"
)

set(CPACK_SOURCE_PACKAGE_FILE_NAME
    "${CMAKE_PROJECT_NAME}-v${FreeOrion_VERSION}-${FreeOrion_WC_REVISION}-source"
)

include(CPack)

if(WIN32)
    cpack_add_component(
        COMPONENT_FREEORION_WIN32_RUNTIME_DEPS
        DISPLAY_NAME FreeOrion Win32 Runtime Dependencies
        DESCRIPTION ""
        HIDDEN
        REQUIRED
    )
endif()

cpack_add_component(
    COMPONENT_FREEORION_DATA
    DISPLAY_NAME FreeOrion Data
    DESCRIPTION ""
    HIDDEN
    REQUIRED
)

cpack_add_component(
    COMPONENT_FREEORION
    DISPLAY_NAME FreeOrion Client
    DESCRIPTION ""
    HIDDEN
    REQUIRED
)

cpack_add_component(
    COMPONENT_FREEORIOND
    DISPLAY_NAME FreeOrion Server
    DESCRIPTION ""
    HIDDEN
    REQUIRED
)

cpack_add_component(
    COMPONENT_FREEORIONCA
    DISPLAY_NAME FreeOrion AI
    DESCRIPTION ""
    HIDDEN
    REQUIRED
)
