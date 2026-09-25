juce_add_gui_app(StagePlotMixer
    PRODUCT_NAME "Stage Plot Mixer"
    COMPANY_NAME "Stage Plot Mixer contributors"
    BUNDLE_ID "org.stageplotmixer.app"
    VERSION ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Stage Plot Mixer needs audio input to mix and record your inputs.")

target_sources(StagePlotMixer PRIVATE
    Main.cpp
    AudioEngine.cpp
    MainComponent.cpp
    SimulatedAudioDevice.cpp
    ../ui/LevelMeter.cpp
)

target_compile_definitions(StagePlotMixer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StagePlotMixer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StagePlotMixer,JUCE_VERSION>"
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_ALSA=1
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
    SPM_VERSION="${PROJECT_VERSION}"
    SPM_GIT_HASH="${SPM_GIT_HASH}"
)

target_link_libraries(StagePlotMixer
    PRIVATE
        spm_audio
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
