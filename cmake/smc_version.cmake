# ---------------------------------------------------------------------------
# Release version and build identity, shared by the firmware (top-level CMakeLists.txt),
# the virtual Nucleo (sim/) and the host tests (tests/).
#
#   SMC_VERSION     the release, read from <repo>/VERSION (SemVer MAJOR.MINOR.PATCH, an
#                   optional -prerelease). Changing VERSION re-runs configure; every
#                   translation unit sees it as SMC_FW_VERSION (Harness::FW_VERSION).
#   smc_add_build_id(<target> [dirty paths...])
#                   the build id (git describe of the tree that was compiled, "+dirty" when
#                   the listed paths have uncommitted changes) regenerated on EVERY build into
#                   <binary dir>/generated/smc_build_id.h. The header is rewritten only when
#                   the id changes, and only harness/pins/build_id.cpp includes it, so a new
#                   commit recompiles one file and relinks; an unchanged tree rebuilds nothing.
#
# Cutting a release: CHANGELOG.md (bump VERSION and the client copies it names, commit, tag vX.Y.Z).
# ---------------------------------------------------------------------------
get_filename_component(SMC_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(READ "${SMC_REPO_ROOT}/VERSION" SMC_VERSION)
string(STRIP "${SMC_VERSION}" SMC_VERSION)
if(NOT SMC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "VERSION holds '${SMC_VERSION}', expected MAJOR.MINOR.PATCH[-prerelease]")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SMC_REPO_ROOT}/VERSION")
message(STATUS "SMC version:             ${SMC_VERSION}")

function(smc_add_build_id target)
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(header "${gen_dir}/smc_build_id.h")
    string(REPLACE ";" "," dirty_paths "${ARGN}")
    add_custom_target(${target}_build_id
        COMMAND ${CMAKE_COMMAND} "-DREPO=${SMC_REPO_ROOT}" "-DOUT=${header}"
                "-DDIRTY_PATHS=${dirty_paths}" -P "${SMC_REPO_ROOT}/cmake/smc_build_id.cmake"
        BYPRODUCTS "${header}"
        COMMENT "Build id"
        VERBATIM)
    add_dependencies(${target} ${target}_build_id)
    target_include_directories(${target} PRIVATE "${gen_dir}")
    target_compile_definitions(${target} PRIVATE SMC_HAVE_BUILD_ID_H=1)
endfunction()
