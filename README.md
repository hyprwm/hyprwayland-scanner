# hyprwayland-scanner
A Hyprland implementation of wayland-scanner, in and for C++.

hw-s automatically generates properly RAII-ready, modern C++ bindings for Wayland protocols, for
either servers or clients.

## Usage

```sh
hyprwayland-scanner '/path/to/proto' '/path/to/output/directory'
```

### Options

- `--client` -> generate client code
- `--wayland-enums` -> use wayland enum naming (snake instead of camel)

## Dependencies

Requires a compiler with C++23 support.

Dep list:
 - pugixml

## Building

```sh
cmake -DCMAKE_INSTALL_PREFIX=/usr -B build
cmake --build build -j `nproc`
```

### Installation

```sh
sudo cmake --install build
```
