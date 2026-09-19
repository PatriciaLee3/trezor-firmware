set(PORT_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(REPO_ROOT "${PORT_DIR}/../../../.." ABSOLUTE)
set(LEGACY "${REPO_ROOT}/legacy")
set(FIRMWARE "${LEGACY}/firmware")
set(CRYPTO "${REPO_ROOT}/crypto")
set(STORAGE "${REPO_ROOT}/storage")
set(NANOPB "${REPO_ROOT}/vendor/nanopb")
set(ZKP "${REPO_ROOT}/vendor/secp256k1-zkp")
set(PROTO_GENERATED "${FIRMWARE}/protob")

set(PORT_SOURCES
    "app_main.c"
    "port_logic.c"
    "esp32s3_display.c"
    "esp32s3_runtime.c"
    "esp32s3_storage.c"
    "esp32s3_norcow.c"
    "usbd_compat.c")

set(LEGACY_COMMON_SOURCES
    "${LEGACY}/common.c"
    "${LEGACY}/fw_signatures.c"
    "${LEGACY}/gen/bitmaps.c"
    "${LEGACY}/gen/fonts.c"
    "${LEGACY}/layout.c"
    "${LEGACY}/oled.c"
    "${LEGACY}/random_delays.c"
    "${LEGACY}/time_estimate.c"
    "${LEGACY}/usb21_standard.c"
    "${LEGACY}/util.c"
    "${LEGACY}/webusb.c"
    "${LEGACY}/winusb.c"
    "${STORAGE}/flash_area.c")

set(FIRMWARE_SOURCES
    "${FIRMWARE}/messages.c"
    "${FIRMWARE}/config.c"
    "${FIRMWARE}/trezor.c"
    "${FIRMWARE}/pinmatrix.c"
    "${FIRMWARE}/fsm.c"
    "${FIRMWARE}/coins.c"
    "${FIRMWARE}/coin_info.c"
    "${FIRMWARE}/transaction.c"
    "${FIRMWARE}/protect.c"
    "${FIRMWARE}/layout2.c"
    "${FIRMWARE}/recovery.c"
    "${FIRMWARE}/reset.c"
    "${FIRMWARE}/signing.c"
    "${FIRMWARE}/crypto.c"
    "${FIRMWARE}/usb.c"
    "${FIRMWARE}/u2f.c"
    "${FIRMWARE}/ethereum.c"
    "${FIRMWARE}/ethereum_definitions.c"
    "${FIRMWARE}/ethereum_networks.c"
    "${FIRMWARE}/ethereum_tokens.c"
    "${FIRMWARE}/nem2.c"
    "${FIRMWARE}/nem_mosaics.c"
    "${FIRMWARE}/stellar.c"
    "${FIRMWARE}/debug.c")

set(CRYPTO_SOURCES
    "${CRYPTO}/address.c"
    "${CRYPTO}/bignum.c"
    "${CRYPTO}/ecdsa.c"
    "${CRYPTO}/curves.c"
    "${CRYPTO}/secp256k1.c"
    "${CRYPTO}/nist256p1.c"
    "${CRYPTO}/hmac_drbg.c"
    "${CRYPTO}/rfc6979.c"
    "${CRYPTO}/rand.c"
    "${CRYPTO}/memzero.c"
    "${CRYPTO}/ed25519-donna/curve25519-donna-32bit.c"
    "${CRYPTO}/ed25519-donna/curve25519-donna-helpers.c"
    "${CRYPTO}/ed25519-donna/modm-donna-32bit.c"
    "${CRYPTO}/ed25519-donna/ed25519-donna-basepoint-table.c"
    "${CRYPTO}/ed25519-donna/ed25519-donna-32bit-tables.c"
    "${CRYPTO}/ed25519-donna/ed25519-donna-impl-base.c"
    "${CRYPTO}/ed25519-donna/ed25519.c"
    "${CRYPTO}/ed25519-donna/curve25519-donna-scalarmult-base.c"
    "${CRYPTO}/ed25519-donna/ed25519-sha3.c"
    "${CRYPTO}/ed25519-donna/ed25519-keccak.c"
    "${CRYPTO}/hmac.c"
    "${CRYPTO}/bip32.c"
    "${CRYPTO}/bip39.c"
    "${CRYPTO}/bip39_english.c"
    "${CRYPTO}/pbkdf2.c"
    "${CRYPTO}/base32.c"
    "${CRYPTO}/base58.c"
    "${CRYPTO}/segwit_addr.c"
    "${CRYPTO}/ripemd160.c"
    "${CRYPTO}/sha2.c"
    "${CRYPTO}/sha3.c"
    "${CRYPTO}/blake256.c"
    "${CRYPTO}/blake2b.c"
    "${CRYPTO}/blake2s.c"
    "${CRYPTO}/groestl.c"
    "${CRYPTO}/hasher.c"
    "${CRYPTO}/aes/aescrypt.c"
    "${CRYPTO}/aes/aeskey.c"
    "${CRYPTO}/aes/aestab.c"
    "${CRYPTO}/aes/aes_modes.c"
    "${CRYPTO}/chacha20poly1305/chacha20poly1305.c"
    "${CRYPTO}/chacha20poly1305/chacha_merged.c"
    "${CRYPTO}/chacha20poly1305/poly1305-donna.c"
    "${CRYPTO}/chacha20poly1305/rfc7539.c"
    "${CRYPTO}/consteq.c"
    "${CRYPTO}/nem.c"
    "${CRYPTO}/cash_addr.c"
    "${CRYPTO}/zkp_bip340.c"
    "${CRYPTO}/zkp_context.c"
    "${CRYPTO}/zkp_ecdsa.c")

set(STORAGE_SOURCES
    "${STORAGE}/storage.c"
    "${STORAGE}/storage_utils.c"
    "${STORAGE}/norcow.c")

set(PROTO_SOURCES
    "${NANOPB}/pb_common.c"
    "${NANOPB}/pb_decode.c"
    "${NANOPB}/pb_encode.c"
    "${PROTO_GENERATED}/messages.pb.c"
    "${PROTO_GENERATED}/messages-bitcoin.pb.c"
    "${PROTO_GENERATED}/messages-common.pb.c"
    "${PROTO_GENERATED}/messages-crypto.pb.c"
    "${PROTO_GENERATED}/messages-debug.pb.c"
    "${PROTO_GENERATED}/messages-management.pb.c"
    "${PROTO_GENERATED}/messages-ethereum.pb.c"
    "${PROTO_GENERATED}/messages-definitions.pb.c"
    "${PROTO_GENERATED}/messages-nem.pb.c"
    "${PROTO_GENERATED}/messages-stellar.pb.c")

set(ZKP_SOURCES
    "${ZKP}/src/secp256k1.c"
    "${ZKP}/src/precomputed_ecmult.c"
    "${ZKP}/src/precomputed_ecmult_gen.c")

idf_component_register(
  SRCS ${PORT_SOURCES} ${LEGACY_COMMON_SOURCES} ${FIRMWARE_SOURCES}
       ${CRYPTO_SOURCES} ${STORAGE_SOURCES} ${PROTO_SOURCES} ${ZKP_SOURCES}
       "${REPO_ROOT}/vendor/QR-Code-generator/c/qrcodegen.c"
  INCLUDE_DIRS
       "." "compat" "${PORT_DIR}/../boards" "${PROTO_GENERATED}"
       "${LEGACY}" "${LEGACY}/gen" "${FIRMWARE}"
       "${CRYPTO}" "${CRYPTO}/aes" "${CRYPTO}/chacha20poly1305"
       "${STORAGE}" "${NANOPB}" "${REPO_ROOT}" "${REPO_ROOT}/vendor/QR-Code-generator/c"
       "${ZKP}" "${ZKP}/src"
  REQUIRES
       esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd
       esp_timer esp_partition spi_flash bootloader_support efuse esp_system
       espressif__esp_tinyusb espressif__tinyusb espressif__esp_lcd_touch
       esp_lcd_jd9853 esp_lcd_touch_axs5106)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EMULATOR=1 ESP32S3=1 DEBUG_LINK=0 DEBUG_LOG=0 BITCOIN_ONLY=0 U2F_ENABLED=1
    PRODUCTION=0 BOOTLOADER_QA=0 DEBUG_RNG=0 DEBUG_T1_SIGNATURES=0
    USE_MONERO=0 USE_OPTIGA=0 USE_TROPIC=0 USE_ETHEREUM=1 USE_NEM=1
    FLASH_BLOCK_WORDS=1 FLASH_BIT_ACCESS=1 HW_MODEL=0x31423154 HW_REVISION=0
    PB_FIELD_16BIT=1 PB_ENCODE_ARRAYS_UNPACKED=1 PB_VALIDATE_UTF8=1
    USE_SECP256K1_ZKP USE_SECP256K1_ZKP_ECDSA SECP256K1_CONTEXT_SIZE=180
    USE_EXTERNAL_DEFAULT_CALLBACKS ECMULT_GEN_PREC_BITS=4 ECMULT_WINDOW_SIZE=2
    ENABLE_MODULE_GENERATOR ENABLE_MODULE_RECOVERY ENABLE_MODULE_SCHNORRSIG
    ENABLE_MODULE_EXTRAKEYS ENABLE_MODULE_ECDH
    CONFIDENTIAL=DRAM_ATTR LOCAL_CONFIDENTIAL=static\ DRAM_ATTR
    __assert_func=legacy_assert_func)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -includeesp_attr.h -Wno-sequence-point -Wno-unused-function
    -Wno-format -Wno-format-security -Wno-unused-const-variable
    -Wno-unterminated-string-initialization -ftrivial-auto-var-init=zero)

set_source_files_properties("${FIRMWARE}/trezor.c"
    PROPERTIES COMPILE_DEFINITIONS "main=trezor_main;__stack_chk_guard=legacy_stack_chk_guard")

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE "(..)" "\\\\x\\1" SCM_REVISION "${GIT_HASH}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "SCM_REVISION=\"${SCM_REVISION}\"")

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=tud_descriptor_string_cb"
    "-Wl,--wrap=esp_ota_get_running_partition")
