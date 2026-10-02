## [0.1.0] - 2026-10-02 - Moon Engine Official Release

The first official release of Moon Engine, now based on **Friday Night Funkin' v0.9 Feature Preview 3**.

### Added

* Added **Android and Linux builds**.
* Added the new **Feature Preview 3 modding structure**.
* Added the **Mod Menu** for managing installed mods.
* Added the **Lua Script Editor**.
* Added the **Modchart Editor**, **Music Editor**, **Animation Editor**, and **Cosmic Editor**.
* Added **video recording** with `F5`.
* Added mobile controls and editor touch support.
* Added frame-time metrics and stutter detection.
* Added the JSON title screen system.

### Changed

* Updated Moon Engine to **Friday Night Funkin' v0.9 Feature Preview 3**.
* Updated the asset and modding systems to use the new structure.
* Switched the Lua backend to `linc_luajit`.
* Improved scripting and modding workflows.
* Improved fullscreen reliability.
* Moved asset hot reload from `F5` to `Ctrl+F5`.
* Reworked the Debug Menu, Pause Menu, Music Editor, and Animation Editor interfaces.
* Improved the Debug Display.

### Fixed

* Fixed Android Chart Editor crashes.
* Fixed crashes when exiting songs on Android.
* Fixed bitmap decoding errors that could cause null object reference crashes.
* Fixed online heartbeat and timer issues during state changes.
* Fixed Polymod not correctly loading Lua scripts.

### Notes

This section covers **Moon Engine-specific changes**. Changes inherited from the underlying Friday Night Funkin' base are documented separately.
