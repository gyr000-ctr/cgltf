/*
 * Test assets for KHR_animation_pointer parsing, resolution and round-trip.
 * The glTF documents are embedded so the test is a single self-contained
 * binary and does not depend on a working directory.
 */
#ifndef CGLTF_TEST_ANIMATION_POINTER_DATA_H
#define CGLTF_TEST_ANIMATION_POINTER_DATA_H

#if defined(__GNUC__) || defined(__clang__)
#define CGLTF_TEST_MAYBE_UNUSED __attribute__((unused))
#else
#define CGLTF_TEST_MAYBE_UNUSED
#endif

/* Base64 of 64 zero bytes ("AAAA...AA==", 88 characters). */
#define CGLTF_TEST_B64_64 \
	"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" \
	"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="
#define CGLTF_TEST_URI_64 "data:application/octet-stream;base64," CGLTF_TEST_B64_64

/*
 * A valid asset exercising material, texture transform, punctual light,
 * camera, node weights element and node TRS pointer targets. Two channels
 * share one sampler, and two channels share an identical pointer string in
 * different animations.
 */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_valid =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"extensionsUsed\": [\"KHR_animation_pointer\", \"KHR_lights_punctual\"],"
"  \"extensions\": {"
"    \"KHR_lights_punctual\": {"
"      \"lights\": [ { \"type\": \"spot\", \"color\": [1, 1, 1], \"intensity\": 5, \"range\": 20, \"spot\": { \"innerConeAngle\": 0.2, \"outerConeAngle\": 0.6 } } ]"
"    }"
"  },"
"  \"materials\": ["
"    {"
"      \"pbrMetallicRoughness\": {"
"        \"baseColorFactor\": [1, 0, 0, 1],"
"        \"baseColorTexture\": { \"index\": 0, \"extensions\": { \"KHR_texture_transform\": { \"offset\": [0, 0], \"rotation\": 0, \"scale\": [1, 1] } } }"
"      },"
"      \"normalTexture\": { \"index\": 0, \"scale\": 1 },"
"      \"emissiveFactor\": [0, 0, 0],"
"      \"alphaCutoff\": 0.5,"
"      \"extensions\": { \"KHR_materials_ior\": { \"ior\": 1.5 } }"
"    }"
"  ],"
"  \"textures\": [ { \"source\": 0 } ],"
"  \"images\": [ { \"uri\": \"x.png\" } ],"
"  \"cameras\": [ { \"type\": \"perspective\", \"perspective\": { \"yfov\": 0.8, \"znear\": 0.1, \"zfar\": 100, \"aspectRatio\": 1.5 } } ],"
"  \"meshes\": ["
"    {"
"      \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, \"targets\": [ { \"POSITION\": 1 }, { \"POSITION\": 2 } ] } ],"
"      \"weights\": [0.25, 0.75]"
"    }"
"  ],"
"  \"nodes\": ["
"    { \"mesh\": 0, \"weights\": [0.25, 0.75], \"translation\": [0, 0, 0] },"
"    { \"light\": 0 },"
"    { \"camera\": 0 }"
"  ],"
"  \"accessors\": ["
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" },"
"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"SCALAR\" }"
"  ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": ["
"    {"
"      \"samplers\": ["
"        { \"input\": 3, \"output\": 4, \"interpolation\": \"LINEAR\" },"
"        { \"input\": 3, \"output\": 5 },"
"        { \"input\": 3, \"output\": 6 },"
"        { \"input\": 3, \"output\": 7 },"
"        { \"input\": 3, \"output\": 8 },"
"        { \"input\": 3, \"output\": 9 },"
"        { \"input\": 3, \"output\": 10 }"
"      ],"
"      \"channels\": ["
"        { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorFactor\" } } } },"
"        { \"sampler\": 1, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorTexture/extensions/KHR_texture_transform/offset\" } } } },"
"        { \"sampler\": 2, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/extensions/KHR_lights_punctual/lights/0/color\" } } } },"
"        { \"sampler\": 3, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/cameras/0/perspective/aspectRatio\" } } } },"
"        { \"sampler\": 4, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/nodes/0/weights/1\" } } } },"
"        { \"sampler\": 5, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/extensions/KHR_materials_ior/ior\" } } } },"
"        { \"sampler\": 6, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/nodes/0/translation\" } } } }"
"      ]"
"    },"
"    {"
"      \"samplers\": [ { \"input\": 3, \"output\": 4 } ],"
"      \"channels\": ["
"        { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorFactor\" } } } }"
"      ]"
"    }"
"  ]"
"}";

/* Malformed RFC 6901 syntax: bad escape sequence. */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_malformed =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"materials\": [ { \"pbrMetallicRoughness\": { \"baseColorFactor\": [1, 1, 1, 1] } } ],"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 8, \"type\": \"SCALAR\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 1, \"output\": 0 } ],"
"    \"channels\": [ { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/~2x\" } } } } ]"
"  } ]"
"}";

/* Index out of bounds: material 7 does not exist. */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_oob =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"materials\": [ { \"pbrMetallicRoughness\": { \"baseColorFactor\": [1, 1, 1, 1] } } ],"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 8, \"type\": \"SCALAR\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 1, \"output\": 0 } ],"
"    \"channels\": [ { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/7/pbrMetallicRoughness/baseColorFactor\" } } } } ]"
"  } ]"
"}";

/* Unknown/unsupported root object and a property that cannot be animated. */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_unknown =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 0, \"output\": 1 } ],"
"    \"channels\": [ { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/buffers/0/byteLength\" } } } } ]"
"  } ]"
"}";

/* Property undefined: zfar missing on the perspective camera. */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_undefined =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"cameras\": [ { \"type\": \"perspective\", \"perspective\": { \"yfov\": 0.8, \"znear\": 0.1 } } ],"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 0, \"output\": 1 } ],"
"    \"channels\": [ { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/cameras/0/perspective/zfar\" } } } } ]"
"  } ]"
"}";

/* Duplicate target within one animation (also covers array vs. element rule). */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_duplicate =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"materials\": [ { \"pbrMetallicRoughness\": { \"baseColorFactor\": [1, 1, 1, 1] } } ],"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 8, \"type\": \"SCALAR\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 1, \"output\": 0 }, { \"input\": 1, \"output\": 0 } ],"
"    \"channels\": ["
"      { \"sampler\": 0, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorFactor\" } } } },"
"      { \"sampler\": 1, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorFactor\" } } } }"
"    ]"
"  } ]"
"}";

/* A classic node animation that must keep parsing and writing identically. */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_legacy =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"nodes\": [ { \"translation\": [1, 2, 3] } ],"
"  \"accessors\": [ { \"componentType\": 5126, \"count\": 2, \"type\": \"VEC3\" }, { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" } ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 1, \"output\": 0, \"interpolation\": \"STEP\" } ],"
"    \"channels\": [ { \"sampler\": 0, \"target\": { \"node\": 0, \"path\": \"translation\" } } ]"
"  } ]"
"}";

/*
 * A pointer-path channel with no extension object. It must parse leniently,
 * stay inert (no binding), and survive a write round-trip without a node.
 * A valid sibling pointer channel proves registry indexing stays correct.
 */
static CGLTF_TEST_MAYBE_UNUSED const char* cgltf_test_pointer_missing_ext =
"{"
"  \"asset\": { \"version\": \"2.0\" },"
"  \"materials\": [ { \"pbrMetallicRoughness\": { \"baseColorFactor\": [1, 1, 1, 1] } } ],"
"  \"accessors\": ["
"    { \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" },"
"    { \"componentType\": 5126, \"count\": 4, \"type\": \"SCALAR\" },"
"    { \"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\" }"
"  ],"
"  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 64 } ],"
"  \"buffers\": [ { \"byteLength\": 64, \"uri\": \"" CGLTF_TEST_URI_64 "\" } ],"
"  \"animations\": [ {"
"    \"samplers\": [ { \"input\": 0, \"output\": 1 }, { \"input\": 2, \"output\": 1 } ],"
"    \"channels\": ["
"      { \"sampler\": 0, \"target\": { \"path\": \"pointer\" } },"
"      { \"sampler\": 1, \"target\": { \"path\": \"pointer\", \"extensions\": { \"KHR_animation_pointer\": { \"pointer\": \"/materials/0/pbrMetallicRoughness/baseColorFactor\" } } } }"
"    ]"
"  } ]"
"}";

#endif
