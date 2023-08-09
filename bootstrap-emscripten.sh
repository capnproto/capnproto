#!/usr/bin/env bash
set -ex

FIRST_ARG=$1
SDK_INSTALL_DIR=${FIRST_ARG:=${HOME}}
MESON_PREFIXES_INI_FILE=meson/cross/emscripten_constants.ini
EMSCRIPTEN_VERSION=3.1.42


create_cross_emscripten_sdk_constants() {
    echo "[constants]" > ${MESON_PREFIXES_INI_FILE}
    echo "emsdk_prefix = '${SDK_INSTALL_DIR}/emsdk/'" >> ${MESON_PREFIXES_INI_FILE}
    echo "emscripten_prefix = emsdk_prefix + '/upstream/emscripten/'" >> ${MESON_PREFIXES_INI_FILE}
    echo "wasm_bin_prefix = emsdk_prefix + '/upstream/bin/'"  >> ${MESON_PREFIXES_INI_FILE}
}


create_cross_emscripten_sdk_constants
if [ -d "${SDK_INSTALL_DIR}/emsdk" ] && [ ! -f "${SDK_INSTALL_DIR}/.installing_emsdk" ]; then
    exit 0
fi


cleanup() {
    local rv=$?
    if [ ${rv} -eq 0 ]; then
        rm -rf ${SDK_INSTALL_DIR}/.installing_emsdk
    else
        rm -rf ${SDK_INSTALL_DIR}/emsdk
        rm ${PREFIXES_INI_FILE}
        rm ${CONAN_PREFIXES_INI_FILE}
    fi
    cd ${OLDDIR}
}


setup_conan
rm -rf "${SDK_INSTALL_DIR}/emsdk"
touch ${SDK_INSTALL_DIR}/.installing_emsdk
trap cleanup EXIT
OLDDIR=${PWD}
cd ${SDK_INSTALL_DIR}
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install ${EMSCRIPTEN_VERSION}
./emsdk activate ${EMSCRIPTEN_VERSION}
