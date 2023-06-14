# Changelog

## [0.0.2] - 2023-06-14
### Added
- Allow static linking
- Logging with spdlog as alternative to loguru
- Allow compilation into library

### Changed
- Move pson/json conversion functions from nlohmann to protoson namespace
- Added option to CMake for enabling Loguru logging library
- Property stream filtering by device id
- Integration of json without the need for httplib
