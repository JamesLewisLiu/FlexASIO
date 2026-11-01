add_executable(FlexASIOMixerTest mixer_test.cpp)
target_link_libraries(FlexASIOMixerTest PRIVATE FlexASIO_mixer tinytoml)
add_test(NAME mixer COMMAND FlexASIOMixerTest)

# Manual integration test: requires a real output device; never runs in CTest.
add_executable(FlexASIODriverTest driver_test.cpp)
target_link_libraries(FlexASIODriverTest PRIVATE FlexASIO_flexasio FlexASIO_portaudio)
