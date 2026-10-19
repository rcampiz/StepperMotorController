# ---------------------------------------------------------------------------
# Writes the build id header; run on every build by smc_add_build_id (cmake/smc_version.cmake):
#
#   cmake -DREPO=<repo root> -DOUT=<header> [-DDIRTY_PATHS=a,b,c] -P smc_build_id.cmake
#
# The id is `git describe --tags --always --abbrev=12 --match v[0-9]*`: "v0.5.0" on a tagged
# release, "v0.5.0-3-g1a2b3c4d5e6f" three commits after it, the bare short hash before the
# first tag. "+dirty" is appended when DIRTY_PATHS (default: the whole tree) have uncommitted
# changes. Outside a git checkout, or without git, the id is "nogit". The file is written only
# when its content changes, so an unchanged tree recompiles nothing.
# ---------------------------------------------------------------------------
set(id "nogit")
execute_process(COMMAND git describe --tags --always --abbrev=12 --match "v[0-9]*"
                WORKING_DIRECTORY "${REPO}"
                OUTPUT_VARIABLE described OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE rc ERROR_QUIET)
if(rc EQUAL 0 AND NOT described STREQUAL "")
    set(id "${described}")
    set(paths "")
    if(DEFINED DIRTY_PATHS AND NOT DIRTY_PATHS STREQUAL "")
        string(REPLACE "," ";" paths "${DIRTY_PATHS}")
    endif()
    execute_process(COMMAND git diff --quiet HEAD -- ${paths}
                    WORKING_DIRECTORY "${REPO}" RESULT_VARIABLE dirty ERROR_QUIET)
    if(NOT dirty EQUAL 0)
        set(id "${id}+dirty")
    endif()
endif()

set(content "// Generated at build time by cmake/smc_build_id.cmake. Do not edit or commit.\n#define SMC_BUILD_ID \"${id}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
    message(STATUS "Build id: ${id}")
endif()
