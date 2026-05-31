cd /home/hope/wsp/demo/lionsos/examples/pc-benchmark

LIONSOS=/home/hope/wsp/demo/lionsos
APP=$LIONSOS/dep/catalog-core/c-hello
UK_ROOT=$LIONSOS/dep/unikraft
UK_BUILD=$PWD/build/uk-test
UK_APP=$APP
UK_DEFCONFIG=$LIONSOS/components/pc-benchmark/uk-carrels-arm.config

rm -rf "$UK_BUILD"

make -C "$APP" \
    UK_ROOT="$UK_ROOT" \
    UK_APP="$UK_APP" \
    UK_BUILD="$UK_BUILD" \
    distclean

make -C "$APP" \
    UK_ROOT="$UK_ROOT" \
    UK_APP="$UK_APP" \
    UK_BUILD="$UK_BUILD" \
    UK_DEFCONFIG="$UK_DEFCONFIG" \
    defconfig

make -C "$APP" \
    UK_ROOT="$UK_ROOT" \
    UK_APP="$UK_APP" \
    UK_BUILD="$UK_BUILD" \
    -j"$(nproc)"