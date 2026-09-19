# 1. Prepare dependencies (Ubuntu / Bash; install uv and ESP-IDF 6.1.0 first)

```sh
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build protobuf-compiler pkg-config libffi-dev libssl-dev libusb-1.0-0-dev libudev-dev
cd ~/trezor-firmware
git submodule update --init vendor/nanopb vendor/secp256k1-zkp vendor/QR-Code-generator vendor/ts-tvl
uv sync
```

# 2. Generate code (use a terminal without IDF activated)

```sh
cd ~/trezor-firmware
uv run make -C legacy/firmware/protob -j1 -B BITCOIN_ONLY=0
uv run python common/tools/cointool.py render legacy/firmware
```

# 3. Build the firmware

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
cd ~/trezor-firmware/legacy/ports/esp32s3
idf.py -B build-ubuntu reconfigure
idf.py -B build-ubuntu build
idf.py -B build-ubuntu size
```

# 4. Run host tests (open a new terminal without IDF activated)

```sh
cd ~/trezor-firmware/legacy/ports/esp32s3
cmake -S tests -B tests/build-ubuntu -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_LINKER=/usr/bin/ld
cmake --build tests/build-ubuntu
ctest --test-dir tests/build-ubuntu --output-on-failure
```

# 5. Flash the firmware (use BOOT/RESET to enter download mode; replace PORT with the serial port)

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
cd ~/trezor-firmware/legacy/ports/esp32s3
idf.py -B build-ubuntu -p PORT flash
```
