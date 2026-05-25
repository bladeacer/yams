<!-- 1. debug build flag -->
<!--  DONE: NDEBUG toggle via meson_options.txt 'logging' option.  -->

<!-- 2. proper tests instead of fucked up shell scripts around other CLI tools -->
<!--  DONE: Tests using cmocka (test_metadata, test_config, test_utils).  -->

<!-- 3. wrap nix flakes around all the tools needed (playerctl, hyperfine, whatever) -->
<!--  DONE: flake.nix provides meson, cmake, ncurses, ninja, playerctl, ffmpeg, hyperfine, chafa.  -->

<!-- 4. separate .c and .h files into src/*.c and lib/*.h -->
<!--  DONE: Headers in include/, sources in src/, library code in lib/.  -->

1. hopefully move off CLI calls, just API to mpris which is p doable and can be its own small C library too
5.5. add in functionality to copy nowplaying-cli functionality, create a PlayerBackend that has a struct and some function pointers
