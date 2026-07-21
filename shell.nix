# Please Note I have a very limited understanding of the inner workings
# of certain things here I am just using what I have seen necessary for
# my application feel free to argue or change that as much as you please

{ pkgs ? import <nixpkgs> {} }:

let
  # use nixos-rebuild to get the system config
  systemConfig = (import <nixpkgs/nixos> {}).config;
  kernelPackages = systemConfig.boot.kernelPackages;
in

pkgs.mkShell {
  # Nix's ld-wrapper refuses to link against anything outside /nix/store,
  # to enforce build purity. Cargo build scripts (used here by Slint's
  # Corrosion-based build) write intermediate objects into the project
  # directory, which trips that check with "impure path ... used in link".
  # This only matters for local dev shells, not real Nix derivation builds,
  # so it's safe to disable here.
  NIX_ENFORCE_PURITY = 0;

  packages = with pkgs; [
    gcc
    cmake

    stdenv.cc
    stdenv.cc.cc

    # Profiling
    flamegraph
      kernelPackages.perf # Needed By FlameGraph

    #? Slint GUI (src/main_gui.cpp + ui/*.slint)
    #? Slint's C++ API is backed by its Rust runtime -- CMake's
    #? FetchContent step builds it via Corrosion, which needs a working
    #? cargo/rustc on PATH. There's no plain `slint` nixpkg for this
    #? (only slint-lsp, the editor language server), so this is the
    #? actual dependency, not a placeholder.
    rustc
    cargo

    #? Slint text rendering needs to locate fontconfig via pkg-config at
    #? build time (yeslogic-fontconfig-sys). Neither was previously listed.
    fontconfig
    pkg-config

    #? Slint's winit backend dlopen()s these at *runtime* (not link time),
    #? so nixpkgs -- which doesn't put shared libs on a standard system
    #? search path -- needs them exported via LD_LIBRARY_PATH below, not
    #? just listed here. They're listed here too so makeLibraryPath can
    #? find them.
    wayland          # libwayland-client.so -- the actual missing lib
    libxkbcommon     # keyboard handling, winit needs this alongside wayland
    libGL            # GL context creation for Slint's renderer
    freetype         # font rasterization, pairs with fontconfig above

    #? Ninja is Slint's recommended CMake generator -- faster builds and
    #? correct .slint dependency tracking (pass -GNinja when configuring)
    ninja
  ];

  nativeBuildInputs = with pkgs; [
    #?  compiler
    gcc

    #?  Testing
    gtest
    coreutils-prefixed
    #(This includes gtest)

    #? Build System
    cmake

    #?LSP
    clang-tools #it has clangd
   ];

  #? Let me Explain why is this needed
  #? perf utility isn't a package in fact it's a kernel utility in linux
  #? Also it has kernel parameter
  #! /proc/sys/kernel/perf_event_paranoid
  #? And for
  #! /proc/sys/kernel/kptr_restrict
  #? it's to profile Kernel Calls

  #* In A technical sense of security you would
  #* never allow this on a "production" or "daily driver" machine
  #* As I am very conscious of this yet i need it for profiling
  #* A temporary solution would be enabling them in a shell temporarily then re-disabling them
  #* Sadly this can't be done in a shell and instead done system-wide so i need you to realize understand and evaluate
  #* the Risks comes with this

  shellHook = "
    unset MAKEFLAGS

    export LD_LIBRARY_PATH=${pkgs.lib.makeLibraryPath [
      pkgs.wayland
      pkgs.libxkbcommon
      pkgs.libGL
      pkgs.fontconfig
      pkgs.freetype
    ]}:\$LD_LIBRARY_PATH

    ${builtins.readFile ./Scripts/kernel_security_bypass.sh}
    ${builtins.readFile ./Scripts/build_release.sh}
    ${builtins.readFile ./Scripts/build_profiling.sh}

  ";

    # ${builtins.readFile ./Scripts/profile.sh}

}