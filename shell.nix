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
  packages = with pkgs; [
    gcc
    cmake
    gnumake

    stdenv.cc
    stdenv.cc.cc

    # Profiling
    flamegraph
      kernelPackages.perf # Needed By FlameGraph
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

  # Ensure CMake can find the compilers
  CMAKE_C_COMPILER = "${pkgs.gcc}/bin/gcc";
  CMAKE_CXX_COMPILER = "${pkgs.gcc}/bin/g++";

  shellHook = ''
    # Export compiler paths explicitly
    export CC="${pkgs.gcc}/bin/gcc"
    export CXX="${pkgs.gcc}/bin/g++"

    ${builtins.readFile ./Scripts/kernel_security_bypass.sh}
    ${builtins.readFile ./Scripts/build_release.sh}
    ${builtins.readFile ./Scripts/build_profiling.sh}

  '';

    # ${builtins.readFile ./Scripts/profile.sh}

}