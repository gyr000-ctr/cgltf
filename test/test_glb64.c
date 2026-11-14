/*
 * Tests for 64-bit GLB (GLBv3, binary container version 3) support and
 * byte-level regression coverage of the legacy 32-bit GLBv2 path.
 *
 * All containers are synthesized in memory. Huge (>= 4 GiB) sizes are
 * represented by their declared 64-bit length fields only; no backing
 * allocation of that size is ever made, so the tests verify overflow and
 * truncation handling without touching large files.
 */

#define CGLTF_IMPLEMENTATION
#include "../cgltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_num_tests = 0;
static int g_num_failed = 0;

#define CHECK(cond) do { \
	++g_num_tests; \
	if (!(cond)) { \
		++g_num_failed; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define GLB_MAGIC 0x46546C67u
#define JSON_MAGIC 0x4E4F534Au
#define BIN_MAGIC 0x004E4942u

static const char* k_json = "{\"asset\":{\"version\":\"2.1\"}}";

static void put_u32(unsigned char* p, uint32_t v)
{
	memcpy(p, &v, 4);
}

static void put_u64(unsigned char* p, uint64_t v)
{
	memcpy(p, &v, 8);
}

static cgltf_result parse_buffer(const unsigned char* buf, cgltf_size size, cgltf_data** out)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	return cgltf_parse(&options, buf, size, out);
}

/* For failure-path tests where the result must be non-success and out stays NULL. */
static cgltf_result parse_fail(const unsigned char* buf, cgltf_size size)
{
	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, size, &data);
	if (result == cgltf_result_success)
	{
		cgltf_free(data);
	}
	return result;
}

/* Forces GLB type detection so magic/format mismatches do not fall back to JSON parsing. */
static cgltf_result parse_fail_glb(const unsigned char* buf, cgltf_size size)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, size, &data);
	if (result == cgltf_result_success)
	{
		cgltf_free(data);
	}
	return result;
}

/* Builds a legacy GLBv2 container with one JSON chunk and an optional BIN chunk. */
static cgltf_size build_glb_v2(unsigned char* out, cgltf_size capacity, const char* json,
	const void* bin, cgltf_size bin_size)
{
	cgltf_size json_len = strlen(json);
	cgltf_size json_pad = (4 - (json_len % 4)) % 4;
	cgltf_size total = 12 + 8 + json_len + json_pad;
	if (bin)
	{
		cgltf_size bin_pad = (4 - (bin_size % 4)) % 4;
		total += 8 + bin_size + bin_pad;
	}
	CHECK(total <= capacity);
	memset(out, 0, total);
	put_u32(out + 0, GLB_MAGIC);
	put_u32(out + 4, 2);
	put_u32(out + 8, (uint32_t)total);
	put_u32(out + 12, (uint32_t)(json_len + json_pad));
	put_u32(out + 16, JSON_MAGIC);
	memcpy(out + 20, json, json_len);
	cgltf_size off = 20 + json_len + json_pad;
	if (bin)
	{
		cgltf_size bin_pad = (4 - (bin_size % 4)) % 4;
		put_u32(out + off, (uint32_t)(bin_size + bin_pad));
		put_u32(out + off + 4, BIN_MAGIC);
		memcpy(out + off + 8, bin, bin_size);
	}
	return total;
}

/* Builds a GLBv3 container: 16-byte header, 16-byte chunk headers, 8-byte alignment. */
static cgltf_size build_glb_v3(unsigned char* out, cgltf_size capacity, const char* json,
	const void* bin, cgltf_size bin_size)
{
	cgltf_size json_len = strlen(json);
	cgltf_size total = 16;
	/* JSON chunk header + data, start already 8-byte aligned after the header. */
	cgltf_size json_data_end = total + 16 + json_len;
	cgltf_size json_aligned = (json_data_end + 7) & ~(cgltf_size)7;
	total = json_aligned;
	if (bin)
	{
		cgltf_size bin_data_end = total + 16 + bin_size;
		cgltf_size bin_aligned = (bin_data_end + 7) & ~(cgltf_size)7;
		total = bin_aligned;
	}
	CHECK(total <= capacity);
	memset(out, 0, total);
	put_u32(out + 0, GLB_MAGIC);
	put_u32(out + 4, 3);
	put_u64(out + 8, (uint64_t)total);
	/* JSON chunk: type, encoding, uint64 length. */
	put_u32(out + 16, JSON_MAGIC);
	put_u32(out + 20, 0);
	put_u64(out + 24, (uint64_t)json_len);
	memcpy(out + 32, json, json_len);
	if (bin)
	{
		cgltf_size off = json_aligned;
		put_u32(out + off, BIN_MAGIC);
		put_u32(out + off + 4, 0);
		put_u64(out + off + 8, (uint64_t)bin_size);
		memcpy(out + off + 16, bin, bin_size);
	}
	return total;
}

static void test_v2_regression(void)
{
	unsigned char buf[512];
	cgltf_size size = build_glb_v2(buf, sizeof(buf), k_json, NULL, 0);

	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->file_type == cgltf_file_type_glb);
		CHECK(data->glb.version == cgltf_glb_version_2);
		CHECK(data->glb.header_size == 12);
		CHECK(data->glb.total_length == size);
		CHECK(data->glb.chunks_count == 1);
		CHECK(data->glb.chunks[0].type == cgltf_glb_chunk_type_json);
		CHECK(data->glb.chunks[0].encoding == 0);
		/* GLBv2 chunkLength includes the 4-byte alignment padding. */
		CHECK(data->glb.chunks[0].size == ((strlen(k_json) + 3) & ~(cgltf_size)3));
		CHECK(data->glb.chunks[0].data == buf + 20); /* zero-copy, borrowed pointer */
		CHECK(data->bin == NULL);
		cgltf_free(data);
	}
}

static void test_v2_with_bin(void)
{
	unsigned char buf[512];
	const unsigned char bin_bytes[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	cgltf_size size = build_glb_v2(buf, sizeof(buf), k_json, bin_bytes, sizeof(bin_bytes));

	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->glb.chunks_count == 2);
		CHECK(data->glb.chunks[1].type == cgltf_glb_chunk_type_bin);
		CHECK(data->glb.chunks[1].size == sizeof(bin_bytes));
		CHECK(data->bin_size == sizeof(bin_bytes));
		CHECK((const unsigned char*)data->bin == (const unsigned char*)data->glb.chunks[1].data);
		CHECK(memcmp(data->bin, bin_bytes, sizeof(bin_bytes)) == 0);
		cgltf_free(data);
	}
}

static void test_v3_basic(void)
{
	unsigned char buf[512];
	cgltf_size size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);

	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->file_type == cgltf_file_type_glb);
		CHECK(data->glb.version == cgltf_glb_version_3);
		CHECK(data->glb.header_size == 16);
		CHECK(data->glb.total_length == size);
		CHECK(data->glb.chunks_count == 1);
		CHECK(data->glb.chunks[0].type == cgltf_glb_chunk_type_json);
		CHECK(data->glb.chunks[0].encoding == 0);
		CHECK(data->glb.chunks[0].size == strlen(k_json));
		CHECK(data->glb.chunks[0].offset == 32);
		CHECK(data->glb.chunks[0].data == buf + 32);
		CHECK(data->bin == NULL);
		cgltf_free(data);
	}
}

static void test_v3_with_bin_and_padding(void)
{
	unsigned char buf[512];
	/* 5-byte BIN forces 1..7 bytes of 8-byte alignment padding after JSON/BIN. */
	const unsigned char bin_bytes[5] = { 9, 8, 7, 6, 5 };
	cgltf_size size = build_glb_v3(buf, sizeof(buf), k_json, bin_bytes, sizeof(bin_bytes));

	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		/* JSON data is 27 bytes; 32 + 27 = 59, next chunk starts at 64. */
		CHECK(data->glb.chunks_count == 2);
		CHECK(data->glb.chunks[1].type == cgltf_glb_chunk_type_bin);
		CHECK(data->glb.chunks[1].size == 5);
		CHECK(data->glb.chunks[1].offset == 64 + 16);
		CHECK(data->bin_size == 5);
		CHECK(memcmp(data->bin, bin_bytes, 5) == 0);
		CHECK((const unsigned char*)data->glb.chunks[1].data == buf + 80);
		cgltf_free(data);
	}
}

/* A v3 JSON that references the BIN chunk explicitly via buffer.chunk. */
static void test_v3_buffer_chunk_binding(void)
{
	const char* json =
		"{\"asset\":{\"version\":\"2.1\"},"
		"\"buffers\":[{\"byteLength\":4,\"chunk\":1}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":4}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5125,\"count\":1,\"type\":\"SCALAR\"}]}";
	unsigned char buf[1024];
	const unsigned char bin_bytes[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0 };
	cgltf_size size = build_glb_v3(buf, sizeof(buf), json, bin_bytes, sizeof(bin_bytes));

	cgltf_options options;
	memset(&options, 0, sizeof(options));
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}
	CHECK(data->buffers_count == 1);
	CHECK(data->buffers[0].has_chunk == 1);
	CHECK(data->buffers[0].chunk == 1);

	result = cgltf_load_buffers(&options, data, "test.glb");
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->buffers[0].data == data->glb.chunks[1].data);
		CHECK(data->buffers[0].data_free_method == cgltf_data_free_method_none);
	}
	cgltf_free(data);
}

/* Implicit first-buffer BIN binding (glTF 2.0 compatibility) must keep working on v3. */
static void test_v3_implicit_bin_binding(void)
{
	const char* json =
		"{\"asset\":{\"version\":\"2.1\"},"
		"\"buffers\":[{\"byteLength\":4}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":4}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5125,\"count\":1,\"type\":\"SCALAR\"}]}";
	unsigned char buf[1024];
	const unsigned char bin_bytes[8] = { 1, 2, 3, 4, 0, 0, 0, 0 };
	cgltf_size size = build_glb_v3(buf, sizeof(buf), json, bin_bytes, sizeof(bin_bytes));

	cgltf_options options;
	memset(&options, 0, sizeof(options));
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}
	CHECK(data->buffers[0].has_chunk == 0);
	result = cgltf_load_buffers(&options, data, "test.glb");
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->buffers[0].data == data->bin);
	}
	cgltf_free(data);
}

static void test_truncated_inputs(void)
{
	unsigned char buf[512];
	cgltf_size size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);

	/* Truncated before even the 16-byte v3 header. */
	CHECK(parse_fail(buf, 15) == cgltf_result_data_too_short);

	/* Truncated inside the JSON chunk header. */
	CHECK(parse_fail(buf, 20) == cgltf_result_data_too_short);

	/* Truncated inside JSON chunk data. */
	CHECK(parse_fail(buf, 32 + 4) == cgltf_result_data_too_short);

	/* Declared total length exceeds the actual buffer. */
	unsigned char copy[512];
	memcpy(copy, buf, size);
	put_u64(copy + 8, (uint64_t)size + 64);
	CHECK(parse_fail(copy, size) == cgltf_result_data_too_short);

	/* Same truncation rules on the v2 path. */
	cgltf_size v2_size = build_glb_v2(buf, sizeof(buf), k_json, NULL, 0);
	CHECK(parse_fail(buf, v2_size - 1) == cgltf_result_data_too_short);
}

static void test_huge_declared_lengths(void)
{
	unsigned char buf[512];

	/* v3 header declaring a > 4 GiB file: must fail cleanly, never allocate. */
	cgltf_size size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u64(buf + 8, ((uint64_t)1 << 32) + 16); /* 4 GiB + header */
	CHECK(parse_fail(buf, size) == cgltf_result_data_too_short);

	/* v3 chunk declaring a > 4 GiB JSON chunk inside a small actual buffer. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u64(buf + 24, ((uint64_t)1 << 32) + 1);
	CHECK(parse_fail(buf, size) == cgltf_result_data_too_short);

	/* MSB set in the 64-bit total length is reserved and must be rejected. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u64(buf + 8, (uint64_t)1 << 63);
	CHECK(parse_fail(buf, size) == cgltf_result_invalid_glb);

	/* MSB set in a chunk length is likewise reserved. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u64(buf + 24, (uint64_t)1 << 63);
	CHECK(parse_fail(buf, size) == cgltf_result_invalid_glb);
}

static void test_corrupt_chunks(void)
{
	unsigned char buf[512];
	cgltf_size size;

	/* Bad magic (forced GLB type; auto-detection would otherwise treat it as JSON). */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 0, 0x12345678);
	CHECK(parse_fail_glb(buf, size) == cgltf_result_unknown_format);

	/* Unknown container version (4). */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 4, 4);
	CHECK(parse_fail(buf, size) == cgltf_result_unsupported_glb_version);

	/* Legacy version 1 still maps to legacy_gltf. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 4, 1);
	CHECK(parse_fail(buf, size) == cgltf_result_legacy_gltf);

	/* First chunk is not JSON. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 16, BIN_MAGIC);
	CHECK(parse_fail(buf, size) == cgltf_result_invalid_glb);

	/* Missing JSON chunk entirely: turn the only chunk into an unknown type. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 16, 0x58585858);
	CHECK(parse_fail(buf, size) == cgltf_result_invalid_glb);

	/* Unknown chunk encoding must be refused with a distinct error. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 20, 7);
	CHECK(parse_fail(buf, size) == cgltf_result_unknown_encoding);

	/* A zero-length chunk cannot advance the parser and must be rejected, not looped on. */
	size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);
	put_u64(buf + 24, 0);
	CHECK(parse_fail(buf, size) == cgltf_result_invalid_glb);

	/* v2 wrong JSON chunk magic keeps its historical unknown_format result. */
	size = build_glb_v2(buf, sizeof(buf), k_json, NULL, 0);
	put_u32(buf + 16, BIN_MAGIC);
	CHECK(parse_fail(buf, size) == cgltf_result_unknown_format);
}

/* Chunk interleaving: a non-JSON chunk may precede the JSON chunk under GLBv3. */
static void test_v3_chunk_interleaving(void)
{
	unsigned char buf[512];
	memset(buf, 0, sizeof(buf));
	cgltf_size json_len = strlen(k_json);

	/* Header (16 bytes). */
	put_u32(buf + 0, GLB_MAGIC);
	put_u32(buf + 4, 3);
	/* Chunk 0 at 16: unknown type "XXXX", plain encoding, 5 bytes of data. */
	put_u32(buf + 16, 0x58585858);
	put_u32(buf + 20, 0);
	put_u64(buf + 24, 5);
	memset(buf + 32, 0xAB, 5);
	/* Data ends at 37; 3 bytes of padding; chunk 1 starts at the next 8-byte boundary, 40. */
	/* Chunk 1: JSON. */
	put_u32(buf + 40, JSON_MAGIC);
	put_u32(buf + 44, 0);
	put_u64(buf + 48, json_len);
	memcpy(buf + 56, k_json, json_len);
	cgltf_size end = (56 + json_len + 7) & ~(cgltf_size)7;
	put_u64(buf + 8, end);

	cgltf_data* data = NULL;
	cgltf_result result = parse_buffer(buf, end, &data);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(data->glb.chunks_count == 2);
		CHECK(data->glb.chunks[0].type == 0x58585858);
		CHECK(data->glb.chunks[0].size == 5);
		CHECK(data->glb.chunks[0].offset == 32);
		CHECK(data->glb.chunks[1].type == cgltf_glb_chunk_type_json);
		CHECK(data->glb.chunks[1].offset == 56);
		/* The first JSON chunk is what gets parsed regardless of position. */
		CHECK(data->glb.chunks[1].data == buf + 56);
		cgltf_free(data);
	}
}

/* Explicit buffer.chunk pointing past the end of the chunk table. */
static void test_v3_invalid_chunk_reference(void)
{
	const char* json =
		"{\"asset\":{\"version\":\"2.1\"},"
		"\"buffers\":[{\"byteLength\":4,\"chunk\":9}]}";
	unsigned char buf[512];
	cgltf_size size = build_glb_v3(buf, sizeof(buf), json, NULL, 0);

	cgltf_options options;
	memset(&options, 0, sizeof(options));
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, size, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}
	result = cgltf_load_buffers(&options, data, "test.glb");
	CHECK(result == cgltf_result_invalid_gltf);
	cgltf_free(data);
}

/* Custom allocator that records live and peak bytes (16-byte header per allocation). */
typedef struct mem_stats
{
	cgltf_size live;
	cgltf_size peak;
	cgltf_size limit; /* hard cap; allocations above it fail */
} mem_stats;

static void* tracking_alloc(void* user, cgltf_size size)
{
	mem_stats* stats = (mem_stats*)user;
	if (stats->limit && size > stats->limit)
	{
		return NULL;
	}
	unsigned char* block = (unsigned char*)malloc(size + 16);
	if (!block)
	{
		return NULL;
	}
	memcpy(block, &size, sizeof(size));
	stats->live += size;
	if (stats->live > stats->peak)
	{
		stats->peak = stats->live;
	}
	return block + 16;
}

static void tracking_free(void* user, void* ptr)
{
	if (!ptr)
	{
		return;
	}
	mem_stats* stats = (mem_stats*)user;
	unsigned char* block = (unsigned char*)ptr - 16;
	cgltf_size size;
	memcpy(&size, block, sizeof(size));
	stats->live -= size;
	free(block);
}

/*
 * Verifies the zero-copy / peak-memory guarantee: parsing a v3 container with
 * two BIN chunks must never allocate storage for chunk data, so peak heap use
 * stays well below the file size plus a fixed constant regardless of BIN size.
 */
static void test_v3_zero_copy_memory(void)
{
	const char* json =
		"{\"asset\":{\"version\":\"2.1\"},"
		"\"buffers\":[{\"byteLength\":64,\"chunk\":1},{\"byteLength\":64,\"chunk\":2}]}";
	unsigned char buf[2048];
	memset(buf, 0, sizeof(buf));
	cgltf_size json_len = strlen(json);

	put_u32(buf + 0, GLB_MAGIC);
	put_u32(buf + 4, 3);
	/* JSON chunk at 16. */
	put_u32(buf + 16, JSON_MAGIC);
	put_u32(buf + 20, 0);
	put_u64(buf + 24, json_len);
	memcpy(buf + 32, json, json_len);
	/* BIN chunk 1 at next 8-byte boundary, 64 bytes. */
	cgltf_size bin1_off = (32 + json_len + 7) & ~(cgltf_size)7;
	put_u32(buf + bin1_off, BIN_MAGIC);
	put_u32(buf + bin1_off + 4, 0);
	put_u64(buf + bin1_off + 8, 64);
	memset(buf + bin1_off + 16, 0xAA, 64);
	/* BIN chunk 2. */
	cgltf_size bin2_off = bin1_off + 16 + 64;
	put_u32(buf + bin2_off, BIN_MAGIC);
	put_u32(buf + bin2_off + 4, 0);
	put_u64(buf + bin2_off + 8, 64);
	memset(buf + bin2_off + 16, 0xBB, 64);
	cgltf_size end = bin2_off + 16 + 64;
	put_u64(buf + 8, end);

	mem_stats stats;
	memset(&stats, 0, sizeof(stats));
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	options.memory.alloc_func = tracking_alloc;
	options.memory.free_func = tracking_free;
	options.memory.user_data = &stats;

	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, end, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}
	CHECK(data->glb.chunks_count == 3);
	result = cgltf_load_buffers(&options, data, "test.glb");
	CHECK(result == cgltf_result_success);
	/* Borrowed pointers: buffers point inside the input buffer, no chunk-data copy. */
	CHECK(data->buffers[0].data == buf + bin1_off + 16);
	CHECK(data->buffers[1].data == buf + bin2_off + 16);
	/* Peak allocation is metadata-only; far below file size plus a constant. */
	CHECK(stats.peak < end + 4096);
	CHECK(stats.peak < (cgltf_size)4096); /* no allocation proportional to BIN size */
	cgltf_free(data);
	CHECK(stats.live == 0);
}

/* A failing allocator on the chunk array must surface out_of_memory, not crash. */
static void test_v3_allocator_failure(void)
{
	unsigned char buf[512];
	cgltf_size size = build_glb_v3(buf, sizeof(buf), k_json, NULL, 0);

	mem_stats stats;
	memset(&stats, 0, sizeof(stats));
	stats.limit = 1; /* reject essentially every allocation, including the chunk array */
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	options.memory.alloc_func = tracking_alloc;
	options.memory.free_func = tracking_free;
	options.memory.user_data = &stats;

	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, buf, size, &data);
	CHECK(result == cgltf_result_out_of_memory);
	CHECK(data == NULL);
	CHECK(stats.live == 0);
}

int main(void)
{
	test_v2_regression();
	test_v2_with_bin();
	test_v3_basic();
	test_v3_with_bin_and_padding();
	test_v3_buffer_chunk_binding();
	test_v3_implicit_bin_binding();
	test_truncated_inputs();
	test_huge_declared_lengths();
	test_corrupt_chunks();
	test_v3_chunk_interleaving();
	test_v3_invalid_chunk_reference();
	test_v3_zero_copy_memory();
	test_v3_allocator_failure();

	printf("Tests run: %d, failures: %d\n", g_num_tests, g_num_failed);
	return g_num_failed ? 1 : 0;
}
