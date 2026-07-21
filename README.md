# vulkan-ps4

vulkan-ps4 is a Vulkan 1.0 Installable Client Driver (ICD) that translates
Vulkan API calls into GNM/PM4 commands, enabling any Vulkan application to
run with GPU acceleration on jailbroken PS4 hardware.

It sits on top of [opengnm](https://github.com/PS4-OpenGNM/opengnm) for the
GNM API and [opengnm-psbc](https://github.com/PS4-OpenGNM/opengnm-psbc) for
runtime SPIR-V shader compilation.

## Features

- **Full Vulkan 1.0 core API** — instances, devices, queues, command buffers
- **Graphics pipelines** — VS/PS/CS/GS/HS/DS shader stages
- **Compute pipelines** — dispatch and descriptors
- **Descriptor sets** — uniform buffers, storage buffers, sampled images
- **Memory management** — VkDeviceMemory backed by GNM memory
- **Textures** — copy, blit, barriers, format conversion
- **Drawing** — indexed draws, indirect draws, vertex input / fetch shaders
- **Render passes** — framebuffer and attachment management
- **Swapchain** — via VideoOut
- **Runtime shader compilation** — SPIR-V → GCN via libpsbc at `vkCreateShaderModule`

## Building

### PS4 (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain

# Dependencies must be built first:
#   ../opengnm/libopengnm.a        (make -C ../opengnm -f config.orbis.mak)
#   ../opengnm-psbc/libpsbc.orbis.a (make -C ../opengnm-psbc -f Makefile.orbis)

make -f Makefile.orbis -j$(nproc)
```

Output:
- `libvulkan_ps4.so` — shared Vulkan ICD library for PS4
- `libvulkan_ps4.a` — static library (for static linking into apps)

### Host (development/testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Dependencies

| Dependency | Path | Purpose |
|------------|------|---------|
| [opengnm](https://github.com/PS4-OpenGNM/opengnm) | `../opengnm` | GNM API + PM4 builders |
| [opengnm-psbc](https://github.com/PS4-OpenGNM/opengnm-psbc) | `../opengnm-psbc` | Runtime shader compilation (`libpsbc.orbis.a`) |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | `../Vulkan-Headers` | Vulkan type definitions |
| OpenOrbis PS4 Toolchain | `$OO_PS4_TOOLCHAIN` | PS4 cross-compilation |

## Usage

Link your Vulkan application against `libvulkan_ps4` instead of the standard
Vulkan loader:

```sh
clang --target=x86_64-ps4-elf \
    -isysroot $OO_PS4_TOOLCHAIN \
    -I../Vulkan-Headers/include \
    -o myapp.elf myapp.c \
    -L. -lvulkan_ps4 \
    -L../opengnm -lopengnm \
    ../opengnm-psbc/libpsbc.orbis.a \
    -lSceGnmDriver -lSceVideoOut -lkernel -lc
```

Your Vulkan application code requires no changes — the ICD handles all
`vk*` calls and translates them to GNM/PM4 internally.

## Tests

```sh
# Host tests
cmake --build build --target vk_ps4_triangle_test
./build/vk_ps4_triangle_test

# Format and descriptor tests
cmake --build build --target vk_ps4_format_test vk_ps4_descriptor_test
./build/vk_ps4_format_test
./build/vk_ps4_descriptor_test
```

## License

MIT, see [LICENSE](LICENSE).
