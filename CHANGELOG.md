# Changelog

## [1.2.2] - 2023-10-03
### Fix
- Bug in installer not allowing setting of credentials

## [1.2.1] - 2023-10-03
### Changed
- Change module name to thinger\_iotmp

### Fixed
- Installer returning empty device id when network connection set by ppp interface

## [1.2.0] - 2023-09-28
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

[1.2.2]: https://github.com/thinger-io/IOTMP-Linux/compare/1.2.1...1.2.2
[1.2.1]: https://github.com/thinger-io/IOTMP-Linux/compare/1.2.0...1.2.1
[1.2.0]: https://github.com/thinger-io/IOTMP-Linux/compare/1.1.0...1.2.0
[1.1.0]: https://github.com/thinger-io/IOTMP-Linux/compare/1.0.0...1.1.0
[1.0.0]: https://github.com/thinger-io/IOTMP-Linux/tag/1.0.0
