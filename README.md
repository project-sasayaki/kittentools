# KittenTools

KittenTools is a Windows compatibility loader that enables SEGA Lindbergh arcade games Initial D series to run natively on Windows with full network support, and the Namco System N2 Wangan Midnight Maximum Tune 3 series to run stand-alone.

# Supported Games
- Initial D Arcade Stage 4 Rev.A (SBML)
- Initial D Arcade Stage 4 Rev.B (SBML)
- Initial D Arcade Stage 4 kai Rev.G (SBML)
- Initial D Arcade Stage 5 Rev.A (SBQZ)
- Initial D Arcade Stage 5 Rev.C (SBQZ)
- Initial D Arcade Stage 5 EXP 2.0 Rev.A (SBTS)
- Wangan Midnight Maximum Tune 3 JPN/EXP (WM3100-1/3 A70)
- Wangan Midnight Maximum Tune 3DX JPN/EXP (W3X100-1/4 C09)
- Wangan Midnight Maximum Tune 3DX+ JPN/EXP (W3P100-1/2 B02)
- Wangan Midnight Maximum Tune 3DX+ CHN (W3P100-5 A16)

## Requirements

- **MSYS2** with MinGW32 toolchain and `make`, `python3`
- **CMake** >= 3.14
- **MinGW32** packages: `mingw-w64-i686-toolchain`, `mingw-w64-i686-cmake`
## Runtime Requirements

The following runtime libraries are required:

- `cg.dll`
- `cgGL.dll`

These libraries can be obtained by installing either of the following NVIDIA Cg Toolkit versions:

- **[D4] NVIDIA Cg Toolkit 1.4.1**
  - Download: [Cg-1.4.1_Setup.exe](https://download.nvidia.com/developer/cg/Cg_1.4/1.4.1/Cg-1.4.1_Setup.exe)

- **[D5] NVIDIA Cg Toolkit 1.5.0019**
  - Download: [Cg-1.5_Setup.exe](https://developer.download.nvidia.com/cg/Cg_1.5/1.5.0/Cg-1.5_Setup.exe)

Wangan Midnight Maximum Tune 3 / 3DX / 3DX+ also need `OpenAL32.dll` (32-bit) for sound; without it the game runs silent.

- **OpenAL 1.1 Windows Installer**
  - Download: [oalinst.zip](https://www.openal.org/downloads/oalinst.zip)
- Alternatively, [OpenAL Soft](https://openal-soft.org/): place its 32-bit `soft_oal.dll` next to `kitten.exe` as `OpenAL32.dll`, or point `[wmmt3] openal_dll` at it.

## Building

**Step 1 — Install dependencies**

Run the automated setup script:

```bat
scripts\install_dependency.bat
```

This downloads and installs MSYS2 to `C:\msys64`, then installs the MinGW32 toolchain and CMake.

**Step 2 — Build**

```bat
scripts\build.bat
```

This configures CMake with the MinGW32 toolchain and builds the project. The output binary is placed in `win-release\`.

## Usage

```
kitten.exe --binary <elf_file> [ELF arguments]
```

## Credits

This project references and improves upon the following open-source projects and components.

### linuxloader

Parts of the project are referenced the implementation of linuxloader. 
- **Source**: <https://github.com/lindbergh-loader/linuxloader>
- **License**: [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

### Contributors

- [@lagswitch](https://github.com/MiG17IsMyLife): improvement on segaapi.
- [@puniru](https://github.com/ppuniru): improvement on AMD and INTEL graphic compatibility.