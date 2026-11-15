if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt)
  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/JUCE ${CMAKE_BINARY_DIR}/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(PrimalModular
  COMPANY_NAME "Primal Modular Project"
  PLUGIN_MANUFACTURER_CODE Prml
  PLUGIN_CODE Pm01
  FORMATS VST3 Standalone
  PRODUCT_NAME "Primal Modular"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB PRIMAL_MACROS ${CMAKE_SOURCE_DIR}/library/macros/*.json)
file(GLOB PRIMAL_PATCHES ${CMAKE_SOURCE_DIR}/patches/*.json)
juce_add_binary_data(PrimalData SOURCES ${PRIMAL_MACROS} ${PRIMAL_PATCHES})

target_sources(PrimalModular PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_compile_definitions(PrimalModular PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PrimalModular
  PRIVATE primal_core PrimalData
          juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
