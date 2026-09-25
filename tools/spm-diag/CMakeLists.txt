juce_add_console_app(spm-diag
    PRODUCT_NAME "spm-diag"
    VERSION ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(spm-diag PRIVATE
    Main.cpp
    Console.cpp
    DeviceSurvey.cpp
    DeviceTests.cpp
)

target_compile_definitions(spm-diag PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_ALSA=1
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
    SPM_VERSION="${PROJECT_VERSION}"
    SPM_GIT_HASH="${SPM_GIT_HASH}"
)

target_link_libraries(spm-diag
    PRIVATE
        spm_core
        spm_platform
        juce::juce_audio_devices
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
