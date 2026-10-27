# DLBHop

DLBHop is a bunnyhop and directional air strafe tool for Deadlock on Windows x64. Hold Space to hop and strafe. Use the movement keys to choose a direction. DLBHop changes movement values. It does not turn the camera.

The launcher loads an internal DLL into Deadlock.

## Use

1. Extract the release ZIP to one folder.
2. Keep `deadlock-directional.exe` and `deadlock-directional-hold.dll` in the same folder.
3. Start Deadlock.
4. Run `Run.cmd`.
5. Hold **Space** to hop and strafe. Release **Space** to return to normal movement.

## Keys

| Key | Action |
| --- | --- |
| Space, held | Hop and strafe |
| F7 | Turn air strafe on or off |
| F8 | Pause or resume the controller |
| End | Stop the controller |

## Operation

In normal use, the controller operates only while Deadlock has focus. While the controller operates, it clears manual jump requests. It sends a jump only after ground contact.

Press **F8** to pause the controller. Space then works as normal.

Hold **Shift** to suspend automatic hop and strafe. DLBHop keeps them off for 650 ms after you release Shift.

When you stop the controller, DLBHop disables the hook. The DLL stays loaded until Deadlock exits.

## Compatibility

The launcher compares the SHA256 hash of `client.dll` with one known value. It rejects other builds. A game update can need a new release.

The DLL uses a command hook. It does not hide from anti-cheat software.

## Build

Install Visual Studio 2022 C++ build tools, MASM, and CMake. Open a terminal in the source folder. Run these commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable and DLL are in `build/Release`. Copy them to the folder that holds `Run.cmd`.

The MinHook notices are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
