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
