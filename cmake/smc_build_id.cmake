# ---------------------------------------------------------------------------
# Writes the build id header; run on every build by smc_add_build_id (cmake/smc_version.cmake):
#
#   cmake -DREPO=<repo root> -DOUT=<header> -DBASE=<VERSION> [-DDIRTY_PATHS=a,b,c]
#         [-DCOUNT_PATHS=a,b,c] -P smc_build_id.cmake
#
# The id is `git describe --tags --always --abbrev=12 --match fw-v[0-9]*`: "fw-v0.5.0" on a
# tagged firmware release, "fw-v0.5.0-3-g1a2b3c4d5e6f" three commits after it, the bare short
# hash before the first tag. "+dirty" is appended when DIRTY_PATHS (default: the whole tree) have uncommitted
# changes. Outside a git checkout, or without git, the id is "nogit". The file is written only
# when its content changes, so an unchanged tree recompiles nothing.
#
# The full version (docs/VERSIONING.md): VERSION holds the firmware release MAJOR.MINOR.0; the
# patch is the number of commits that touched COUNT_PATHS (the firmware's paths) since the tag
# fw-vMAJOR.MINOR.0, so every firmware commit has its own version ("0.6.3" three firmware
# commits after fw-v0.6.0) and a client-only commit leaves it alone; "+dirty" with uncommitted
# changes. Without the tag (or git) it is VERSION as written.
# ---------------------------------------------------------------------------
set(id "nogit")
set(full "${BASE}")
set(dirty_suffix "")
execute_process(COMMAND git describe --tags --always --abbrev=12 --match "fw-v[0-9]*"
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
        set(dirty_suffix "+dirty")
    endif()
    if(BASE MATCHES "^([0-9]+)\\.([0-9]+)\\.")
        set(major "${CMAKE_MATCH_1}")
        set(minor "${CMAKE_MATCH_2}")
        set(tag "fw-v${major}.${minor}.0")
        set(counted "")
        if(DEFINED COUNT_PATHS AND NOT COUNT_PATHS STREQUAL "")
            string(REPLACE "," ";" counted "${COUNT_PATHS}")
        endif()
        execute_process(COMMAND git rev-list --count "${tag}..HEAD" -- ${counted}
                        WORKING_DIRECTORY "${REPO}"
                        OUTPUT_VARIABLE since OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE rc2 ERROR_QUIET)
        if(rc2 EQUAL 0 AND since MATCHES "^[0-9]+$")
            set(full "${major}.${minor}.${since}${dirty_suffix}")
        endif()
    endif()
endif()

set(content "// Generated at build time by cmake/smc_build_id.cmake. Do not edit or commit.\n#define SMC_BUILD_ID \"${id}\"\n#define SMC_VERSION_FULL \"${full}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
    message(STATUS "Build id: ${id}")
endif()
