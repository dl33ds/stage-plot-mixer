# spm_core: framework-independent building blocks shared by the app and tools.
add_library(spm_core STATIC
    core/CallbackStats.cpp
    core/DelayFinder.cpp
)
target_include_directories(spm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(spm_core PUBLIC cxx_std_20)

# spm_platform: OS-specific queries behind a portable interface.
if(WIN32)
    add_library(spm_platform STATIC platform/HardwareInfo_windows.cpp)
    target_link_libraries(spm_platform PRIVATE setupapi cfgmgr32 powrprof advapi32)
    target_compile_definitions(spm_platform PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_library(spm_platform STATIC platform/HardwareInfo_other.cpp)
endif()
target_include_directories(spm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(spm_platform PUBLIC cxx_std_20)

# spm_audio: the framework-independent real-time engine, node types and graph builder.
add_library(spm_audio STATIC
    engine/CompiledGraph.cpp
    engine/EngineCore.cpp
    engine/GraphHandoff.cpp
    nodes/BuiltInNodes.cpp
    graph/GraphBuilder.cpp
    graph/GraphRules.cpp
)
target_include_directories(spm_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(spm_audio PUBLIC cxx_std_20)
target_link_libraries(spm_audio PUBLIC spm_core)

# spm_juce: the JUCE modules, compiled once and shared by the app and the model tests.
add_library(spm_juce STATIC)
target_link_libraries(spm_juce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
target_compile_definitions(spm_juce
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ALSA=1
        $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
        JUCE_MODAL_LOOPS_PERMITTED=0
    INTERFACE
        $<TARGET_PROPERTY:spm_juce,COMPILE_DEFINITIONS>)
target_include_directories(spm_juce INTERFACE $<TARGET_PROPERTY:spm_juce,INCLUDE_DIRECTORIES>)
set_target_properties(spm_juce PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

# spm_model: the session document (ValueTree + undo, JSON files).
add_library(spm_model STATIC model/Session.cpp model/Templates.cpp)
target_include_directories(spm_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spm_model PUBLIC spm_audio spm_juce)

add_subdirectory(app)
