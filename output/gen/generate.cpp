// Test data generator for meshopt compression support in cgltf.
// Produces paired uncompressed/reference and meshopt-compressed glTF files
// using the authoritative meshoptimizer encoder, plus a manifest for the
// corruption tests. All randomness is deterministic.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <string>

#include "meshoptimizer.h"

static uint32_t rng_state = 0x12345678u;
static uint32_t rng()
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}
static float frand(float lo, float hi) { return lo + (hi - lo) * (rng() / 4294967296.0f); }

static void write_file(const char* path, const std::vector<uint8_t>& data)
{
	FILE* f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	fwrite(data.data(), 1, data.size(), f);
	fclose(f);
}

static void append(std::vector<uint8_t>& dst, const void* src, size_t size)
{
	const uint8_t* p = (const uint8_t*)src;
	dst.insert(dst.end(), p, p + size);
}

struct View
{
	std::string name;
	std::vector<uint8_t> raw;       // uncompressed bytes (reference storage)
	std::vector<uint8_t> encoded;   // meshopt bitstream
	size_t stride;
	size_t count;
	const char* mode;
	const char* filter;
};

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
		return 1;
	}
	std::string out = argv[1];

	const size_t vertex_count = 510; // divisible by 16-ish blocks, > 256 to exercise multi-block
	const size_t tri_count = 510;    // 170 triangles, divisible by 3
	const size_t seq_count = 128;

	std::vector<View> views;

	// 1. POSITION: VEC3 float, stride 12, ATTRIBUTES, no filter
	{
		View v;
		v.name = "position";
		v.stride = 12;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "NONE";
		std::vector<float> f(vertex_count * 3);
		for (size_t i = 0; i < vertex_count * 3; ++i)
			f[i] = frand(-10.0f, 10.0f);
		v.raw.resize(f.size() * 4);
		memcpy(v.raw.data(), f.data(), v.raw.size());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 12));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), f.data(), vertex_count, 12);
		v.encoded.resize(n);
		views.push_back(v);
	}

	// 2. Interleaved POSITION+NORMAL quantized: stride 16 (float3 + pad byte group), no filter
	{
		View v;
		v.name = "interleaved16";
		v.stride = 16;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "NONE";
		v.raw.assign(vertex_count * 16, 0);
		for (size_t i = 0; i < vertex_count; ++i)
		{
			float* xyz = (float*)(v.raw.data() + i * 16);
			xyz[0] = frand(-1.0f, 1.0f);
			xyz[1] = frand(-1.0f, 1.0f);
			xyz[2] = frand(-1.0f, 1.0f);
		}
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 16));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), v.raw.data(), vertex_count, 16);
		v.encoded.resize(n);
		views.push_back(v);
	}

	// 3. OCTAHEDRAL normals, stride 4, int8 quantized
	{
		View v;
		v.name = "oct4";
		v.stride = 4;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "OCTAHEDRAL";
		std::vector<float> unit(vertex_count * 4);
		for (size_t i = 0; i < vertex_count; ++i)
		{
			float x = frand(-1, 1), y = frand(-1, 1), z = frand(-1, 1);
			float l = sqrtf(x * x + y * y + z * z);
			unit[i * 4 + 0] = x / l;
			unit[i * 4 + 1] = y / l;
			unit[i * 4 + 2] = z / l;
			unit[i * 4 + 3] = 0.0f;
		}
		std::vector<int8_t> quant(vertex_count * 4, 0);
		meshopt_encodeFilterOct(quant.data(), vertex_count, 4, 8, unit.data());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 4));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 4);
		v.encoded.resize(n);
		// reference stores the post-filter bytes, as a conforming decoder would produce them
		v.raw.resize(vertex_count * 4);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterOct(v.raw.data(), vertex_count, 4);
		views.push_back(v);
	}

	// 4. OCTAHEDRAL normals, stride 8, int16
	{
		View v;
		v.name = "oct8";
		v.stride = 8;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "OCTAHEDRAL";
		std::vector<float> unit(vertex_count * 4);
		for (size_t i = 0; i < vertex_count; ++i)
		{
			float x = frand(-1, 1), y = frand(-1, 1), z = frand(-1, 1);
			float l = sqrtf(x * x + y * y + z * z);
			unit[i * 4 + 0] = x / l;
			unit[i * 4 + 1] = y / l;
			unit[i * 4 + 2] = z / l;
			unit[i * 4 + 3] = 0.0f;
		}
		std::vector<int16_t> quant(vertex_count * 4, 0);
		meshopt_encodeFilterOct(quant.data(), vertex_count, 8, 16, unit.data());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 8));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 8);
		v.encoded.resize(n);
		v.raw.resize(vertex_count * 8);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterOct(v.raw.data(), vertex_count, 8);
		views.push_back(v);
	}

	// 5. QUATERNION, stride 8, int16
	{
		View v;
		v.name = "quat8";
		v.stride = 8;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "QUATERNION";
		std::vector<float> q(vertex_count * 4);
		for (size_t i = 0; i < vertex_count; ++i)
		{
			float x = frand(-1, 1), y = frand(-1, 1), z = frand(-1, 1), w = frand(-1, 1);
			float l = sqrtf(x * x + y * y + z * z + w * w);
			q[i * 4 + 0] = x / l;
			q[i * 4 + 1] = y / l;
			q[i * 4 + 2] = z / l;
			q[i * 4 + 3] = w / l;
		}
		std::vector<int16_t> quant(vertex_count * 4, 0);
		meshopt_encodeFilterQuat(quant.data(), vertex_count, 8, 16, q.data());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 8));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 8);
		v.encoded.resize(n);
		v.raw.resize(vertex_count * 8);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterQuat(v.raw.data(), vertex_count, 8);
		views.push_back(v);
	}

	// 6. EXPONENTIAL, stride 12 (3 floats), VEC3
	{
		View v;
		v.name = "exp12";
		v.stride = 12;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "EXPONENTIAL";
		std::vector<float> f(vertex_count * 3);
		for (size_t i = 0; i < vertex_count * 3; ++i)
			f[i] = frand(-1000.0f, 1000.0f);
		std::vector<uint32_t> quant(f.size());
		meshopt_encodeFilterExp(quant.data(), vertex_count, 12, 16, f.data(), meshopt_EncodeExpSeparate);
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 12));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 12);
		v.encoded.resize(n);
		v.raw.resize(vertex_count * 12);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterExp(v.raw.data(), vertex_count, 12);
		views.push_back(v);
	}

	// 7. COLOR, stride 4, uint8 RGBA
	{
		View v;
		v.name = "color4";
		v.stride = 4;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "COLOR";
		std::vector<float> rgba(vertex_count * 4);
		for (size_t i = 0; i < vertex_count * 4; ++i)
			rgba[i] = frand(0.0f, 1.0f);
		std::vector<uint8_t> quant(vertex_count * 4, 0);
		meshopt_encodeFilterColor(quant.data(), vertex_count, 4, 8, rgba.data());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 4));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 4);
		v.encoded.resize(n);
		v.raw.resize(vertex_count * 4);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterColor(v.raw.data(), vertex_count, 4);
		views.push_back(v);
	}

	// 8. COLOR, stride 8, uint16 RGBA
	{
		View v;
		v.name = "color8";
		v.stride = 8;
		v.count = vertex_count;
		v.mode = "ATTRIBUTES";
		v.filter = "COLOR";
		std::vector<float> rgba(vertex_count * 4);
		for (size_t i = 0; i < vertex_count * 4; ++i)
			rgba[i] = frand(0.0f, 1.0f);
		std::vector<uint16_t> quant(vertex_count * 4, 0);
		meshopt_encodeFilterColor(quant.data(), vertex_count, 8, 16, rgba.data());
		v.encoded.resize(meshopt_encodeVertexBufferBound(vertex_count, 8));
		size_t n = meshopt_encodeVertexBuffer(v.encoded.data(), v.encoded.size(), quant.data(), vertex_count, 8);
		v.encoded.resize(n);
		v.raw.resize(vertex_count * 8);
		memcpy(v.raw.data(), quant.data(), v.raw.size());
		meshopt_decodeFilterColor(v.raw.data(), vertex_count, 8);
		views.push_back(v);
	}

	// 9. TRIANGLES: ushort indices, 170 triangles
	{
		View v;
		v.name = "triangles";
		v.stride = 2;
		v.count = tri_count;
		v.mode = "TRIANGLES";
		v.filter = "NONE";
		std::vector<uint32_t> idx(tri_count);
		for (size_t t = 0; t < tri_count; t += 3)
		{
			uint32_t a = rng() % vertex_count, b = rng() % vertex_count, c = rng() % vertex_count;
			// inject some shared-edge / strip-like structure
			if (t >= 3 && (rng() & 1)) { a = idx[t - 2]; b = idx[t - 1]; }
			idx[t + 0] = a;
			idx[t + 1] = b;
			idx[t + 2] = c;
		}
		v.raw.resize(tri_count * 2);
		for (size_t i = 0; i < tri_count; ++i)
			((uint16_t*)v.raw.data())[i] = (uint16_t)idx[i];
		v.encoded.resize(meshopt_encodeIndexBufferBound(tri_count, vertex_count));
		size_t n = meshopt_encodeIndexBuffer(v.encoded.data(), v.encoded.size(), idx.data(), tri_count);
		v.encoded.resize(n);
		/* The triangle codec may rotate vertex order while preserving winding,
		   so the authoritative reference is the decoded bitstream, not the encoder input. */
		int rc = meshopt_decodeIndexBuffer(v.raw.data(), tri_count, 2, v.encoded.data(), v.encoded.size());
		if (rc != 0) { fprintf(stderr, "ref triangle decode failed: %d\n", rc); return 1; }
		views.push_back(v);
	}

	// 10. INDICES: uint32 sequence, arbitrary values
	{
		View v;
		v.name = "sequence";
		v.stride = 4;
		v.count = seq_count;
		v.mode = "INDICES";
		v.filter = "NONE";
		std::vector<uint32_t> idx(seq_count);
		for (size_t i = 0; i < seq_count; ++i)
			idx[i] = rng() % 100000u;
		v.raw.resize(seq_count * 4);
		memcpy(v.raw.data(), idx.data(), v.raw.size());
		v.encoded.resize(meshopt_encodeIndexSequenceBound(seq_count, 100000));
		size_t n = meshopt_encodeIndexSequence(v.encoded.data(), v.encoded.size(), idx.data(), seq_count);
		v.encoded.resize(n);
		/* reference-decoded baseline, same rationale as the triangle view */
		int rc = meshopt_decodeIndexSequence(v.raw.data(), seq_count, 4, v.encoded.data(), v.encoded.size());
		if (rc != 0) { fprintf(stderr, "ref sequence decode failed: %d\n", rc); return 1; }
		views.push_back(v);
	}

	// Normalize every reference blob through the authoritative reference DECODER so that
	// raw == decode(encoded), not raw == encoder input (the triangle codec rotates winding).
	for (View& v : views)
	{
		std::vector<uint8_t> decoded(v.stride * v.count, 0);
		int rc = 0;
		if (strcmp(v.mode, "ATTRIBUTES") == 0)
		{
			rc = meshopt_decodeVertexBuffer(decoded.data(), v.count, v.stride, v.encoded.data(), v.encoded.size());
			if (rc == 0 && strcmp(v.filter, "OCTAHEDRAL") == 0)
				meshopt_decodeFilterOct(decoded.data(), v.count, v.stride);
			else if (rc == 0 && strcmp(v.filter, "QUATERNION") == 0)
				meshopt_decodeFilterQuat((int16_t*)decoded.data(), v.count, v.stride);
			else if (rc == 0 && strcmp(v.filter, "EXPONENTIAL") == 0)
				meshopt_decodeFilterExp((uint32_t*)decoded.data(), v.count, v.stride);
			else if (rc == 0 && strcmp(v.filter, "COLOR") == 0)
				meshopt_decodeFilterColor(decoded.data(), v.count, v.stride);
		}
		else if (strcmp(v.mode, "TRIANGLES") == 0)
			rc = meshopt_decodeIndexBuffer(decoded.data(), v.count, v.stride, v.encoded.data(), v.encoded.size());
		else
			rc = meshopt_decodeIndexSequence(decoded.data(), v.count, v.stride, v.encoded.data(), v.encoded.size());

		if (rc != 0) { fprintf(stderr, "reference decode failed for %s: %d\n", v.name.c_str(), rc); return 1; }
		v.raw.swap(decoded);
	}

	// Build raw reference blob and encoded blob with per-view offsets.
	std::vector<uint8_t> raw_blob, enc_blob;
	struct Loc { std::string name; size_t raw_offset, raw_size, enc_offset, enc_size, stride, count; const char *mode, *filter; };
	std::vector<Loc> locs;
	for (View& v : views)
	{
		Loc loc;
		loc.name = v.name;
		loc.raw_offset = raw_blob.size();
		loc.raw_size = v.raw.size();
		loc.stride = v.stride;
		loc.count = v.count;
		loc.mode = v.mode;
		loc.filter = v.filter;
		append(raw_blob, v.raw.data(), v.raw.size());

		loc.enc_offset = enc_blob.size();
		loc.enc_size = v.encoded.size();
		append(enc_blob, v.encoded.data(), v.encoded.size());
		locs.push_back(loc);
	}

	write_file((out + "/reference.bin").c_str(), raw_blob);
	write_file((out + "/compressed.bin").c_str(), enc_blob);

	// Emit JSON for the two glTF files and a manifest used by the corruption tests.
	FILE* ref_json = fopen((out + "/reference.gltf").c_str(), "w");
	FILE* cmp_json = fopen((out + "/compressed.gltf").c_str(), "w");
	FILE* manifest = fopen((out + "/manifest.json").c_str(), "w");

	const char* header_ref =
		"{\n  \"asset\": {\"version\": \"2.0\", \"generator\": \"meshopt-cgltf-test\"},\n"
		"  \"buffers\": [{\"uri\": \"reference.bin\", \"byteLength\": %zu}],\n"
		"  \"bufferViews\": [\n";
	const char* header_cmp =
		"{\n  \"asset\": {\"version\": \"2.0\", \"generator\": \"meshopt-cgltf-test\"},\n"
		"  \"extensionsUsed\": [\"EXT_meshopt_compression\"],\n"
		"  \"extensionsRequired\": [\"EXT_meshopt_compression\"],\n"
		"  \"buffers\": [\n"
		"    {\"byteLength\": %zu},\n"
		"    {\"uri\": \"compressed.bin\", \"byteLength\": %zu}\n"
		"  ],\n"
		"  \"bufferViews\": [\n";
	fprintf(ref_json, header_ref, raw_blob.size());
	fprintf(cmp_json, header_cmp, raw_blob.size(), enc_blob.size());

	fprintf(manifest, "{\n  \"encBuffer\": 1,\n  \"placeholderBuffer\": 0,\n  \"encBlobFile\": \"compressed.bin\",\n  \"encBlobSize\": %zu,\n  \"views\": [\n", enc_blob.size());

	for (size_t i = 0; i < locs.size(); ++i)
	{
		Loc& L = locs[i];
		const char* comma = (i + 1 < locs.size()) ? "," : "";

		fprintf(ref_json,
			"    {\"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"byteStride\": %zu}%s\n",
			L.raw_offset, L.raw_size, L.stride, comma);

		fprintf(cmp_json,
			"    {\"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"byteStride\": %zu,\n"
			"     \"extensions\": {\"EXT_meshopt_compression\": {\n"
			"       \"buffer\": 1, \"byteOffset\": %zu, \"byteLength\": %zu, \"byteStride\": %zu,\n"
			"       \"count\": %zu, \"mode\": \"%s\", \"filter\": \"%s\"}}}%s\n",
			L.raw_offset, L.raw_size, L.stride,
			L.enc_offset, L.enc_size, L.stride, L.count, L.mode, L.filter, comma);

		fprintf(manifest,
			"    {\"name\": \"%s\", \"encOffset\": %zu, \"encSize\": %zu, \"stride\": %zu, \"count\": %zu, \"mode\": \"%s\", \"filter\": \"%s\"}%s\n",
			L.name.c_str(), L.enc_offset, L.enc_size, L.stride, L.count, L.mode, L.filter, comma);
	}

	// Accessors: map each view to a component/type combination.
	struct Acc { const char* view; int component_type; const char* type; };
	const Acc accs[] = {
		{"position", 5126, "VEC3"},
		{"interleaved16", 5126, "VEC3"},
		{"oct4", 5120, "VEC4"},
		{"oct8", 5122, "VEC4"},
		{"quat8", 5122, "VEC4"},
		{"exp12", 5126, "VEC3"},
		{"color4", 5121, "VEC4"},
		{"color8", 5123, "VEC4"},
		{"triangles", 5123, "SCALAR"},
		{"sequence", 5125, "SCALAR"},
	};
	fprintf(ref_json, "  ],\n  \"accessors\": [\n");
	fprintf(cmp_json, "  ],\n  \"accessors\": [\n");
	for (size_t i = 0; i < locs.size(); ++i)
	{
		const char* comma = (i + 1 < locs.size()) ? "," : "";
		fprintf(ref_json, "    {\"bufferView\": %zu, \"componentType\": %d, \"type\": \"%s\", \"count\": %zu}%s\n",
			i, accs[i].component_type, accs[i].type, locs[i].count, comma);
		fprintf(cmp_json, "    {\"bufferView\": %zu, \"componentType\": %d, \"type\": \"%s\", \"count\": %zu}%s\n",
			i, accs[i].component_type, accs[i].type, locs[i].count, comma);
	}
	fprintf(ref_json, "  ]\n}\n");
	fprintf(cmp_json, "  ]\n}\n");
	fprintf(manifest, "  ]\n}\n");

	fclose(ref_json);
	fclose(cmp_json);
	fclose(manifest);

	printf("generated %zu views, raw=%zu bytes, encoded=%zu bytes\n",
		views.size(), raw_blob.size(), enc_blob.size());
	return 0;
}
