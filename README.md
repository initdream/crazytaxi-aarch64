# crazytaxi-aarch64

Port of Crazy Taxi Classic (Android aarch64) to Linux aarch64

## Requirements

- An aarch64 environment
- Game files from a legitimate Android copy of Crazy Taxi Classic

## Build

```bash
mkdir build
cd build
cmake ..
make
```

## Run

```bash
./crazitaxi
```

## Credits

* **[max_arm64](https://github.com/orktes/max_arm64)** by [@orktes](https://github.com/orktes)
  * For the proof of concept of the .so loader architecture.

* **[lswtcs_arm64](https://github.com/mtojek/lswtcs_arm64)** by [@mtojek](https://github.com/mtojek)
  * For some useful files and general structure.

* **[nextos_ports_android](https://github.com/felc18-blip/nextos_ports_android)** by [@felc18-blip](https://github.com/felc18-blip)
  * For the framework and template to build off of.

* **[BinaryCounter](https://github.com/binarycounter)**
    * For not releasing their version of this port, making me do this entire thing.

## Legal

You must own a legitimate copy of the game to use this port.
