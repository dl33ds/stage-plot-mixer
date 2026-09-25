add_executable(spm_tests
    CallbackStatsTests.cpp
    DelayFinderTests.cpp
    EngineTests.cpp
    GraphTests.cpp
    NodeTests.cpp
)

target_link_libraries(spm_tests PRIVATE spm_core spm_audio Catch2::Catch2WithMain)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(spm_tests)
