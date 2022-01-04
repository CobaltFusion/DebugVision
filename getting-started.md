# tools

## clang-format

on most linux distros clang-format can be installed using the package manager of the distribution
for windows, it can be downloaded here: https://llvm.org/builds/ and https://prereleases.llvm.org/win-snapshots/clang-format-6923b0a7.exe

Its recommended to configure your editor to format-on-save, that way, we completely avoid discussion and merge-conflicts involving whitespace.

## .editorconfig

see https://editorconfig.org/

A widely supported standard to auto-configure your editor to the right tabs/spaces/indentation settings for different file types.

## pitch fork layout

see https://github.com/vector-of-bool/pitchfork and https://api.csswg.org/bikeshed/?force=1&url=https://raw.githubusercontent.com/vector-of-bool/pitchfork/develop/data/spec.bs


Basically we follow this pattern; where each executable and library has its own directory and contains "src" for its sources, "include" for headers that are exported from the library and tests for, well, tests ;)

```
+---libs
|   \---mylibrary
|       +---include
|       +---src
|       \---tests
\---tools
    \---debug.vision.application
        +---include
        +---src
        \---tests
```
