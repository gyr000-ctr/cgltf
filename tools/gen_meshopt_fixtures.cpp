/*
 * gen_meshopt_fixtures.cpp - produces a matched pair of glTF files:
 *   ref.gltf/ref.bin   plain uncompressed glTF (the byte-exact reference)
 *   ext.gltf/ext.bin   the same logical data with EXT_meshopt_compression
 *
 * Compression uses the reference meshoptimizer encoder, so the bitstreams are
 * real production streams (not hand-crafted). The verifier test only consumes
 * the files through the public cgltf API.
 *
 * Streams covered:
 *   - POSITION      ATTRIBUTES, FLOAT32, filter NONE
 *   - NORMAL        ATTRIBUTES, int16 normalized, filter OCTAHEDRAL
 *   - ROTATION      ATTRIBUTES, int16 normalized, filter QUATERNION
 *   - TEXCOORD_0    ATTRIBUTES, FLOAT32, filter EXPONENTIAL
 *   - COLOR_0       ATTRIBUTES, uint8 normalized, filter COLOR (raw pass-through)
 *   - indices (u16) TRIANGLES
 *   - indices (u32) INDICES
 */

#include "../third_party/meshoptimizer/meshoptimizer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef MESHOPT_FIXTURE_HEADER
#include MESHOPT_FIXTURE_HEADER
#endif

namespace {

const size_t kVertexCount = 96;
const size_t kTriangleCount = 24; /* indices: 72 */

struct Stream {
	std::string name;          /* JSON accessor name */
	std::string attr;          /* attribute semantic, empty for indices */
	int component_type;        /* glTF componentType */
	std::string type;          /* glTF type string */
	bool normalized = false;
	size_t stride = 0;
	std::string mode;          /* meshopt mode, empty when uncompressed-only */
	std::string filter;        /* meshopt filter, empty when NONE */
	std::vector<unsigned char> packed;    /* pre-filter bytes fed to the meshopt encoder */
	std::vector<unsigned char> raw;       /* post-filter decoded bytes (the reference truth) */
	std::vector<unsigned char> encoded;   /* meshopt bitstream */
	size_t ref_view = 0, ext_view = 0;
};

void append_u16(std::vector<unsigned char>& b, uint16_t v)
{
	b.push_back(unsigned(v) & 0xff);
	b.push_back((unsigned(v) >> 8) & 0xff);
}

void append_u32(std::vector<unsigned char>& b, uint32_t v)
{
	for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff);
}

void append_f32(std::vector<unsigned char>& b, float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	append_u32(b, u);
}

void pad4(std::vector<unsigned char>& b)
{
	while (b.size() % 4) b.push_back(0);
}

uint8_t quantize_unorm8(float v)
{
	return uint8_t(meshopt_quantizeUnorm(v, 8));
}

/* ---------------- stream builders ---------------- */

Stream make_position()
{
	Stream s;
	s.name = "POSITION";
	s.attr = "POSITION";
	s.component_type = 5126;
	s.type = "VEC3";
	s.stride = 12;
	s.mode = "ATTRIBUTES";
	for (size_t i = 0; i < kVertexCount; ++i)
	{
		float t = float(i) / kVertexCount;
		append_f32(s.raw, std::cos(t * 6.2831853f) * 2.0f);
		append_f32(s.raw, std::sin(t * 6.2831853f));
		append_f32(s.raw, t - 0.5f);
	}
	return s;
}

Stream make_normal()
{
	Stream s;
	s.name = "NORMAL";
	s.attr = "NORMAL";
	s.component_type = 5122;
	s.type = "VEC3";
	s.normalized = true;
	s.stride = 8;
	s.mode = "ATTRIBUTES";
	s.filter = "OCTAHEDRAL";
	std::vector<int16_t> packed;
	for (size_t i = 0; i < kVertexCount; ++i)
	{
		float t = float(i) / kVertexCount;
		float n[3] = {std::cos(t * 6.2831853f), std::sin(t * 6.2831853f), 0.25f};
		float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		/* oct encode: unit vector plus the 1.0f placeholder; output packed int16 xyzw */
		float f[4] = {n[0] / l, n[1] / l, n[2] / l, 1.0f};
		int16_t dst[4] = {0, 0, 0, 0};
		meshopt_encodeFilterOct(dst, 1, 8, 16, f);
		for (int k = 0; k < 4; ++k) packed.push_back(dst[k]);
	}
	/* packed feeds the bitstream encoder; running the decode filter yields the
	   post-decompression int16 stream that an uncompressed reference would hold. */
	s.packed.assign(reinterpret_cast<unsigned char*>(packed.data()),
			reinterpret_cast<unsigned char*>(packed.data() + packed.size()));
	s.raw = s.packed;
	meshopt_decodeFilterOct(s.raw.data(), kVertexCount, 8);
	return s;
}

Stream make_rotation()
{
	Stream s;
	s.name = "ROTATION";
	s.attr = "ROTATION";
	s.component_type = 5122;
	s.type = "VEC4";
	s.normalized = true;
	s.stride = 8;
	s.mode = "ATTRIBUTES";
	s.filter = "QUATERNION";
	std::vector<int16_t> packed;
	for (size_t i = 0; i < kVertexCount; ++i)
	{
		float t = float(i) / kVertexCount;
		float q[4] = {std::cos(t * 3.1f), std::sin(t * 3.1f) * 0.5f, 0.25f, 0.0f};
		float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		for (float& x : q) x /= l;
		int16_t dst[4] = {0, 0, 0, 0};
		meshopt_encodeFilterQuat(dst, 1, 8, 16, q);
		for (int k = 0; k < 4; ++k) packed.push_back(dst[k]);
	}
	s.packed.assign(reinterpret_cast<unsigned char*>(packed.data()),
			reinterpret_cast<unsigned char*>(packed.data() + packed.size()));
	s.raw = s.packed;
	meshopt_decodeFilterQuat(reinterpret_cast<short*>(s.raw.data()), kVertexCount, 8);
	return s;
}

Stream make_texcoord()
{
	Stream s;
	s.name = "TEXCOORD_0";
	s.attr = "TEXCOORD_0";
	s.component_type = 5126;
	s.type = "VEC2";
	s.stride = 8;
	s.mode = "ATTRIBUTES";
	s.filter = "EXPONENTIAL";
	for (size_t i = 0; i < kVertexCount; ++i)
	{
		float t = float(i) / kVertexCount;
		float uv[2] = {t * 3.0f - 1.5f, std::fmod(t * 7.0f, 1.0f)};
		append_f32(s.packed, uv[0]);
		append_f32(s.packed, uv[1]);
	}
	/* exp encode in place: packed now holds the 8/12-bit shared-exponent words.
	   The decoded float stream (lossy vs. the originals) is what a reference
	   glTF must contain to match what the decoder reconstructs. */
	meshopt_encodeFilterExp(s.packed.data(), kVertexCount, 8, 12,
			reinterpret_cast<const float*>(s.packed.data()), meshopt_EncodeExpSharedVector);
	s.raw = s.packed;
	meshopt_decodeFilterExp(reinterpret_cast<unsigned int*>(s.raw.data()), kVertexCount, s.stride);
	return s;
}

Stream make_color()
{
	Stream s;
	s.name = "COLOR_0";
	s.attr = "COLOR_0";
	s.component_type = 5121;
	s.type = "VEC4";
	s.normalized = true;
	s.stride = 4;
	s.mode = "ATTRIBUTES";
	s.filter = "COLOR";
	for (size_t i = 0; i < kVertexCount; ++i)
	{
		float t = float(i) / kVertexCount;
		s.raw.push_back(quantize_unorm8(t));
		s.raw.push_back(quantize_unorm8(1.0f - t));
		s.raw.push_back(quantize_unorm8(0.5f + 0.5f * std::sin(t * 6.28f)));
		s.raw.push_back(255);
	}
	return s;
}

Stream make_triangles()
{
	Stream s;
	s.name = "indices16";
	s.component_type = 5123;
	s.type = "SCALAR";
	s.stride = 2;
	s.mode = "TRIANGLES";
	/*
	 * A closed triangle-fan manifold: every adjacent pair shares an edge with
	 * consistent winding, which is the precondition the triangle codec documents
	 * (data must be vertex-cache optimized; it then preserves contents and order
	 * exactly). Each triangle is (center, ring[i], ring[i+1]).
	 */
	const uint32_t center = 0;
	const uint32_t ring_base = 1;
	std::vector<uint32_t> idx;
	for (size_t t = 0; t < kTriangleCount; ++t)
	{
		uint32_t a = ring_base + uint32_t(t);
		uint32_t b = ring_base + uint32_t((t + 1) % kTriangleCount);
		idx.push_back(center);
		idx.push_back(a);
		idx.push_back(b);
	}
	for (uint32_t v : idx) append_u16(s.raw, uint16_t(v));

	uint32_t max_idx = 0;
	for (uint32_t v : idx) max_idx = max_idx < v ? v : max_idx;
	std::vector<unsigned char> bound(meshopt_encodeIndexBufferBound(idx.size(), max_idx + 1));
	size_t n = meshopt_encodeIndexBuffer(bound.data(), bound.size(), idx.data(), idx.size());
	s.encoded.assign(bound.begin(), bound.begin() + n);
	return s;
}

Stream make_index_sequence()
{
	Stream s;
	s.name = "indices32";
	s.component_type = 5125;
	s.type = "SCALAR";
	s.stride = 4;
	s.mode = "INDICES";
	std::vector<uint32_t> seq(kTriangleCount * 3);
	for (size_t i = 0; i < seq.size(); ++i) seq[i] = uint32_t((i * 13 + 5) % kVertexCount);
	for (uint32_t v : seq) append_u32(s.raw, v);

	std::vector<unsigned char> bound(meshopt_encodeIndexSequenceBound(seq.size(), kVertexCount));
	size_t n = meshopt_encodeIndexSequence(bound.data(), bound.size(), seq.data(), seq.size());
	s.encoded.assign(bound.begin(), bound.begin() + n);
	return s;
}

void encode_vertex_stream(Stream& s)
{
	if (s.mode != "ATTRIBUTES") return;
	/* Filtered streams encode their pre-filter representation; others have packed == raw. */
	const std::vector<unsigned char>& source = s.packed.empty() ? s.raw : s.packed;
	std::vector<unsigned char> bound(meshopt_encodeVertexBufferBound(kVertexCount, s.stride));
	size_t n = meshopt_encodeVertexBuffer(bound.data(), bound.size(), source.data(), kVertexCount, s.stride);
	s.encoded.assign(bound.begin(), bound.begin() + n);
}

/* ---------------- glTF writer ---------------- */

std::string ctype_name(int ct)
{
	switch (ct)
	{
	case 5126: return "5126";
	case 5123: return "5123";
	case 5125: return "5125";
	case 5122: return "5122";
	case 5121: return "5121";
	}
	return "0";
}

void write_ref_gltf(const std::string& path, const std::vector<Stream*>& streams,
		const std::vector<size_t>& view_offsets, size_t bin_size)
{
	FILE* f = fopen(path.c_str(), "wb");
	fprintf(f, " {\n \"asset\":{\"version\":\"2.0\",\"generator\":\"meshopt-fixture\"},\n");
	fprintf(f, " \"buffers\":[{\"uri\":\"ref.bin\",\"byteLength\":%zu}],\n", bin_size);
	fprintf(f, " \"bufferViews\":[\n");
	for (size_t i = 0; i < streams.size(); ++i)
	{
		Stream* s = streams[i];
		fprintf(f, "  {\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", view_offsets[i], s->raw.size());
		if (s->attr.size()) fprintf(f, ",\"byteStride\":%zu,\"target\":34962", s->stride);
		else fprintf(f, ",\"target\":34963");
		fprintf(f, "}%s\n", i + 1 == streams.size() ? "" : ",");
	}
	fprintf(f, " ],\n \"accessors\":[\n");
	for (size_t i = 0; i < streams.size(); ++i)
	{
		Stream* s = streams[i];
		size_t count = s->stride == 0 ? 0 : (s->raw.size() / s->stride);
		fprintf(f, "  {\"name\":\"%s\",\"bufferView\":%zu,\"componentType\":%s,\"count\":%zu,\"type\":\"%s\"",
				s->name.c_str(), i, ctype_name(s->component_type).c_str(), count, s->type.c_str());
		if (s->normalized) fprintf(f, ",\"normalized\":true");
		fprintf(f, "}%s\n", i + 1 == streams.size() ? "" : ",");
	}
	fprintf(f, " ],\n \"meshes\":[{\"primitives\":[{\"attributes\":{");
	bool first = true;
	size_t vi = 0;
	for (size_t i = 0; i < streams.size(); ++i)
	{
		if (!streams[i]->attr.size()) continue;
		if (!first) fprintf(f, ",");
		fprintf(f, "\"%s\":%zu", streams[i]->attr.c_str(), i);
		first = false;
		++vi;
	}
	fprintf(f, "},\"indices\":%zu,\"material\":0}],\"extras\":{\"seq\":%zu}}],\n",
			streams.size() - 2, streams.size() - 1);
	fprintf(f, " \"materials\":[{}]\n}\n");
	fclose(f);
}

void write_ext_gltf(const std::string& path, const std::vector<Stream*>& streams,
		const std::vector<size_t>& ref_offsets, const std::vector<size_t>& cmp_offsets,
		size_t uncompressed_bin_size, size_t compressed_bin_size)
{
	FILE* f = fopen(path.c_str(), "wb");
	fprintf(f, "{\n \"asset\":{\"version\":\"2.0\",\"generator\":\"meshopt-fixture\"},\n");
	fprintf(f, " \"extensionsUsed\":[\"EXT_meshopt_compression\"],\"extensionsRequired\":[\"EXT_meshopt_compression\"],\n");
	/* buffer 0: compressed bitstreams; buffer 1: uncompressed fallback layout (spec layout) */
	fprintf(f, " \"buffers\":[{\"uri\":\"ext.bin\",\"byteLength\":%zu},{\"uri\":\"ref.bin\",\"byteLength\":%zu}],\n",
			compressed_bin_size, uncompressed_bin_size);
	fprintf(f, " \"bufferViews\":[\n");
	for (size_t i = 0; i < streams.size(); ++i)
	{
		Stream* s = streams[i];
		fprintf(f, "  {\"buffer\":1,\"byteOffset\":%zu,\"byteLength\":%zu", ref_offsets[i], s->raw.size());
		if (s->attr.size()) fprintf(f, ",\"byteStride\":%zu,\"target\":34962", s->stride);
		else fprintf(f, ",\"target\":34963");
		fprintf(f, ",\"extensions\":{\"EXT_meshopt_compression\":{");
		fprintf(f, "\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"byteStride\":%zu,",
				cmp_offsets[i], s->encoded.size(), s->stride);
		fprintf(f, "\"mode\":\"%s\"", s->mode.c_str());
		if (!s->filter.empty()) fprintf(f, ",\"filter\":\"%s\"", s->filter.c_str());
		fprintf(f, ",\"count\":%zu}}", s->raw.size() / s->stride);
		fprintf(f, "}%s\n", i + 1 == streams.size() ? "" : ",");
	}
	fprintf(f, " ],\n \"accessors\":[\n");
	for (size_t i = 0; i < streams.size(); ++i)
	{
		Stream* s = streams[i];
		size_t count = s->raw.size() / s->stride;
		fprintf(f, "  {\"name\":\"%s\",\"bufferView\":%zu,\"componentType\":%s,\"count\":%zu,\"type\":\"%s\"",
				s->name.c_str(), i, ctype_name(s->component_type).c_str(), count, s->type.c_str());
		if (s->normalized) fprintf(f, ",\"normalized\":true");
		fprintf(f, "}%s\n", i + 1 == streams.size() ? "" : ",");
	}
	fprintf(f, " ],\n \"meshes\":[{\"primitives\":[{\"attributes\":{");
	bool first = true;
	for (size_t i = 0; i < streams.size(); ++i)
	{
		if (!streams[i]->attr.size()) continue;
		if (!first) fprintf(f, ",");
		fprintf(f, "\"%s\":%zu", streams[i]->attr.c_str(), i);
		first = false;
	}
	fprintf(f, "},\"indices\":%zu,\"material\":0}],\"extras\":{\"seq\":%zu}}],\n",
			streams.size() - 2, streams.size() - 1);
	fprintf(f, " \"materials\":[{}]\n}\n");
	fclose(f);
}

} /* namespace */

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <out-dir>\n", argv[0]);
		return 2;
	}
	std::string dir = argv[1];

	std::vector<Stream> storage;
	storage.push_back(make_position());
	storage.push_back(make_normal());
	storage.push_back(make_rotation());
	storage.push_back(make_texcoord());
	storage.push_back(make_color());
	storage.push_back(make_triangles());
	storage.push_back(make_index_sequence());

	std::vector<Stream*> streams;
	for (Stream& s : storage)
	{
		/* Streams without a decode filter are stored verbatim, so packed == raw. */
		if (s.packed.empty()) s.packed = s.raw;
		if (s.mode == "ATTRIBUTES") encode_vertex_stream(s);
		streams.push_back(&s);
	}

	/* uncompressed reference bin: views padded to 4 bytes */
	std::vector<unsigned char> ref_bin;
	std::vector<size_t> ref_offsets;
	for (Stream* s : streams)
	{
		pad4(ref_bin);
		ref_offsets.push_back(ref_bin.size());
		ref_bin.insert(ref_bin.end(), s->raw.begin(), s->raw.end());
	}
	pad4(ref_bin);

	/* compressed bin */
	std::vector<unsigned char> cmp_bin;
	std::vector<size_t> cmp_offsets;
	for (Stream* s : streams)
	{
		pad4(cmp_bin);
		cmp_offsets.push_back(cmp_bin.size());
		cmp_bin.insert(cmp_bin.end(), s->encoded.begin(), s->encoded.end());
	}
	pad4(cmp_bin);

	std::string ref_path = dir + "/ref.gltf";
	std::string ext_path = dir + "/ext.gltf";
	write_ref_gltf(ref_path, streams, ref_offsets, ref_bin.size());
	write_ext_gltf(ext_path, streams, ref_offsets, cmp_offsets, ref_bin.size(), cmp_bin.size());

	FILE* fb = fopen((dir + "/ref.bin").c_str(), "wb");
	fwrite(ref_bin.data(), 1, ref_bin.size(), fb);
	fclose(fb);
	fb = fopen((dir + "/ext.bin").c_str(), "wb");
	fwrite(cmp_bin.data(), 1, cmp_bin.size(), fb);
	fclose(fb);

	printf("generated %zu streams, ref=%zu bytes, compressed=%zu bytes in %s\n",
			streams.size(), ref_bin.size(), cmp_bin.size(), dir.c_str());
	return 0;
}
