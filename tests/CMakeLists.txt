add_executable(spm_tests
    CallbackStatsTests.cpp
    DelayFinderTests.cpp
    EffectTests.cpp
    EngineTests.cpp
    GraphTests.cpp
    NodeTests.cpp
    RecordTests.cpp
)

target_link_libraries(spm_tests PRIVATE spm_core spm_audio spm_record Catch2::Catch2WithMain)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(spm_tests)

# Tests that need JUCE (the session model).
add_executable(spm_model_tests SessionTests.cpp GroupTests.cpp)
target_link_libraries(spm_model_tests PRIVATE spm_model Catch2::Catch2WithMain)
catch_discover_tests(spm_model_tests)
