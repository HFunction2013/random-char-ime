# RandomChar IME

A Windows input method editor that replaces **every character you type** with a
**random character of the same category**.

| You press         | You get                          |
|-------------------|----------------------------------|
| Uppercase letter  | Random uppercase letter (A-Z)    |
| Lowercase letter  | Random lowercase letter (a-z)    |
| Digit             | Random digit (0-9)               |
| Symbol            | Random symbol (! @ # ...)        |

Caps Lock and Num Lock are fully respected. Ctrl / Alt / Win modifier
combinations (shortcuts) pass through unchanged so your system and
applications keep working normally.

---

## How it works

The application installs a **low-level keyboard hook** (`WH_KEYBOARD_LL`) that
intercepts every keystroke system-wide. For each character-producing key:

1. It determines the effective character category based on the virtual-key code
   plus the current modifier and toggle state (Shift, Caps Lock, Num Lock).
2. It generates a random character from that same category.
3. It injects the random character via `SendInput` with `KEYEVENTF_UNICODE`
   and swallows the original keystroke.

Injected keys are tagged with `LLKHF_INJECTED`, which the hook recognises and
passes straight through — preventing an infinite feedback loop.

The application runs from the **system tray** with no visible window.

---

## Features

- **Category-preserving randomization** — letters stay letters, digits stay
  digits, symbols stay symbols.
- **Caps Lock aware** — `CapsLock + a` produces a random uppercase letter;
  `CapsLock + Shift + a` produces a random lowercase letter.
- **Num Lock aware** — numpad keys produce random digits only when Num Lock is
  on; otherwise they behave as normal navigation keys.
- **Shortcut-safe** — any key pressed while Ctrl, Alt, or Win is held passes
  through untouched (Ctrl+C, Alt+Tab, Win+R, etc.).
- **Tray icon controls** — right-click for enable/disable, auto-start, about,
  and exit. Double-click toggles randomization on/off.
- **Single instance** — only one copy runs at a time.
- **MSI installer** — installs to Program Files, creates Start Menu and Desktop
  shortcuts, supports clean uninstall.

---

## Building

### Prerequisites

- Windows 10 / 11
- Visual Studio 2022 (or Build Tools for Visual Studio 2022) with the
  **Desktop development with C++** workload
- [WiX Toolset v3.11](https://wixtoolset.org/releases/) (for the MSI)

### Build the EXE

Open `RandomCharIME.sln` in Visual Studio and build the **Release | Win32**
configuration, or run from a Developer Command Prompt:

```cmd
msbuild RandomCharIME.sln /p:Configuration=Release /p:Platform=Win32 /m
```

The executable is produced at `bin\Win32\Release\RandomCharIME.exe`.

### Build the MSI

```cmd
set "WIX_BIN=C:\Program Files (x86)\WiX Toolset v3.11\bin"
if not exist build mkdir build
"%WIX_BIN%\candle.exe" -nologo -dBinDir=bin\Win32\Release installer\Product.wxs -out build\
"%WIX_BIN%\light.exe"  -nologo -out build\RandomCharIME.msi build\Product.wixobj
```

---

## Continuous Integration (GitHub Actions)

Pushing to `main` or `master` automatically triggers the workflow defined in
`.github/workflows/build.yml`, which:

1. Sets up MSBuild via `microsoft/setup-msbuild`.
2. Installs WiX Toolset v3 via Chocolatey (does not rely on a pre-installed
   copy).
3. Builds the EXE with MSBuild (Release | Win32).
4. Compiles the WiX source into an MSI installer.
5. Uploads both the EXE and MSI as build artifacts.

---

## Installation

Run `RandomCharIME.msi` and follow the wizard. The application is installed to
`C:\Program Files (x86)\RandomChar IME\` by default, with shortcuts added to
the Start Menu and Desktop.

To uninstall, use **Settings > Apps > Installed apps** or the MSI's
**Remove** option.

---

## Usage

1. Launch **RandomChar IME** from the Start Menu, Desktop shortcut, or the
   installed EXE.
2. A shield icon appears in the system tray. Randomization is **on** by
   default.
3. Start typing anywhere — every character is replaced with a random character
   of the same category.
4. **Double-click** the tray icon to toggle randomization on/off.
5. **Right-click** the tray icon for:
   - **Enable Randomization** — toggle on/off
   - **Start with Windows** — launch automatically at logon
   - **About** — version and description
   - **Exit** — close the application

---

## Project structure

```
random-char-ime/
├── .github/
│   └── workflows/
│       └── build.yml              # GitHub Actions CI pipeline
├── installer/
│   └── Product.wxs                # WiX MSI installer definition
├── src/
│   ├── main.c                     # IME core (hook + tray app)
│   ├── RandomCharIME.vcxproj      # Visual Studio C++ project
│   └── RandomCharIME.vcxproj.filters
├── RandomCharIME.sln              # Visual Studio solution
└── README.md                      # This file
```

---

## License

This project is provided as-is for educational and personal use.
