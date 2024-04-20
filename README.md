# hyprwayland-scanner
A Hyprland implementation of wayland-scanner, in and for C++.

## Usage

```sh
hyprwayland-scanner '/path/to/proto' '/path/to/output/directory'
```

## Building

```sh
cmake -B build
cmake --build build -j`nproc`
```

### Installation

```sh
sudo cmake --install build
```
