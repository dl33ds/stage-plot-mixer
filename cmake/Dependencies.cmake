# Third-party dependencies, fetched at configure time and pinned to exact versions.

include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# JUCE (AGPLv3). Includes the ASIO 2.3 headers (Steinberg, dual proprietary/GPLv3).
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

if(SPM_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()
