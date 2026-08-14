# Development Board

By default the FSW is compiled to run as a software-only digital twin. It can also be cross-compiled and deployed to a 32-bit ARM development board (Zybo 7020, Xilinx Zynq SoC).

## Prerequisites

Complete a full build first:

```bash
make
```

## Flashing the Board

**1. Create the transfer tarball**

```bash
tar -C ./build/drm/sat-1/fsw/exe/cpu2/ -zcvf cpu2_fsw.tar.gz .
```

**2. Connect via minicom**

```bash
minicom -D /dev/ttyUSB1
```

**3. Transfer using ZMODEM**

Inside minicom:
- `CTRL + A`, then `S`
- Select **zmodem**
- Select the `cpu2_fsw.tar.gz` file

The transfer will take a few minutes over the low-speed serial link.

**4. Untar on the board**

```bash
tar -xzf ./cpu2_fsw.tar.gz
```

----
Last updated: 20260728
