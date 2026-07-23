
# About

A setup for distributed system nodes for linux servers.


# Building

### Prerequisites

Development with WSL

- WSL, VSCode (preferred). Follow the following linux instructions

Linux

- `apt-get` packages: g++, make, build-essentials


### Dev with Vscode

- Make sure you have , open a cmd prompt at the base path, type `bash` to log into wsl, then `code .`

- Make sure you can see a `WSL:Ubuntu` at the bottom left

- you can modify what to build in `.vscode/tasks.json` under the `"command"` keyword

- you can modify which executable to run in `.vscode/launch.json` under the `"program"` keyword

### Building

- Open a linux terminal in whatever ide you have, run `make`

- to clean `make clean`

### Outputs

- Built outputs should be located in the subfolder. You can place your config files there.


# Modifying

- You can add new services at the base `Makefile` by adding a subdir and source files. Polyglot languages are allowed as long as they have a makefile and the appropriate packages installed



# Features

## Sample

- Basic program forking 4 child process with the master process restarting them if they crash, ensuring fault tolerance.

- The 4 child processes then listen on port and handle 

## Utilities

- Logging

- Json Parser

- Timing mechanism

- Http parser
