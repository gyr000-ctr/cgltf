int meshopt_decodeIndexBuffer(void* destination, size_t index_count, size_t index_size, const unsigned char* buffer, size_t buffer_size)
{
	using namespace meshopt;

	assert(index_count % 3 == 0);
	assert(index_size == 2 || index_size == 4);

	// the minimum valid encoding is header, 1 byte per triangle and a 16-byte codeaux table
	if (buffer_size < 1 + index_count / 3 + 16)
		return -2;

	if ((buffer[0] & 0xf0) != kIndexHeader)
		return -1;

	int version = buffer[0] & 0x0f;
	if (version > kDecodeIndexVersion)
		return -1;

	EdgeFifo edgefifo;
	memset(edgefifo, -1, sizeof(edgefifo));

	VertexFifo vertexfifo;
	memset(vertexfifo, -1, sizeof(vertexfifo));

	size_t edgefifooffset = 0;
	size_t vertexfifooffset = 0;

	unsigned int next = 0;
	unsigned int last = 0;

	int fecmax = version >= 1 ? 13 : 15;

	// since we store 16-byte codeaux table at the end, triangle data has to begin before data_safe_end
	const unsigned char* code = buffer + 1;
	const unsigned char* code_end = code + index_count / 3;
	const unsigned char* data = code_end;

	// each triangle reads at most 16 bytes of data: 1b for codeaux and 5b for each free index
	const unsigned char* data_safe_end = buffer + buffer_size - 16;

	const unsigned char* codeaux_table = data_safe_end;

	while (code < code_end)
	{
		unsigned char codetri = *code++;

		if (codetri < 0xf0)
		{
			int fe = codetri >> 4;

			// fifo reads are wrapped around 16 entry buffer
			unsigned int a = edgefifo[(edgefifooffset - 1 - fe) & 15][0];
			unsigned int b = edgefifo[(edgefifooffset - 1 - fe) & 15][1];
			unsigned int c = 0;

			int fec = codetri & 15;

			// note: this is the most common path in the entire decoder
			// inside this if we try to stay branchless (by using cmov/etc.) since these aren't predictable
			if (fec < fecmax)
			{
				// fifo reads are wrapped around 16 entry buffer
				unsigned int cf = vertexfifo[(vertexfifooffset - 1 - fec) & 15];

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__aarch64__))
				// on clang, x86-cmov-conversion pass emits a branch since cf is a load from memory; asm barrier defeats this
				// this can be fixed with __builtin_unpredictable, but gcc doesn't support it and has a similar problem due to if-conversion
				// additionally, gcc can fuse a/b into one 64-bit load but use 32-bit edgefifo[] stores, which breaks load store forwarding
				__asm__("" : "+r"(a) : "r"(cf));
#endif

				c = (fec == 0) ? next : cf;

				int fec0 = fec == 0;
				next += fec0;

				// push vertex fifo must match the encoding step *exactly* otherwise the data will not be decoded correctly
				pushVertexFifo(vertexfifo, c, vertexfifooffset, fec0);
			}
			else
			{
				// make sure we have enough data to read for a triangle; this check covers worst case advance
				if (data > data_safe_end)
					return -2;

				// fec * 2 - 27 decodes 13, 14 into -1, 1
				// note that we need to update the last index since free indices are delta-encoded
				last = c = (fec != 15) ? last + (fec * 2 - 27) : decodeIndex(data, last);

				// push vertex/edge fifo must match the encoding step *exactly* otherwise the data will not be decoded correctly
				pushVertexFifo(vertexfifo, c, vertexfifooffset);
			}

			// push edge fifo must match the encoding step *exactly* otherwise the data will not be decoded correctly
			pushEdgeFifo(edgefifo, c, b, edgefifooffset);
			pushEdgeFifo(edgefifo, a, c, edgefifooffset);

			// output triangle
			destination = writeTriangle(destination, index_size, a, b, c);
		}
		else
		{
			// fast path: read codeaux from the table
			if (codetri < 0xfe)
			{
				unsigned char codeaux = codeaux_table[codetri & 15];

				// note: table can't contain feb/fec=15
				int feb = codeaux >> 4;
				int fec = codeaux & 15;

				// fifo reads are wrapped around 16 entry buffer
				// also note that we increment next for all three vertices before decoding indices - this matches encoder behavior
				unsigned int a = next++;

				unsigned int bf = vertexfifo[(vertexfifooffset - feb) & 15];
				unsigned int b = (feb == 0) ? next : bf;

				int feb0 = feb == 0;
				next += feb0;

				unsigned int cf = vertexfifo[(vertexfifooffset - fec) & 15];
				unsigned int c = (fec == 0) ? next : cf;

				int fec0 = fec == 0;
				next += fec0;

				// output triangle
				destination = writeTriangle(destination, index_size, a, b, c);

				// push vertex/edge fifo must match the encoding step *exactly* otherwise the data will not be decoded correctly
				pushVertexFifo(vertexfifo, a, vertexfifooffset);
				pushVertexFifo(vertexfifo, b, vertexfifooffset, feb0);
				pushVertexFifo(vertexfifo, c, vertexfifooffset, fec0);

				pushEdgeFifo(edgefifo, b, a, edgefifooffset);
				pushEdgeFifo(edgefifo, c, b, edgefifooffset);
				pushEdgeFifo(edgefifo, a, c, edgefifooffset);
			}
			else
			{
				// make sure we have enough data to read for a triangle; this check covers worst case advance
				if (data > data_safe_end)
					return -2;

				// slow path: read a full byte for codeaux instead of using a table lookup
				unsigned char codeaux = *data++;

				int fea = codetri == 0xfe ? 0 : 15;
				int feb = codeaux >> 4;
				int fec = codeaux & 15;

				// reset: codeaux is 0 but encoded as not-a-table
				if (codeaux == 0)
					next = 0;

				// fifo reads are wrapped around 16 entry buffer
				// also note that we increment next for all three vertices before decoding indices - this matches encoder behavior
				unsigned int a = (fea == 0) ? next++ : 0;
				unsigned int b = (feb == 0) ? next++ : vertexfifo[(vertexfifooffset - feb) & 15];
				unsigned int c = (fec == 0) ? next++ : vertexfifo[(vertexfifooffset - fec) & 15];

				// note that we need to update the last index since free indices are delta-encoded
				if (fea == 15)
					last = a = decodeIndex(data, last);

				if (feb == 15)
					last = b = decodeIndex(data, last);

				if (fec == 15)
					last = c = decodeIndex(data, last);

				// output triangle
				destination = writeTriangle(destination, index_size, a, b, c);

				// push vertex/edge fifo must match the encoding step *exactly* otherwise the data will not be decoded correctly
				pushVertexFifo(vertexfifo, a, vertexfifooffset);
				pushVertexFifo(vertexfifo, b, vertexfifooffset, (feb == 0) | (feb == 15));
				pushVertexFifo(vertexfifo, c, vertexfifooffset, (fec == 0) | (fec == 15));

				pushEdgeFifo(edgefifo, b, a, edgefifooffset);
				pushEdgeFifo(edgefifo, c, b, edgefifooffset);
				pushEdgeFifo(edgefifo, a, c, edgefifooffset);
			}
		}
	}

	// we should've read all data bytes and stopped at the boundary between data and codeaux table
	if (data != data_safe_end)
		return -3;

	return 0;
}
