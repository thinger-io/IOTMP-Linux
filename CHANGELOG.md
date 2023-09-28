# Changelog

## [1.2.0] - 2023-08-17
### Added
- Add new THINGER\_STREAMS\_READY state listener
- Install script and service file

## [1.1.0] - 2023-06-28
### Added
- Allow static linking
- Logging with spdlog as alternative to loguru
- Allow compilation into library
- Bin extension in CMakeLists

### Changed
- Move pson/json conversion functions from nlohmann to protoson namespace
- Added option to CMake for enabling Loguru logging library
- Integration of json without the need for httplib

### Removed
- Unmaintained loguru library

## [1.0.0] - 2023-09-03
- First version
