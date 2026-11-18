# KittenTools
[![Build](https://github.com/project-sasayaki/kittentools/actions/workflows/build.yml/badge.svg)](https://github.com/project-sasayaki/kittentools/actions/workflows/build.yml)

KittenTools is a Windows compatibility loader that enables SEGA Lindbergh arcade games Initial D series to run natively on Windows with full network support, and the Namco System N2 Wangan Midnight Maximum Tune 3 series to run stand-alone or linked to other PCs.

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

## Settings

Settings are in `kitten/config.ini`, in the folder the game is started from. The file holds the settings of every supported game. KittenTools writes it again on every start, keeping the values set in it and adding any settings it lacks.

`kitten.exe --create` writes the file without starting a game.

## Wangan Midnight Maximum Tune 3 settings

The settings for Wangan Midnight Maximum Tune 3 / 3DX / 3DX+ are in the `[wmmt3]` section of `kitten/config.ini`: screen, coins, card, keys, gamepad, steering wheel, force feedback, sound, load times and dongles. The other sections are for the other games, except `[sysconf] log_level` and the H-pattern shifter below. `hasp2_serial`, the serial number of the second dongle, is used by 3DX+ only.

With an XInput gamepad the controller rumbles on the expressway's road joints, where the game gives the cabinet's wheel a kick (`xinput_rumble`, 0 = off; `xinput_rumble_ms`, the length of each pulse). That is the only force a gamepad reproduces: the wheel's centring, weight and side force have nothing to act on in a pad and are not sent to it. A steering wheel gets the joints through its normal force feedback.

The card reader is built in by default. To use one outside KittenTools instead, set `card_reader_port`:

- `COM3` (any COM port): a cabinet card reader on a serial line, for example through a USB-RS232 adapter. The port runs at the speed and parity the game sets (38400, even). The reader's own power and wiring are outside KittenTools.
- `\\.\pipe\<name>`: a card reader emulator running on this PC that creates that named pipe. Start it before or after the game; KittenTools connects when the game opens the reader and reconnects if the pipe breaks.

While nothing answers on that port or pipe, the game sees a reader that does not respond. With an external reader the card files, the insert key and the print images of the built-in reader are not used.

The buttons of an H-pattern shifter are `gear1` to `gear6` in `[dinput]` (DirectInput button numbers, 0 = none). They are read only with `[io3] mode = 2` and a steering wheel (`[wmmt3] input_mode = 2`); without them the shifter is moved one gear at a time with `dinput_shift_down` / `dinput_shift_up`.

## Wangan Midnight Maximum Tune 3 linked play

The game runs stand-alone by default. To link cabinets (one PC is one cabinet), set this in `kitten/config.ini` on every PC:

```ini
[wmmt3]
linked_play = true
; the LAN or VPN the cabinets share
link_subnet = 192.168.1.0/24
```

KittenTools uses this PC's address inside `link_subnet`, so the same file works on every PC. To name the address outright instead (for example when two adapters are in that network), set `link_ip = 192.168.1.23`; it takes precedence.

The game's own cabinet networking is used as it is, so the network has to look like the one between real cabinets:

- Every PC runs the same game version.
- All addresses are in the same /24: the first three numbers are equal and the last one differs. The game builds the other cabinets' addresses from its own.
- The network carries multicast (the cabinets find each other on 225.0.0.1). A LAN does, and so does a layer-2 VPN such as ZeroTier. Tailscale and plain WireGuard do not; Radmin VPN and Hamachi give addresses that differ in the first three numbers.
- UDP and TCP port 50765 are allowed through the firewall for `kitten.exe`.
- Each PC has its own PCB ID, set in the game's test mode.

## Credits

This project references and improves upon the following open-source projects and components.

### linuxloader

Parts of the project are referenced the implementation of linuxloader. 
- **Source**: <https://github.com/lindbergh-loader/linuxloader>
- **License**: [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

### Contributors

- [@lagswitch](https://github.com/MiG17IsMyLife): improvement on segaapi.
- [@puniru](https://github.com/ppuniru): improvement on AMD and INTEL graphic compatibility.
