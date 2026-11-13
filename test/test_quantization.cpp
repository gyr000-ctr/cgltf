/*
 * KHR_mesh_quantization acceptance tests:
 *  1. dequantized values match the spec formulas with a bounded error;
 *  2. write -> parse round trip preserves the extension and its values;
 *  3. malformed quantization parameters are rejected by cgltf_validate;
 *  4. an accessor without the extension behaves exactly as before.
 */
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "../cgltf_write.h"

#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond, message) \
	do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); ++failures; } } while (0)

static std::string slurp(const char* path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static cgltf_data* parse_validate(const char* path)
{
	cgltf_options options = {};
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path, &data);
	if (result != cgltf_result_success)
	{
		return NULL;
	}
	result = cgltf_load_buffers(&options, data, path);
	if (result != cgltf_result_success)
	{
		cgltf_free(data);
		return NULL;
	}
	result = cgltf_validate(data);
	if (result != cgltf_result_success)
	{
		cgltf_free(data);
		return NULL;
	}
	return data;
}

/* Load the flat expected-value companion produced by gen_quantization_assets.py. */
static std::vector<std::vector<float> > load_expected(const std::string& flat,
                                                      const std::string& name)
{
	std::ifstream f(flat.c_str());
	std::string file;
	size_t count = 0, comps = 0;
	while (f >> file >> count >> comps)
	{
		if (file == name)
		{
			std::vector<std::vector<float> > rows(count, std::vector<float>(comps));
			for (size_t i = 0; i < count; ++i)
				for (size_t c = 0; c < comps; ++c)
					f >> rows[i][c];
			return rows;
		}
		// skip this block's value rows
		for (size_t skip = 0; skip < count * comps; ++skip)
		{
			float dummy;
			f >> dummy;
		}
	}
	return std::vector<std::vector<float> >();
}

static const char* VALID_FILES[] =
{
	"quant_i8.gltf", "quant_u8.gltf", "quant_i16.gltf", "quant_u16.gltf",
	"quant_float_storage.gltf", "quant_i8.glb", "quant_srgb_color.gltf",
};

static const char* INVALID_FILES[] =
{
	"invalid_width_mismatch.gltf",
	"invalid_unsigned_int.gltf",
	"invalid_not_declared.gltf",
	"invalid_matrix.gltf",
	"invalid_srgb.gltf",
	"invalid_float_unsigned.gltf",
};

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("usage: %s <asset-dir> <work-dir>\n", argv[0]);
		return 2;
	}
	std::string asset_dir = argv[1];
	std::string work_dir = argv[2];

	std::string flat = asset_dir + "/expected_flat.txt";

	// ---- 1. bounded dequantization error + declared range -----------------
	for (size_t a = 0; a < sizeof(VALID_FILES) / sizeof(VALID_FILES[0]); ++a)
	{
		std::string path = asset_dir + "/" + VALID_FILES[a];
		std::vector<std::vector<float> > expected = load_expected(flat, VALID_FILES[a]);
		CHECK(!expected.empty(), "expected values loaded");
		cgltf_data* data = parse_validate(path.c_str());
		CHECK(data != NULL, VALID_FILES[a]);
		if (!data) continue;

		CHECK(data->accessors_count == 1, "single accessor");
		cgltf_accessor* acc = &data->accessors[0];
		CHECK(acc->has_mesh_quantization, "extension flag present");
		CHECK(data->extensions_used_count >= 1, "extensionsUsed populated");

		bool declared = false;
		for (cgltf_size e = 0; e < data->extensions_used_count; ++e)
			declared |= strcmp(data->extensions_used[e], "KHR_mesh_quantization") == 0;
		CHECK(declared, "KHR_mesh_quantization in extensionsUsed");

		const std::vector<std::vector<float> >& expected_ref = expected;
		CHECK(acc->count == expected_ref.size(), "count matches manifest");

		bool unsigned_storage = strstr(VALID_FILES[a], "u8") || strstr(VALID_FILES[a], "u16");
		/* Exact reachable extrema of each quantization volume. Signed minima
		 * extend slightly past -1 by spec (-128/127, -32768/32767). */
		float lo, hi;
		switch (acc->mesh_quantization.quantized_bits)
		{
			case cgltf_component_type_r_8:  lo = -128.0f / 127.0f;   hi = 1.0f; break;
			case cgltf_component_type_r_16: lo = -32768.0f / 32767.0f; hi = 1.0f; break;
			case cgltf_component_type_r_8u:
			case cgltf_component_type_r_16u: lo = 0.0f; hi = 1.0f; break;
			default: lo = -FLT_MAX; hi = FLT_MAX; break; /* FLOAT storage */
		}
		float max_err = 0.0f;
		for (cgltf_size i = 0; i < acc->count; ++i)
		{
			float got[16] = {};
			cgltf_bool ok = cgltf_accessor_read_float(acc, i, got, 16);
			CHECK(ok, "cgltf_accessor_read_float succeeds");
			for (size_t c = 0; c < expected_ref[i].size(); ++c)
			{
				float want = expected_ref[i][c];
				float err = fabsf(got[c] - want);
				max_err = fmaxf(max_err, err);
				// float32 evaluation of the exact spec formula: tight bound.
				CHECK(err <= 1e-6f + 1e-7f * fabsf(want), "dequant error bounded");
				// declared range of the quantization volume.
				CHECK(got[c] >= lo - 1e-7f && got[c] <= hi + 1e-7f,
				      unsigned_storage ? "unsigned range [0,1]" : "signed quantized range");
			}
		}
		printf("%-24s count=%u max_abs_err=%.3g\n", VALID_FILES[a],
		       (unsigned)acc->count, max_err);

		// ---- 2. write -> parse round trip preserves everything -----------
		cgltf_options options = {};
		bool is_glb = strstr(VALID_FILES[a], ".glb") != NULL;
		if (is_glb)
			options.type = cgltf_file_type_glb;
		std::string out_path = work_dir + "/rt_" + VALID_FILES[a];
		cgltf_result wr = cgltf_write_file(&options, out_path.c_str(), data);
		CHECK(wr == cgltf_result_success, "write succeeds");

		if (!is_glb)
		{
			std::string text = slurp(out_path.c_str());
			CHECK(text.find("KHR_mesh_quantization") != std::string::npos,
			      "extension serialized to JSON");
			CHECK(text.find("quantizedBits") != std::string::npos,
			      "quantizedBits serialized to JSON");
			if (acc->mesh_quantization.color_space ==
			    cgltf_mesh_quantization_color_space_srgb)
			{
				CHECK(text.find("\"colorSpace\": \"srgb\"") != std::string::npos,
				      "srgb colorSpace serialized to JSON");
			}
		}

		cgltf_data* data2 = parse_validate(out_path.c_str());
		CHECK(data2 != NULL, "round-trip file validates");
		if (data2)
		{
			cgltf_accessor* acc2 = &data2->accessors[0];
			CHECK(acc2->has_mesh_quantization, "extension flag round trips");
			CHECK(acc2->mesh_quantization.quantized_bits ==
			      acc->mesh_quantization.quantized_bits, "quantizedBits round trips");
			CHECK(acc2->mesh_quantization.color_space ==
			      acc->mesh_quantization.color_space, "colorSpace round trips");
			CHECK(acc2->count == acc->count, "count round trips");
			for (cgltf_size i = 0; i < acc->count; ++i)
			{
				float a0[16] = {}, a1[16] = {};
				cgltf_accessor_read_float(acc, i, a0, 16);
				cgltf_accessor_read_float(acc2, i, a1, 16);
				for (cgltf_size c = 0; c < expected_ref[i].size(); ++c)
					CHECK(a0[c] == a1[c], "dequantized values identical after round trip");
			}
			cgltf_free(data2);
		}
		cgltf_free(data);
	}

	// ---- 3. illegal quantization parameters are rejected ------------------
	for (size_t a = 0; a < sizeof(INVALID_FILES) / sizeof(INVALID_FILES[0]); ++a)
	{
		std::string path = asset_dir + "/" + INVALID_FILES[a];
		cgltf_options options = {};
		cgltf_data* data = NULL;
		cgltf_result pr = cgltf_parse_file(&options, path.c_str(), &data);
		cgltf_result vr = cgltf_result_success;
		if (pr == cgltf_result_success)
		{
			vr = cgltf_load_buffers(&options, data, path.c_str());
			if (vr == cgltf_result_success)
				vr = cgltf_validate(data);
			cgltf_free(data);
		}
		bool rejected = pr != cgltf_result_success || vr != cgltf_result_success;
		CHECK(rejected, INVALID_FILES[a]);
		printf("%-24s rejected (parse=%d validate=%d)\n", INVALID_FILES[a],
		       (int)pr, (int)vr);
	}

	// ---- 4. unquantized accessor: read path unchanged ---------------------
	{
		std::string path = asset_dir + "/plain.gltf";
		cgltf_data* data = parse_validate(path.c_str());
		CHECK(data != NULL, "plain.gltf validates");
		if (data)
		{
			cgltf_accessor* acc = &data->accessors[0];
			CHECK(!acc->has_mesh_quantization, "no extension flag on plain accessor");
			CHECK(acc->mesh_quantization.quantized_bits == cgltf_component_type_invalid,
			      "quantizedBits defaults to invalid");
			float got[3] = {};
			cgltf_accessor_read_float(acc, 0, got, 3);
			CHECK(got[0] == 0.25f && got[1] == 0.5f && got[2] == 0.75f,
			      "plain FLOAT values pass through unchanged");
			cgltf_free(data);
		}
	}

	if (failures)
	{
		printf("\n%d check(s) failed\n", failures);
		return 1;
	}
	printf("\nAll KHR_mesh_quantization checks passed.\n");
	return 0;
}
