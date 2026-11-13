// End-to-end verification on the real Khronos Avocado asset:
// decodes the KHR_mesh_quantization variant through cgltf's single shared
// accessor read path and compares it against the floating-point reference.
//
// gltfpack bakes the reference node's 180-degree Y rotation into the quantized
// vertices and reorders them, so comparison is done as a rotation-corrected
// nearest-neighbor point cloud rather than an index-by-index diff.
//
// Usage: verify_avocado <float.gltf> <quantized.gltf>
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "../../cgltf_write.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool cond, const char* what)
{
	if (!cond) { printf("FAIL: %s\n", what); ++g_failures; }
}

static cgltf_data* load(const char* path)
{
	cgltf_options options = {};
	cgltf_data* data = NULL;
	cgltf_result r = cgltf_parse_file(&options, path, &data);
	if (r != cgltf_result_success) { printf("parse failed %d for %s\n", r, path); return NULL; }
	r = cgltf_load_buffers(&options, data, path);
	if (r != cgltf_result_success) { printf("load failed %d for %s\n", r, path); cgltf_free(data); return NULL; }
	r = cgltf_validate(data);
	if (r != cgltf_result_success) { printf("validate failed %d for %s\n", r, path); cgltf_free(data); return NULL; }
	return data;
}

static const cgltf_accessor* find_attr(const cgltf_primitive* prim, cgltf_attribute_type type)
{
	return cgltf_find_accessor(prim, type, 0);
}

struct Vec
{
	double x[4];
};

// Reads one element, optionally applying the 180-degree Y rotation (x,z negated).
static Vec read_vec(const cgltf_accessor* a, cgltf_size v, int comps, bool flip_xz)
{
	cgltf_float f[16];
	cgltf_accessor_read_float(a, v, f, 16);
	Vec out;
	for (int c = 0; c < comps; ++c) out.x[c] = f[c];
	if (flip_xz) { out.x[0] = -out.x[0]; if (comps >= 3) out.x[2] = -out.x[2]; }
	return out;
}

static double dist_sq(const Vec& a, const Vec& b, int comps)
{
	double d = 0;
	for (int c = 0; c < comps; ++c) d += (a.x[c] - b.x[c]) * (a.x[c] - b.x[c]);
	return d;
}

// For every decoded quantized element, find the nearest reference element;
// returns the worst nearest-neighbor error, independent of vertex ordering.
static double worst_nearest(const cgltf_accessor* ra, const cgltf_accessor* qa, int comps,
	const std::vector<Vec>& ref_pts, bool flip_xz, cgltf_size count)
{
	(void)ra;
	double worst = 0;
	for (cgltf_size i = 0; i < count; ++i)
	{
		Vec q = read_vec(qa, i, comps, flip_xz);
		double best = 1e30;
		for (size_t j = 0; j < ref_pts.size(); ++j)
			best = std::min(best, dist_sq(q, ref_pts[j], comps));
		worst = std::max(worst, std::sqrt(best));
	}
	return worst;
}

int main(int argc, char** argv)
{
	if (argc < 3) { printf("usage: %s <float.gltf> <quantized.gltf>\n", argv[0]); return 2; }

	cgltf_data* ref = load(argv[1]);
	cgltf_data* quant = load(argv[2]);
	if (!ref || !quant) return 1;

	check(cgltf_data_has_extension(quant, "KHR_mesh_quantization") == 1, "quantization extension detected");
	check(cgltf_data_has_extension(ref, "KHR_mesh_quantization") == 0, "float reference has no quantization extension");

	const cgltf_primitive* rp = &ref->meshes[0].primitives[0];
	const cgltf_primitive* qp = &quant->meshes[0].primitives[0];

	// The quantized node's local matrix carries the uniform dequant scale/translation.
	cgltf_float qm[16];
	cgltf_node_transform_world(&quant->nodes[0], qm);
	const double qscale = qm[0];
	const double qtrans[3] = {qm[12], qm[13], qm[14]};

	// UV dequantization declared via KHR_texture_transform.
	double uv_scale[2] = {1, 1}, uv_offset[2] = {0, 0};
	const cgltf_texture_view* bcolor = &quant->materials[0].pbr_metallic_roughness.base_color_texture;
	if (bcolor->has_transform)
	{
		uv_scale[0] = bcolor->transform.scale[0];
		uv_scale[1] = bcolor->transform.scale[1];
		uv_offset[0] = bcolor->transform.offset[0];
		uv_offset[1] = bcolor->transform.offset[1];
	}

	cgltf_size count = find_attr(qp, cgltf_attribute_type_position)->count;
	check(find_attr(rp, cgltf_attribute_type_position)->count == count, "vertex counts match");

	// Build rotation-corrected, dequantized position clouds in mesh-local space.
	const cgltf_accessor* rpos = find_attr(rp, cgltf_attribute_type_position);
	const cgltf_accessor* qpos = find_attr(qp, cgltf_attribute_type_position);
	std::vector<Vec> ref_pos;
	ref_pos.reserve(count);
	for (cgltf_size v = 0; v < count; ++v)
		ref_pos.push_back(read_vec(rpos, v, 3, false));

	double worst_pos = 0, sum_pos = 0;
	for (cgltf_size i = 0; i < count; ++i)
	{
		cgltf_float raw[3];
		cgltf_accessor_read_float(qpos, i, raw, 3);
		// Dequantize via node scale/translation, then apply the baked 180-degree Y rotation.
		Vec q;
		q.x[0] = -(raw[0] * qscale + qtrans[0]);
		q.x[1] =   raw[1] * qscale + qtrans[1];
		q.x[2] = -(raw[2] * qscale + qtrans[2]);
		double best = 1e30;
		for (size_t j = 0; j < ref_pos.size(); ++j)
			best = std::min(best, dist_sq(q, ref_pos[j], 3));
		best = std::sqrt(best);
		worst_pos = std::max(worst_pos, best);
		sum_pos += best;
	}

	// NORMAL / TANGENT: unit-length vectors, rotation-corrected nearest-neighbor.
	const cgltf_accessor* rn = find_attr(rp, cgltf_attribute_type_normal);
	const cgltf_accessor* qn = find_attr(qp, cgltf_attribute_type_normal);
	const cgltf_accessor* rt = find_attr(rp, cgltf_attribute_type_tangent);
	const cgltf_accessor* qt = find_attr(qp, cgltf_attribute_type_tangent);

	std::vector<Vec> ref_n, ref_t;
	for (cgltf_size v = 0; v < count; ++v)
	{
		Vec n = read_vec(rn, v, 3, false);
		Vec t = read_vec(rt, v, 3, false);
		ref_n.push_back(n);
		ref_t.push_back(t);
	}
	double worst_n = worst_nearest(rn, qn, 3, ref_n, true, count);
	double worst_t = worst_nearest(rt, qt, 3, ref_t, true, count);

	// TEXCOORD: dequantize via scale/offset, then nearest-neighbor in 2D.
	const cgltf_accessor* ru = find_attr(rp, cgltf_attribute_type_texcoord);
	const cgltf_accessor* qu = find_attr(qp, cgltf_attribute_type_texcoord);
	std::vector<Vec> ref_u;
	for (cgltf_size v = 0; v < count; ++v)
		ref_u.push_back(read_vec(ru, v, 2, false));
	double worst_uv = 0;
	for (cgltf_size i = 0; i < count; ++i)
	{
		cgltf_float raw[2];
		cgltf_accessor_read_float(qu, i, raw, 2);
		Vec q;
		q.x[0] = raw[0] * uv_scale[0] + uv_offset[0];
		q.x[1] = raw[1] * uv_scale[1] + uv_offset[1];
		double best = 1e30;
		for (size_t j = 0; j < ref_u.size(); ++j)
			best = std::min(best, dist_sq(q, ref_u[j], 2));
		worst_uv = std::max(worst_uv, std::sqrt(best));
	}

	printf("vertices compared (nearest-neighbor, order-independent): %zu\n", size_t(count));
	printf("POSITION : worst=%.3e  mean=%.3e  (mesh span ~0.04, 16-bit)\n", worst_pos, sum_pos / count);
	printf("NORMAL   : worst=%.3e  (8-bit unit-vector bound 2/127=%.3e)\n", worst_n, 2.0 / 127);
	printf("TANGENT  : worst=%.3e\n", worst_t);
	printf("TEXCOORD : worst=%.3e\n", worst_uv);

	check(worst_pos <= 2e-5, "position error bounded");
	check(worst_n <= 2.0 / 127 + 1e-6, "normal error bounded by one BYTE step");
	check(worst_t <= 2.0 / 127 + 1e-6, "tangent error bounded by one BYTE step");
	check(worst_uv <= 5e-4, "texcoord error bounded");

	// Write round-trip of the real quantized asset. The source is an external .gltf whose
	// bytes live in buffers[0].data while data->bin is populated only when parsing a GLB, so
	// both the embedded BIN pointer and the (spec-forbidden on GLB) buffer uri are swapped in
	// for the write and restored afterwards, leaving the parsed object untouched.
	cgltf_options wopt = {};
	wopt.type = cgltf_file_type_glb;
	const char* out_path = "avocado_roundtrip.glb";
	const void* src_bin = quant->buffers[0].data;
	cgltf_size src_bin_size = quant->buffers[0].size;
	cgltf_size saved_bin_size = quant->bin_size;
	const void* saved_bin = quant->bin;
	char* saved_uri = quant->buffers[0].uri;
	quant->bin = src_bin;
	quant->bin_size = src_bin_size;
	quant->buffers[0].uri = NULL;
	check(cgltf_write_file(&wopt, out_path, quant) == cgltf_result_success, "write quantized asset");
	quant->bin = saved_bin;
	quant->bin_size = saved_bin_size;
	quant->buffers[0].uri = saved_uri;

	// Re-read the written, now spec-conformant GLB from memory.
	FILE* rt_file = fopen(out_path, "rb");
	check(rt_file != NULL, "open round-trip GLB");
	std::vector<uint8_t> rt_bytes;
	if (rt_file)
	{
		fseek(rt_file, 0, SEEK_END);
		long sz = ftell(rt_file);
		fseek(rt_file, 0, SEEK_SET);
		rt_bytes.resize(sz);
		fread(rt_bytes.data(), 1, sz, rt_file);
		fclose(rt_file);
	}
	cgltf_options ropt = {};
	cgltf_data* rtd = NULL;
	cgltf_result rtr = cgltf_parse(&ropt, rt_bytes.data(), rt_bytes.size(), &rtd);
	if (rtr == cgltf_result_success) rtr = cgltf_load_buffers(&ropt, rtd, NULL);
	if (rtr == cgltf_result_success) rtr = cgltf_validate(rtd);
	check(rtr == cgltf_result_success, "round-trip reparse/validate");
	if (rtr == cgltf_result_success)
	{
		check(cgltf_data_has_extension(rtd, "KHR_mesh_quantization") == 1, "round-trip keeps extension used");
		bool required = false;
		for (cgltf_size i = 0; i < rtd->extensions_required_count; ++i)
			if (strcmp(rtd->extensions_required[i], "KHR_mesh_quantization") == 0) required = true;
		check(required, "round-trip keeps extension required");
		check(rtd->bin_size >= src_bin_size && memcmp(rtd->bin, src_bin, src_bin_size) == 0,
			"round-trip buffer bytes preserved");
		const cgltf_primitive* rtp = &rtd->meshes[0].primitives[0];
		const cgltf_accessor* qa = find_attr(qp, cgltf_attribute_type_normal);
		const cgltf_accessor* ra = find_attr(rtp, cgltf_attribute_type_normal);
		double rt_max = 0;
		for (cgltf_size v = 0; v < count; ++v)
		{
			cgltf_float a[16], b[16];
			cgltf_accessor_read_float(qa, v, a, 16);
			cgltf_accessor_read_float(ra, v, b, 16);
			for (int c = 0; c < 3; ++c) rt_max = std::max(rt_max, double(std::fabs(a[c] - b[c])));
		}
		check(rt_max == 0.0, "round-trip decoded values bit-identical");
	}
	if (rtd) cgltf_free(rtd);
	std::remove(out_path);

	cgltf_free(quant);
	cgltf_free(ref);

	if (g_failures == 0) { printf("Avocado end-to-end verification passed.\n"); return 0; }
	printf("%d check(s) failed.\n", g_failures);
	return 1;
}
