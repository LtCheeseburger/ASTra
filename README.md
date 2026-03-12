# Project ASTra (ASTra monorepo bootstrap)

This is the **initial skeleton** for Project ASTra / ASTra:
- C++20 + CMake + Ninja
- Qt 6 Widgets GUI (`gf_toolsuite_gui`)
- CLI (`gf_cli`) for CI/batch workflows
- spdlog + nlohmann/json via FetchContent
- Catch2 tests
- Early cross-platform CI scaffolding (GitHub Actions)

## Quick start (Windows 11)

### Prereqs
- Visual Studio 2022 Build Tools or full VS 2022 (Desktop development with C++)
- CMake (3.24+)
- Ninja
- Qt 6.6+ (MSVC build)
- Git
- VSCode + CMake Tools extension

### Configure / Build
```powershell
cmake -S . -B out-win -G Ninja -DGF_BUILD_TESTS=ON -DGF_BUILD_GUI=ON
cmake --build out-win
ctest --test-dir out-win --output-on-failure
```

### Run
```powershell
out-win\apps\gf_cli\gf_cli.exe --version
out-win\apps\gf_toolsuite_gui\gf_toolsuite_gui.exe
```

## Notes
- Non-Qt deps are pinned via FetchContent for reproducible builds.
- Qt is a system dependency (installed separately). Make sure CMake can find it.
  - If needed: `-DCMAKE_PREFIX_PATH="C:\Qt\6.6.3\msvc2019_64"`

## Next steps
- Add `gf_io` safe-write + backup primitives
- Add `gf_ast` reader/writer interfaces + safety rules
- Wire golden tests (hash/size stable first, then parse/extract/repack)

