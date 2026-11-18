/* Tests for MSFT_lod support: ids chain parsing, screen coverage thresholds,
   LOD level picking, and parse-time rejection of malformed LOD data. */
#define CGLTF_IMPLEMENTATION
#include "../cgltf.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

static cgltf_result parse(const char* json, cgltf_data** out_data)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	*out_data = NULL;
	return cgltf_parse(&options, json, strlen(json), out_data);
}

/* The scene artist's case: one distant tree with 4 LOD levels, ids listed on
   the highest LOD, per-level screen coverage thresholds in extras. */
static const char* json_flat_ids =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":["
	"{\"name\":\"Tree_LOD0\","
	"\"extensions\":{\"MSFT_lod\":{\"ids\":[1,2,3]}},"
	"\"extras\":{\"MSFT_screencoverage\":[0.5,0.2,0.05,0.01]}},"
	"{\"name\":\"Tree_LOD1\"},"
	"{\"name\":\"Tree_LOD2\"},"
	"{\"name\":\"Tree_LOD3\"}]}";

/* Same 4 levels, but authored as a chain: each level points at the next. */
static const char* json_chained_ids =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":["
	"{\"name\":\"Tree_LOD0\","
	"\"extensions\":{\"MSFT_lod\":{\"ids\":[1]}},"
	"\"extras\":{\"MSFT_screencoverage\":[0.5,0.2,0.05,0.01]}},"
	"{\"name\":\"Tree_LOD1\",\"extensions\":{\"MSFT_lod\":{\"ids\":[2]}}},"
	"{\"name\":\"Tree_LOD2\",\"extensions\":{\"MSFT_lod\":{\"ids\":[3]}}},"
	"{\"name\":\"Tree_LOD3\"}]}";

static const char* json_missing_ids =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":[{\"extensions\":{\"MSFT_lod\":{}}}]}";

static const char* json_empty_ids =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":[{\"extensions\":{\"MSFT_lod\":{\"ids\":[]}}}]}";

static const char* json_cycle_two_nodes =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":["
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[1]}}},"
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[0]}}}]}";

static const char* json_cycle_self =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":[{\"extensions\":{\"MSFT_lod\":{\"ids\":[0]}}}]}";

static const char* json_cycle_indirect =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":["
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[1]}}},"
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[2]}}},"
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[1]}}}]}";

static const char* json_index_out_of_bounds =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":["
	"{\"extensions\":{\"MSFT_lod\":{\"ids\":[7]}}},"
	"{}]}";

static const char* json_no_extension =
	"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
	"\"scenes\":[{\"nodes\":[0]}],"
	"\"nodes\":[{\"name\":\"Tree\"},{\"name\":\"Rock\"}]}";

static void test_flat_ids(void)
{
	cgltf_data* data = NULL;
	cgltf_result result = parse(json_flat_ids, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success) return;

	CHECK(data->nodes_count == 4);

	cgltf_node* lod0 = &data->nodes[0];
	CHECK(lod0->lods_count == 3);
	CHECK(lod0->lods_count == 3 && lod0->lods[0] == &data->nodes[1]);
	CHECK(lod0->lods_count == 3 && lod0->lods[1] == &data->nodes[2]);
	CHECK(lod0->lods_count == 3 && lod0->lods[2] == &data->nodes[3]);

	CHECK(data->nodes[1].lods_count == 0);
	CHECK(data->nodes[1].lods == NULL);

	CHECK(lod0->screen_coverages_count == 4);
	if (lod0->screen_coverages_count == 4)
	{
		CHECK(lod0->screen_coverages[0] == 0.5f);
		CHECK(lod0->screen_coverages[1] == 0.2f);
		CHECK(lod0->screen_coverages[2] == 0.05f);
		CHECK(lod0->screen_coverages[3] == 0.01f);
	}

	/* raw extras must still be preserved */
	CHECK(lod0->extras.data != NULL);
	CHECK(lod0->extras.data && strstr(lod0->extras.data, "MSFT_screencoverage") != NULL);

	/* pick LOD level by screen coverage */
	CHECK(cgltf_node_pick_lod(lod0, 1.0f) == &data->nodes[0]);
	CHECK(cgltf_node_pick_lod(lod0, 0.5f) == &data->nodes[0]);
	CHECK(cgltf_node_pick_lod(lod0, 0.3f) == &data->nodes[1]);
	CHECK(cgltf_node_pick_lod(lod0, 0.2f) == &data->nodes[1]);
	CHECK(cgltf_node_pick_lod(lod0, 0.1f) == &data->nodes[2]);
	CHECK(cgltf_node_pick_lod(lod0, 0.02f) == &data->nodes[3]);
	CHECK(cgltf_node_pick_lod(lod0, 0.01f) == &data->nodes[3]);
	/* below the lowest threshold: render nothing */
	CHECK(cgltf_node_pick_lod(lod0, 0.005f) == NULL);
	CHECK(cgltf_node_pick_lod(lod0, 0.0f) == NULL);

	cgltf_free(data);
}

static void test_chained_ids(void)
{
	cgltf_data* data = NULL;
	cgltf_result result = parse(json_chained_ids, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success) return;

	/* the chain is flattened: the top node sees all lower levels */
	cgltf_node* lod0 = &data->nodes[0];
	CHECK(lod0->lods_count == 3);
	CHECK(lod0->lods_count == 3 && lod0->lods[0] == &data->nodes[1]);
	CHECK(lod0->lods_count == 3 && lod0->lods[1] == &data->nodes[2]);
	CHECK(lod0->lods_count == 3 && lod0->lods[2] == &data->nodes[3]);

	/* intermediate nodes see their own sub-chains */
	CHECK(data->nodes[1].lods_count == 2);
	CHECK(data->nodes[1].lods_count == 2 && data->nodes[1].lods[0] == &data->nodes[2]);
	CHECK(data->nodes[1].lods_count == 2 && data->nodes[1].lods[1] == &data->nodes[3]);
	CHECK(data->nodes[2].lods_count == 1);
	CHECK(data->nodes[2].lods_count == 1 && data->nodes[2].lods[0] == &data->nodes[3]);
	CHECK(data->nodes[3].lods_count == 0);

	/* coverage picking works through the flattened chain */
	CHECK(cgltf_node_pick_lod(lod0, 0.6f) == &data->nodes[0]);
	CHECK(cgltf_node_pick_lod(lod0, 0.3f) == &data->nodes[1]);
	CHECK(cgltf_node_pick_lod(lod0, 0.1f) == &data->nodes[2]);
	CHECK(cgltf_node_pick_lod(lod0, 0.02f) == &data->nodes[3]);
	CHECK(cgltf_node_pick_lod(lod0, 0.005f) == NULL);

	cgltf_free(data);
}

static void test_missing_ids(void)
{
	cgltf_data* data = NULL;
	CHECK(parse(json_missing_ids, &data) == cgltf_result_lod_missing_ids);
	CHECK(data == NULL);
	CHECK(parse(json_empty_ids, &data) == cgltf_result_lod_missing_ids);
	CHECK(data == NULL);
}

static void test_cycles_rejected_at_parse_time(void)
{
	cgltf_data* data = NULL;
	CHECK(parse(json_cycle_two_nodes, &data) == cgltf_result_lod_cycle);
	CHECK(data == NULL);
	CHECK(parse(json_cycle_self, &data) == cgltf_result_lod_cycle);
	CHECK(data == NULL);
	CHECK(parse(json_cycle_indirect, &data) == cgltf_result_lod_cycle);
	CHECK(data == NULL);
}

static void test_index_out_of_bounds(void)
{
	cgltf_data* data = NULL;
	CHECK(parse(json_index_out_of_bounds, &data) == cgltf_result_lod_index_out_of_bounds);
	CHECK(data == NULL);
}

static void test_error_codes_are_distinct(void)
{
	CHECK(cgltf_result_lod_missing_ids != cgltf_result_lod_cycle);
	CHECK(cgltf_result_lod_missing_ids != cgltf_result_lod_index_out_of_bounds);
	CHECK(cgltf_result_lod_cycle != cgltf_result_lod_index_out_of_bounds);
	CHECK(cgltf_result_lod_missing_ids != cgltf_result_invalid_gltf);
	CHECK(cgltf_result_lod_cycle != cgltf_result_invalid_gltf);
	CHECK(cgltf_result_lod_index_out_of_bounds != cgltf_result_invalid_gltf);
}

static void test_no_extension_unchanged(void)
{
	cgltf_data* data = NULL;
	cgltf_result result = parse(json_no_extension, &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success) return;

	CHECK(data->nodes_count == 2);
	for (cgltf_size i = 0; i < data->nodes_count; ++i)
	{
		CHECK(data->nodes[i].lods == NULL);
		CHECK(data->nodes[i].lods_count == 0);
		CHECK(data->nodes[i].screen_coverages == NULL);
		CHECK(data->nodes[i].screen_coverages_count == 0);
		/* without LOD data a node always renders itself */
		CHECK(cgltf_node_pick_lod(&data->nodes[i], 0.3f) == &data->nodes[i]);
		CHECK(data->nodes[i].extras.data == NULL);
	}

	CHECK(data->nodes[0].name && strcmp(data->nodes[0].name, "Tree") == 0);
	CHECK(data->nodes[1].name && strcmp(data->nodes[1].name, "Rock") == 0);

	cgltf_free(data);
}

int main(void)
{
	test_flat_ids();
	test_chained_ids();
	test_missing_ids();
	test_cycles_rejected_at_parse_time();
	test_index_out_of_bounds();
	test_error_codes_are_distinct();
	test_no_extension_unchanged();

	if (failures == 0)
	{
		printf("test_msft_lod: all tests passed\n");
		return 0;
	}

	printf("test_msft_lod: %d check(s) failed\n", failures);
	return 1;
}
