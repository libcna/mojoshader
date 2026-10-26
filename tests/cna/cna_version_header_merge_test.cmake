# Retained regression for A3a2 A-01 / R4-03: the standalone build's generated mojoshader_version.h
# must name exactly HEAD, including when HEAD is a merge. The earlier `git rev-list HEAD~..` listed
# every commit a merge brings in -- several SHAs, one per line -- which the header then wrapped in a
# string literal, so the default build of a merge HEAD (the fork's cna branch) did not compile. The
# existing header test runs at the checkout's own HEAD, which is linear in CI, so it passed with
# that command restored; this test builds a repository whose HEAD is a merge, runs the real
# generation module against it, compiles the header it writes, and checks the changeset it names.
#
# Driven as: cmake -DMOJOSHADER_VERSION_MODULE=<module> -DTEST_CC=<cc> -DTEST_CC_IS_MSVC=<bool>
#                  -DTEST_WORKDIR=<dir> -P cna_version_header_merge_test.cmake

cmake_minimum_required(VERSION 3.12.0)

find_program(GIT git)
if(NOT GIT)
    message(STATUS "cna_version_header_merge: git not found; skipping")
    return()
endif()

set(repo "${TEST_WORKDIR}/merge-repo")
set(gendir "${TEST_WORKDIR}/gen")
set(isolation "${TEST_WORKDIR}/git-isolation")
file(REMOVE_RECURSE "${TEST_WORKDIR}")
file(MAKE_DIRECTORY "${repo}")
file(MAKE_DIRECTORY "${gendir}")

# R6-02: every git command below -- the fixture's and the generation module's -- runs with
# configuration of its own. Only this process's environment changes; nothing the developer has
# configured is touched. The user's and the system's git configuration are empty files here (a
# global merge.verifySignatures=true refused the unsigned side branch's merge, and a global
# core.hooksPath or init.templateDir would run the developer's hooks), the repository starts from
# an empty template, so it has no hooks, the identity is the fixture's, and a repository named by
# the calling environment (GIT_DIR and friends, as a git hook that runs ctest sets them) or found
# above the work directory is not used.
file(MAKE_DIRECTORY "${isolation}/home")
file(MAKE_DIRECTORY "${isolation}/template")
file(WRITE "${isolation}/empty.gitconfig" "")
foreach(_var GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR
             GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_CONFIG GIT_CONFIG_PARAMETERS
             GIT_CONFIG_COUNT)
    unset(ENV{${_var}})
endforeach()
set(ENV{HOME} "${isolation}/home")
set(ENV{XDG_CONFIG_HOME} "${isolation}/home/.config")
set(ENV{GIT_CONFIG_GLOBAL} "${isolation}/empty.gitconfig")
set(ENV{GIT_CONFIG_SYSTEM} "${isolation}/empty.gitconfig")
set(ENV{GIT_CONFIG_NOSYSTEM} "1")
set(ENV{GIT_TEMPLATE_DIR} "${isolation}/template")
set(ENV{GIT_CEILING_DIRECTORIES} "${TEST_WORKDIR}")
set(ENV{GIT_AUTHOR_NAME} "CNA Test")
set(ENV{GIT_AUTHOR_EMAIL} "test@example.com")
set(ENV{GIT_COMMITTER_NAME} "CNA Test")
set(ENV{GIT_COMMITTER_EMAIL} "test@example.com")

function(git_in)
    execute_process(COMMAND "${GIT}" ${ARGN} WORKING_DIRECTORY "${repo}"
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed: ${_err}")
    endif()
endfunction()

git_in(init -q -b main "--template=${isolation}/template")
git_in(config user.email "test@example.com")
git_in(config user.name "CNA Test")
git_in(config commit.gpgsign false)
git_in(config core.hooksPath "${isolation}/template")
file(WRITE "${repo}/a.txt" "a\n")
git_in(add a.txt)
git_in(commit -q -m base)
git_in(checkout -q -b side)
file(WRITE "${repo}/b.txt" "b\n")
git_in(add b.txt)
git_in(commit -q -m side)
git_in(checkout -q main)
file(WRITE "${repo}/c.txt" "c\n")
git_in(add c.txt)
git_in(commit -q -m main)
git_in(merge --no-ff -q -m merge side)

# HEAD must now be a merge, and `git rev-list HEAD~..` must list more than one commit -- the
# condition that broke the old generation. If this environment somehow made a linear HEAD, the
# test would not be exercising anything, so fail loudly.
execute_process(COMMAND "${GIT}" rev-parse HEAD WORKING_DIRECTORY "${repo}"
                OUTPUT_VARIABLE head_sha OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${GIT}" rev-list "HEAD~.." WORKING_DIRECTORY "${repo}"
                OUTPUT_VARIABLE revlist OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCHALL "[0-9a-f]+" revlist_shas "${revlist}")
list(LENGTH revlist_shas revlist_count)
if(revlist_count LESS 2)
    message(FATAL_ERROR "the fixture's HEAD is not a merge (git rev-list HEAD~.. gave ${revlist_count} commit(s))")
endif()

# Run the real generation module against the merge-HEAD repository.
include("${MOJOSHADER_VERSION_MODULE}")
mojoshader_write_version_header("${repo}" "${gendir}" "-1")

file(READ "${gendir}/mojoshader_version.h" header)
message(STATUS "generated header:\n${header}")

# Compile the generated header the way a standalone consumer does, and print the changeset it
# names. With the old multi-line command this source does not compile.
file(WRITE "${gendir}/consumer.c"
    "#include \"mojoshader_version.h\"\n"
    "#include <stdio.h>\n"
    "int main(void) { printf(\"%s\", MOJOSHADER_CHANGESET); return 0; }\n")

if(TEST_CC_IS_MSVC)
    execute_process(
        COMMAND "${TEST_CC}" /nologo "/I${gendir}" "/Fe${gendir}/consumer.exe"
                "/Fo${gendir}/consumer.obj" "${gendir}/consumer.c"
        RESULT_VARIABLE cc_rc OUTPUT_VARIABLE cc_out ERROR_VARIABLE cc_err)
    set(consumer_exe "${gendir}/consumer.exe")
else()
    execute_process(
        COMMAND "${TEST_CC}" "-I${gendir}" "${gendir}/consumer.c" -o "${gendir}/consumer"
        RESULT_VARIABLE cc_rc OUTPUT_VARIABLE cc_out ERROR_VARIABLE cc_err)
    set(consumer_exe "${gendir}/consumer")
endif()
if(NOT cc_rc EQUAL 0)
    message(FATAL_ERROR
        "the generated version header did not compile -- it does not name exactly one changeset:\n"
        "${cc_out}${cc_err}")
endif()

execute_process(COMMAND "${consumer_exe}" OUTPUT_VARIABLE changeset RESULT_VARIABLE run_rc)
if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR "the version-header consumer did not run")
endif()

if(NOT changeset STREQUAL "git-${head_sha}")
    message(FATAL_ERROR
        "MOJOSHADER_CHANGESET is \"${changeset}\", expected \"git-${head_sha}\" (exactly HEAD)")
endif()

message(STATUS "cna_version_header_merge: MOJOSHADER_CHANGESET names exactly the merge HEAD (${changeset})")
