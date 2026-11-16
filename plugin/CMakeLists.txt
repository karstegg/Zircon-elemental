if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt)
  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/JUCE ${CMAKE_BINARY_DIR}/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ZirconElemental
  COMPANY_NAME "Zircon Elemental Project"
  PLUGIN_MANUFACTURER_CODE Zrcn
  PLUGIN_CODE Ze01
  FORMATS VST3 Standalone
  PRODUCT_NAME "Zircon Elemental"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB ZIRCON_MACROS ${CMAKE_SOURCE_DIR}/library/macros/*.json)
file(GLOB ZIRCON_PATCHES ${CMAKE_SOURCE_DIR}/patches/*.json)
juce_add_binary_data(ZirconData SOURCES ${ZIRCON_MACROS} ${ZIRCON_PATCHES})

# Optional ASIO (ASIO4ALL etc.) for the Standalone: needs the Steinberg ASIO SDK.
#   cmake ... -DZIRCON_ASIO_SDK=C:/path/to/asiosdk
set(ZIRCON_ASIO_SDK "" CACHE PATH "Path to Steinberg ASIO SDK (enables ASIO in Standalone)")
if(ZIRCON_ASIO_SDK AND EXISTS "${ZIRCON_ASIO_SDK}/common/iasiodrv.h")
  message(STATUS "ASIO enabled: ${ZIRCON_ASIO_SDK}")
  target_compile_definitions(ZirconElemental PUBLIC JUCE_ASIO=1)
  target_include_directories(ZirconElemental PUBLIC
    "${ZIRCON_ASIO_SDK}/common" "${ZIRCON_ASIO_SDK}/host" "${ZIRCON_ASIO_SDK}/host/pc")
elseif(ZIRCON_ASIO_SDK)
  message(WARNING "ZIRCON_ASIO_SDK set but common/iasiodrv.h not found; ASIO disabled")
endif()

target_sources(ZirconElemental PRIVATE PluginProcessor.cpp PluginEditor.cpp StructureView.cpp Dx7Panel.cpp)
target_compile_definitions(ZirconElemental PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ZirconElemental
  PRIVATE zircon_core ZirconData
          juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
