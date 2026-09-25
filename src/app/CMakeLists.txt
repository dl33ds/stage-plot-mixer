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
    SessionController.cpp
    SimulatedAudioDevice.cpp
    StatusBar.cpp
    ../ui/GraphCanvas.cpp
    ../ui/Icons.cpp
    ../ui/Inspector.cpp
    ../ui/LevelMeter.cpp
    ../ui/MeterCache.cpp
    ../ui/NodeComponent.cpp
    ../ui/ParamFormat.cpp
    ../ui/QuickAddPanel.cpp
    ../ui/Theme.cpp
)

target_compile_definitions(StagePlotMixer PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StagePlotMixer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StagePlotMixer,JUCE_VERSION>"
    SPM_VERSION="${PROJECT_VERSION}"
    SPM_GIT_HASH="${SPM_GIT_HASH}"
)

file(GLOB SPM_ICON_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)

juce_add_binary_data(spm_resources
    NAMESPACE spm_resources
    HEADER_NAME SpmResources.h
    SOURCES
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf
        ${SPM_ICON_FILES})

target_link_libraries(StagePlotMixer PRIVATE spm_model spm_resources)
