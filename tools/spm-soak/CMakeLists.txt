add_executable(spm-soak Main.cpp)
target_link_libraries(spm-soak PRIVATE spm_audio)

if(SPM_BUILD_TESTS)
    # A short soak as part of the test suite; run the tool by hand for the full hour.
    add_test(NAME soak_short COMMAND spm-soak --minutes 2)
endif()
