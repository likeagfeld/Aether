# Aether build image

Builds the SaturnSDK GCC/binutils/newlib toolchain used to compile Aether. No Jo Engine.

```
docker build -t omsat-build docker/
```

To convert the SGL 3.02 libs/headers to ELF for linking, point `convert_sgl.sh` at your SGL tree:

```
docker run --rm -v "$PWD:/work" -v "/path/to/SGL302:/sgl302:ro" omsat-build sh /work/docker/convert_sgl.sh
```

Output goes to `build/sgl/` (gitignored, rebuilt as needed).
