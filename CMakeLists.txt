cmake_minimum_required(VERSION 3.13)
# Currently the minimum supported version to run CMake on is Debian
# Buster. That only has CMake 3.13.4 in it's offical package system.
# Backports has 3.16.3 if absolutely needed. After that we have to make
# a concious decision to upgrade to bump the minimum version to Bullseye
# (or we could choose an even more aggressive stance where we just
# specify how many years of CMake releases we support rather than
# tracking the minimum version in the package system of a single Linux
# distribution).
project("Cap'n Proto Root" CXX)
add_subdirectory(c++)
