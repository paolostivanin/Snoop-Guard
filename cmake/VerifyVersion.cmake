file(READ "${SOURCE_DIR}/CHANGELOG.md" changelog)
string(FIND "${changelog}" "## ${EXPECTED_VERSION}" changelog_match)
if(changelog_match EQUAL -1)
    message(FATAL_ERROR
        "CHANGELOG.md has no heading for version ${EXPECTED_VERSION}")
endif()

execute_process(
    COMMAND git describe --exact-match --tags HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE tag_result
    OUTPUT_VARIABLE current_tag
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(tag_result EQUAL 0)
    string(REGEX REPLACE "^v" "" tag_version "${current_tag}")
    if(NOT tag_version STREQUAL EXPECTED_VERSION)
        message(FATAL_ERROR
            "Git tag ${current_tag} does not match ${EXPECTED_VERSION}")
    endif()
endif()
