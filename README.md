# :diamond_shape_with_a_dot_inside: cgltf
**Single-file/stb-style C glTF loader and writer**

[![Build Status](https://github.com/jkuhlmann/cgltf/workflows/build/badge.svg)](https://github.com/jkuhlmann/cgltf/actions)

Used in: [bgfx](https://github.com/bkaradzic/bgfx), [Filament](https://github.com/google/filament), [gltfpack](https://github.com/zeux/meshoptimizer/tree/master/gltf), [raylib](https://github.com/raysan5/raylib), [Unigine](https://developer.unigine.com/en/docs/2.14.1/third_party?rlang=cpp#cgltf), and more!

## Usage: Loading
Loading from file:
```c
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

cgltf_options options = {0};
cgltf_data* data = NULL;
cgltf_result result = cgltf_parse_file(&options, "scene.gltf", &data);
if (result == cgltf_result_success)
{
	/* TODO make awesome stuff */
	cgltf_free(data);
}
```

Loading from memory:
```c
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

void* buf; /* Pointer to glb or gltf file data */
size_t size; /* Size of the file data */

cgltf_options options = {0};
cgltf_data* data = NULL;
cgltf_result result = cgltf_parse(&options, buf, size, &data);
if (result == cgltf_result_success)
{
	/* TODO make awesome stuff */
	cgltf_free(data);
}
```

Note that cgltf does not load the contents of extra files such as buffers or images into memory by default. You'll need to read these files yourself using URIs from `data.buffers[]` or `data.images[]` respectively.
For buffer data, you can alternatively call `cgltf_load_buffers`, which will use `FILE*` APIs to open and read buffer files. This automatically decodes base64 data URIs in buffers. For data URIs in images, you will need to use `cgltf_load_buffer_base64`.

**For more in-depth documentation and a description of the public interface refer to the top of the `cgltf.h` file.**

## Usage: Writing
When writing glTF data, you need a valid `cgltf_data` structure that represents a valid glTF document. You can construct such a structure yourself or load it using the loader functions described above. The writer functions do not deallocate any memory. So, you either have to do it manually or call `cgltf_free()` if you got the data by loading it from a glTF document.

Writing to file:
```c
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

cgltf_options options = {0};
cgltf_data* data = /* TODO must be valid data */;
cgltf_result result = cgltf_write_file(&options, "out.gltf", data);
if (result != cgltf_result_success)
{
	/* TODO handle error */
}
```

Writing to memory:
```c
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"
cgltf_options options = {0};
cgltf_data* data = /* TODO must be valid data */;

cgltf_size size = cgltf_write(&options, NULL, 0, data);

char* buf = malloc(size);

cgltf_size written = cgltf_write(&options, buf, size, data);
if (written != size)
{
	/* TODO handle error */
}
```

Note that cgltf does not write the contents of extra files such as buffers or images. You'll need to write this data yourself.

**For more in-depth documentation and a description of the public interface refer to the top of the `cgltf_write.h` file.**


## Features
cgltf supports core glTF 2.0:
- glb (binary files) and gltf (JSON files)
- meshes (including accessors, buffer views, buffers)
- materials (including textures, samplers, images)
- scenes and nodes
- skins
- animations
- cameras
- morph targets
- extras data

cgltf also supports some glTF extensions:
- EXT_mesh_gpu_instancing
- EXT_meshopt_compression / KHR_meshopt_compression (decoding supported, see below)
- EXT_texture_webp
- KHR_draco_mesh_compression (requires a library like [Google's Draco](https://github.com/google/draco) for decompression though)
- KHR_lights_punctual
- KHR_materials_anisotropy
- KHR_materials_clearcoat
- KHR_materials_diffuse_transmission
- KHR_materials_dispersion
- KHR_materials_emissive_strength
- KHR_materials_ior
- KHR_materials_iridescence
- KHR_materials_pbrSpecularGlossiness
- KHR_materials_sheen
- KHR_materials_specular
- KHR_materials_transmission
- KHR_materials_unlit
- KHR_materials_variants
- KHR_materials_volume
- KHR_texture_basisu (requires a library like [Binomial Basisu](https://github.com/BinomialLLC/basis_universal) for transcoding to native compressed texture)
- KHR_texture_transform

cgltf does **not** yet support unlisted extensions. However, unlisted extensions can be accessed via "extensions" member on objects.

### Meshopt compression (EXT/KHR_meshopt_compression)

Parsing the extension declarations is built into `cgltf.h`; the actual decompression lives in a
separate, optional C-ABI layer (`third_party/meshoptimizer/cgltf_meshopt.cpp`, built on the
MIT-licensed [meshoptimizer](https://github.com/zeux/meshoptimizer) decoder). cgltf reaches it
through a single weak function pointer, `cgltf_meshopt_decode_hook`, so no accessor-parsing code is
duplicated: decoded bytes are placed in the existing `cgltf_buffer_view::data` field and every read
(`cgltf_buffer_view_data`, `cgltf_accessor_read_float/uint/int`, `cgltf_accessor_unpack_floats`,
sparse accessors) goes through the same entry points as for uncompressed files.

Link the objects from `third_party/meshoptimizer/` into your target (the CMake setup under `test/`
shows how) to enable decoding. If the layer is not linked, or `CGLTF_MESHOPT_DECODE` is defined to
`0`, `cgltf_load_buffers` fails with a distinct result instead of returning undecoded vertices:

- `cgltf_result_unsupported_extension` — no decoder is available for the extension.
- `cgltf_result_invalid_compressed_data` — a decoder is present but the bitstream is corrupt or
  truncated.
- `cgltf_result_data_too_short` — the compressed byte range exceeds its buffer.

Files without the extension are untouched and decode byte-for-byte identically.

## Building
The easiest approach is to integrate the `cgltf.h` header file into your project. If you are unfamiliar with single-file C libraries (also known as stb-style libraries), this is how it goes:

1. Include `cgltf.h` where you need the functionality.
1. Have exactly one source file that defines `CGLTF_IMPLEMENTATION` before including `cgltf.h`.
1. Use the cgltf functions as described above.

Support for writing can be found in a separate file called `cgltf_write.h` (which includes `cgltf.h`). Building it works analogously using the `CGLTF_WRITE_IMPLEMENTATION` define.

## Contributing
Everyone is welcome to contribute to the library. If you find any problems, you can submit them using [GitHub's issue system](https://github.com/jkuhlmann/cgltf/issues). If you want to contribute code, you should fork the project and then send a pull request.


## Dependencies
None.

C headers being used by the implementation:
```
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h> // If asserts are enabled.
```

Note, this library has a copy of the [JSMN JSON parser](https://github.com/zserge/jsmn) embedded in its source.

## Testing
There is a Python script in the `test/` folder that retrieves the glTF 2.0 sample files from the glTF-Sample-Models repository (https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0) and runs the library against all gltf and glb files.

Here's one way to build and run the test:

    cd test ; mkdir build ; cd build ; cmake .. -DCMAKE_BUILD_TYPE=Debug
    make -j
    cd ..
    ./test_all.py

There is also a llvm-fuzz test in `fuzz/`. See http://llvm.org/docs/LibFuzzer.html for more information.

The meshopt decoder has its own end-to-end test (`test_meshopt`). `gen_meshopt_fixtures` produces a
matched pair of glTF files (an uncompressed reference and an `EXT_meshopt_compression` version) using
the reference meshoptimizer encoder; the test then verifies through the public cgltf API that decoded
attributes and indices match the reference element-by-element, that plain files stay byte-identical,
and that truncated/corrupt streams and disabled decoders return the distinct error codes above. The
`run_meshopt_tests` CMake target builds the fixtures and runs the checks:

    cd test ; mkdir build ; cd build ; cmake .. -DCMAKE_BUILD_TYPE=Debug
    make run_meshopt_tests

