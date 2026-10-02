# Moon Engine Launcher

The **official launcher for Moon Engine**.

Moon Engine Launcher is the official application for discovering, installing, managing, and launching Moon Engine content.

The launcher is designed to provide a centralized way for users to manage their Moon Engine installation without having to manually download and organize files.

## Features

* Moon Engine game management
* Mod downloading and installation
* Game launching
* Launcher news and information
* Moon Engine version management
* Launcher Modding support
* Automatic handling of required launcher files
* Windows support

## Launcher Modding

Moon Engine Launcher supports **Launcher Modding**, allowing developers to customize parts of the launcher without modifying the launcher source code directly.

Launcher mods can be used to customize supported launcher content such as:

* Launcher pages
* UI elements
* Assets
* Launcher metadata
* Scripts
* Custom launcher components

Launcher Modding is separate from **Moon Engine game modding**.

For documentation about creating launcher mods, see the **Launcher Modding** documentation.

---

# Building the Launcher

## Requirements

To build Moon Engine Launcher for Windows, you need:

* Windows
* [MSYS2](https://www.msys2.org/)
* The **UCRT64** environment
* `g++`
* The required project dependencies

The launcher expects the compiler to be available at:

```text
C:\msys64\ucrt64\bin\g++.exe
```

---

# Windows Build

### 1. Install MSYS2

Install MSYS2 and make sure the **UCRT64** environment is available.

The default installation location should be:

```text
C:\msys64
```

### 2. Open the project directory

Open a terminal in the Moon Engine Launcher project directory.

### 3. Run the build script

Execute:

```bat
build.bat
```

The build script handles the compilation process and prepares the launcher for distribution.

### 4. Find the executable

After a successful build, the launcher will be generated at:

```text
export/windows/release/bin/Moon Launcher.exe
```

### 5. Required DLLs

The required DLL files are automatically copied to the same directory as the executable.

This allows the launcher to run without requiring the user to manually install the required runtime dependencies.

The final directory will look similar to:

```text
export/
└── windows/
    └── release/
        └── bin/
            ├── Moon Launcher.exe
            ├── required.dll
            └── ...
```

---

# Troubleshooting

## Compiler Not Found

If the build script cannot find the compiler, verify that the following file exists:

```text
C:\msys64\ucrt64\bin\g++.exe
```

If it does not exist, make sure MSYS2's **UCRT64** environment and compiler packages are installed correctly.

## Build Fails Immediately

Check that:

1. MSYS2 is installed.
2. The UCRT64 environment is installed.
3. `g++.exe` exists.
4. You are running `build.bat` from the project directory.
5. The project dependencies are available.

## Launcher Does Not Start

If the executable was successfully built but does not start, check that the required DLL files were copied to:

```text
export/windows/release/bin/
```

The executable and its required DLLs should normally remain in the same directory.

---

# Project Structure

The launcher project contains the source code and resources required to build the official Moon Engine Launcher.

The generated release build is located at:

```text
export/windows/release/bin/
```

The `export` directory contains generated build files and should not be treated as the main source directory.

---

# Contributing

Contributions to Moon Engine Launcher are welcome.

Before making major changes, make sure your changes are compatible with the launcher architecture and existing Moon Engine systems.

When submitting changes:

* Keep the launcher buildable.
* Test the Windows build.
* Avoid unnecessary changes to unrelated systems.
* Document new launcher modding functionality.
* Test Launcher Modding changes when applicable.

---

# Moon Engine

Moon Engine is a Friday Night Funkin' engine fork focused on expanding the modern V-Slice/Funkin' modding experience with additional scripting, development tools, and engine features.

The launcher is a separate application designed to make installing and managing Moon Engine content easier.

---

# License

See the project's license files for licensing information.
