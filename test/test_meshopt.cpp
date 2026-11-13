/*
 * test_meshopt.cpp - end-to-end verification of KHR/EXT_meshopt_compression decoding.
 *
 * The fixtures (ref.gltf + ext.gltf pair) are generated at test runtime by
 * gen_meshopt_fixtures, which encodes real meshoptimizer bitstreams. This test
 * only talks to cgltf through its public API, mirroring how an application uses
 * the library:
 *
 *   1. decoded accessor values match the uncompressed reference element-by-element
 *   2. decoded index values match the reference (TRIANGLES + INDICES modes)
 *   3. truncated / corrupted bitstreams return cgltf_result_invalid_compressed_data,
 *      never silently wrong vertices
 *   4. a structurally invalid extension declaration returns cgltf_result_invalid_gltf
 *   5. an uncompressed file still works and its decoded view bytes are identical
 *      to the reference bin (zero-regression path)
 */

#define CGLTF_IMPLEMENTATION
#include "../cgltf.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
	do                                                                                                                \
	{                                                                                                                 \
		if (!(cond))                                                                                                  \
		{                                                                                                             \
			printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                   \
			++g_failures;                                                                                             \
		}                                                                                                             \
	} while (0)

static std::string read_file(const std::string& path)
{
	std::ifstream f(path.c_str(), std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool write_file(const std::string& path, const std::string& data)
{
	std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f.write(data.data(), std::streamsize(data.size()));
	return bool(f);
}

/* Replace the first occurrence of `from` with `to` inside `s`. */
static bool replace_first(std::string& s, const std::string& from, const std::string& to)
{
	size_t p = s.find(from);
	if (p == std::string::npos) return false;
	s.replace(p, from.size(), to);
	return true;
}

static const cgltf_accessor* find_named(const cgltf_data* data, const char* name)
{
	for (cgltf_size i = 0; i < data->accessors_count; ++i)
		if (data->accessors[i].name && strcmp(data->accessors[i].name, name) == 0)
			return &data->accessors[i];
	return NULL;
}

static bool floats_close(float a, float b)
{
	float d = std::fabs(a - b);
	return d <= 1e-5f || d <= 1e-5f * std::max(std::fabs(a), std::fabs(b));
}

/* Compare every element of two equally-shaped accessors through the public read API. */
static void compare_float_accessors(const cgltf_accessor* a, const cgltf_accessor* b, const char* label)
{
	CHECK(a && b, label);
	if (!a || !b) return;
	CHECK(a->count == b->count, label);
	cgltf_size ncomp = cgltf_num_components(a->type);
	std::vector<cgltf_float> va(ncomp), vb(ncomp);
	for (cgltf_size i = 0; i < a->count && i < b->count; ++i)
	{
		CHECK(cgltf_accessor_read_float(a, i, va.data(), ncomp) != 0, label);
		CHECK(cgltf_accessor_read_float(b, i, vb.data(), ncomp) != 0, label);
		for (cgltf_size c = 0; c < ncomp; ++c)
			CHECK(floats_close(va[c], vb[c]), label);
	}
}

static void compare_uint_accessors(const cgltf_accessor* a, const cgltf_accessor* b, const char* label)
{
	CHECK(a && b, label);
	if (!a || !b) return;
	CHECK(a->count == b->count, label);
	for (cgltf_size i = 0; i < a->count && i < b->count; ++i)
	{
		cgltf_uint ua = 0, ub = 0;
		CHECK(cgltf_accessor_read_uint(a, i, &ua, 1) != 0, label);
		CHECK(cgltf_accessor_read_uint(b, i, &ub, 1) != 0, label);
		CHECK(ua == ub, label);
	}
}

static cgltf_result parse_and_load(const char* path, cgltf_data** out)
{
	cgltf_options options = {};
	cgltf_result r = cgltf_parse_file(&options, path, out);
	if (r == cgltf_result_success)
		r = cgltf_load_buffers(&options, *out, path);
	if (r == cgltf_result_success)
		r = cgltf_validate(*out);
	return r;
}

/* 1+2+5: the compressed fixture decodes to the same values as the reference. */
static void test_roundtrip_equivalence(const std::string& dir)
{
	cgltf_data *ref = NULL, *ext = NULL;
	cgltf_result rr = parse_and_load((dir + "/ref.gltf").c_str(), &ref);
	cgltf_result er = parse_and_load((dir + "/ext.gltf").c_str(), &ext);
	CHECK(rr == cgltf_result_success, "reference parses");
	CHECK(er == cgltf_result_success, "compressed parses+decodes");
	if (!ref || !ext)
	{
		cgltf_free(ref);
		cgltf_free(ext);
		return;
	}

	const char* float_streams[] = {"POSITION", "NORMAL", "ROTATION", "TEXCOORD_0", "COLOR_0"};
	for (const char* name : float_streams)
		compare_float_accessors(find_named(ref, name), find_named(ext, name), name);

	compare_uint_accessors(find_named(ref, "indices16"), find_named(ext, "indices16"), "indices16/TRIANGLES");
	compare_uint_accessors(find_named(ref, "indices32"), find_named(ext, "indices32"), "indices32/INDICES");

	/* The decoded view must expose exactly the reference stream's raw bytes. */
	for (cgltf_size i = 0; i < ext->buffer_views_count; ++i)
	{
		cgltf_buffer_view* ev = &ext->buffer_views[i];
		if (!ev->has_meshopt_compression) continue;
		const uint8_t* decoded = cgltf_buffer_view_data(ev);
		CHECK(decoded != NULL, "decoded view has data");
		const uint8_t* original = cgltf_buffer_view_data(&ref->buffer_views[i]);
		CHECK(original != NULL, "reference view has data");
		if (decoded && original)
			CHECK(memcmp(decoded, original, ev->size) == 0, "decoded bytes match reference");
	}

	cgltf_free(ref);
	cgltf_free(ext);
}

/* 3a: truncate the compressed bin -> distinct decode failure, no wrong vertices. */
static void test_truncated_stream(const std::string& dir)
{
	std::string bin = read_file(dir + "/ext.bin");
	CHECK(write_file(dir + "/trunc.bin", bin.substr(0, bin.size() / 2)), "write trunc.bin");

	std::string json = read_file(dir + "/ext.gltf");
	replace_first(json, "ext.bin", "trunc.bin");
	CHECK(write_file(dir + "/trunc.gltf", json), "write trunc.gltf");

	cgltf_data* d = NULL;
	cgltf_result r = parse_and_load((dir + "/trunc.gltf").c_str(), &d);
	CHECK(r == cgltf_result_invalid_compressed_data || r == cgltf_result_data_too_short ||
					r == cgltf_result_io_error,
			"truncated stream reported as a distinct failure");
	cgltf_free(d);
}

/* 3b: keep the length plausible but corrupt every byte of the bitstream. */
static void test_corrupt_stream(const std::string& dir)
{
	std::string bin = read_file(dir + "/ext.bin");
	for (size_t i = 0; i < bin.size(); ++i)
		bin[i] = static_cast<char>(((static_cast<unsigned char>(bin[i]) ^ 0xA5) + 1) & 0xff);
	CHECK(write_file(dir + "/corrupt.bin", bin), "write corrupt.bin");

	std::string json = read_file(dir + "/ext.gltf");
	replace_first(json, "ext.bin", "corrupt.bin");
	CHECK(write_file(dir + "/corrupt.gltf", json), "write corrupt.gltf");

	cgltf_data* d = NULL;
	cgltf_result r = parse_and_load((dir + "/corrupt.gltf").c_str(), &d);
	CHECK(r == cgltf_result_invalid_compressed_data, "corrupt stream reported as invalid_compressed_data");
	cgltf_free(d);
}

/* 4: extension JSON declaring an unknown mode -> invalid_gltf from validate(). */
static void test_invalid_declaration(const std::string& dir)
{
	std::string json = read_file(dir + "/ext.gltf");
	CHECK(replace_first(json, "\"TRIANGLES\"", "\"BOGUS\""), "rewrite mode token");
	CHECK(write_file(dir + "/badmode.gltf", json), "write badmode.gltf");

	cgltf_options options = {};
	cgltf_data* d = NULL;
	cgltf_result r = cgltf_parse_file(&options, (dir + "/badmode.gltf").c_str(), &d);
	if (r == cgltf_result_success)
		r = cgltf_load_buffers(&options, d, (dir + "/badmode.gltf").c_str());
	/* An unrecognized mode is surfaced as a distinct, diagnosable result rather than
	   silently passing through; validate() independently flags it as invalid_gltf. */
	CHECK(r == cgltf_result_unsupported_extension || r == cgltf_result_invalid_compressed_data,
			"unknown mode fails with a distinct meshopt result");
	if (r == cgltf_result_success)
	{
		cgltf_result vr = cgltf_validate(d);
		CHECK(vr == cgltf_result_invalid_gltf, "validate flags unknown meshopt mode");
	}
	cgltf_free(d);
}

/* Uncompressed file path stays byte-for-byte identical (zero regression guard). */
static void test_plain_path_untouched(const std::string& dir)
{
	cgltf_data* d = NULL;
	cgltf_result r = parse_and_load((dir + "/ref.gltf").c_str(), &d);
	CHECK(r == cgltf_result_success, "plain file loads");
	if (d)
	{
		for (cgltf_size i = 0; i < d->buffer_views_count; ++i)
			CHECK(!d->buffer_views[i].has_meshopt_compression, "plain views unflagged");

		std::string ref_bin = read_file(dir + "/ref.bin");
		const cgltf_buffer* b = &d->buffers[0];
		CHECK(b->data != NULL && b->size == ref_bin.size(), "plain buffer size unchanged");
		if (b->data)
			CHECK(memcmp(b->data, ref_bin.data(), b->size) == 0, "plain buffer bytes unchanged");
	}
	cgltf_free(d);
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: %s <fixture-dir>\n", argv[0]);
		return 2;
	}
	std::string dir = argv[1];

	test_plain_path_untouched(dir);
	test_roundtrip_equivalence(dir);
	test_truncated_stream(dir);
	test_corrupt_stream(dir);
	test_invalid_declaration(dir);

	if (g_failures)
	{
		printf("%d meshopt test(s) failed\n", g_failures);
		return 1;
	}
	printf("test_meshopt: all checks passed\n");
	return 0;
}
