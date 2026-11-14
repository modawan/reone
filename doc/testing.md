# Testing

## Unit Tests

- Install GoogleTest with GoogleMock and configure CMake project with `-DBUILD_TESTS=ON` (enabled by default)
- Build tests: `cmake --build build --config RelWithDebInfo --target tests`
- From the build directory (`cd build`), run unit tests: `ctest -VV -C RelWithDebInfo`

For single-configuration generators, also set `-DCMAKE_BUILD_TYPE=RelWithDebInfo` when configuring.

## Functional Testing

- Pick functionality or a module to test
- Examine game resources in the toolkit application
- Play using vanilla engine with saved games and cheat codes to determine expected behaviour
- Play reone in developer mode with console commands to verify behaviour
