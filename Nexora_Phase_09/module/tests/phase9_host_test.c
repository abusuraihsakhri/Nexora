#include <ai/distributed.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GiB (1024ull * 1024ull * 1024ull)
#define MiB (1024ull * 1024ull)

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static ai_dist_registry make_topology(void) {
    ai_dist_registry r;
    ai_dist_init(&r);

    ai_dist_endpoint *cpu = ai_dist_add_endpoint(
        &r, "host-cpu", AI_DEVICE_CPU, true, 0,
        64 * GiB, 48 * GiB, 2000000, 10000
    );
    ai_dist_endpoint *gpu0 = ai_dist_add_endpoint(
        &r, "local-gpu", AI_DEVICE_GPU, true, 0,
        24 * GiB, 18 * GiB, 100000000, 100000
    );
    ai_dist_endpoint *gpu1 = ai_dist_add_endpoint(
        &r, "remote-gpu-a", AI_DEVICE_GPU, false, 1,
        80 * GiB, 70 * GiB, 220000000, 200000
    );
    ai_dist_endpoint *gpu2 = ai_dist_add_endpoint(
        &r, "remote-gpu-b", AI_DEVICE_GPU, false, 2,
        80 * GiB, 72 * GiB, 210000000, 120000
    );

    CHECK(cpu && gpu0 && gpu1 && gpu2);

    CHECK(ai_dist_add_link(&r, cpu->id, gpu0->id, AI_DIST_TRANSPORT_SHM,
                           32ull * GiB, 3000, 0, true));
    CHECK(ai_dist_add_link(&r, gpu0->id, gpu1->id, AI_DIST_TRANSPORT_RDMA,
                           12ull * GiB, 10000, 0, true));
    CHECK(ai_dist_add_link(&r, gpu1->id, gpu2->id, AI_DIST_TRANSPORT_RDMA,
                           24ull * GiB, 7000, 0, true));
    CHECK(ai_dist_add_link(&r, gpu0->id, gpu2->id, AI_DIST_TRANSPORT_TCP,
                           2ull * GiB, 80000, 0, true));

    return r;
}

static void test_registry_and_validation(void) {
    ai_dist_registry r = make_topology();
    CHECK(r.endpoint_count == 4);
    CHECK(r.link_count == 4);
    CHECK(ai_dist_validate(&r));
    CHECK(strcmp(ai_dist_transport_name(AI_DIST_TRANSPORT_RDMA), "RDMA") == 0);
}

static void test_path_prefers_rdma_route(void) {
    ai_dist_registry r = make_topology();
    const ai_dist_endpoint *gpu0 = &r.endpoints[1];
    const ai_dist_endpoint *gpu2 = &r.endpoints[3];

    ai_dist_path p;
    CHECK(ai_dist_find_path(&r, gpu0->id, gpu2->id, 512 * MiB,
                            AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA, &p));
    CHECK(p.valid);
    CHECK(p.hop_count == 2);
    CHECK(p.endpoint_ids[0] == gpu0->id);
    CHECK(p.endpoint_ids[2] == gpu2->id);
}

static void test_direct_constraint(void) {
    ai_dist_registry r = make_topology();
    const ai_dist_endpoint *gpu0 = &r.endpoints[1];
    const ai_dist_endpoint *gpu2 = &r.endpoints[3];

    ai_dist_transfer_request req = {
        .src_endpoint_id = gpu0->id,
        .dst_endpoint_id = gpu2->id,
        .remote_tensor_id = 0,
        .bytes = 256 * MiB,
        .flags = AI_DIST_TRANSFER_REQUIRE_DIRECT,
        .deadline_budget_ns = 0
    };
    ai_dist_transfer_plan plan;
    CHECK(ai_dist_plan_transfer(&r, &req, &plan));
    CHECK(plan.path.hop_count == 1);
}

static void test_remote_tensor_and_replica(void) {
    ai_dist_registry r = make_topology();
    ai_tensor fake;
    memset(&fake, 0, sizeof(fake));
    fake.id = 77;
    fake.name = "kv-cache";
    fake.bytes = 2 * GiB;
    fake.location = AI_LOC_REMOTE;

    ai_remote_tensor *rt = ai_dist_publish_tensor(
        &r, NULL, &fake, r.endpoints[2].id, 5,
        AI_REMOTE_TENSOR_PINNED | AI_REMOTE_TENSOR_COHERENT
    );
    CHECK(rt != NULL);
    CHECK(rt->origin_tensor_id == 77);
    CHECK(rt->bytes == 2 * GiB);
    CHECK(ai_dist_tensor_add_replica(&r, rt->id, r.endpoints[3].id));
    CHECK(rt->replica_count == 1);
    CHECK((rt->flags & AI_REMOTE_TENSOR_REPLICATED) != 0);
    CHECK(ai_dist_validate(&r));
}

static void test_placement_uses_topology_and_load(void) {
    ai_dist_registry r = make_topology();

    ai_dist_placement_request request = {
        .required_device_kind = AI_DEVICE_GPU,
        .required_memory_bytes = 12 * GiB,
        .compute_units = 200000000,
        .data_endpoint_id = r.endpoints[0].id,
        .input_bytes = 128 * MiB,
        .deadline_budget_ns = 0,
        .prefer_local = false
    };

    ai_dist_placement_result result;
    CHECK(ai_dist_place(&r, &request, &result));
    CHECK(result.feasible);
    CHECK(result.endpoint_id != 0);

    /* Make the selected endpoint unavailable; planner must fail over. */
    u64 first = result.endpoint_id;
    CHECK(ai_dist_set_endpoint_health(&r, first, AI_DIST_HEALTH_DOWN));
    ai_dist_placement_result failover;
    CHECK(ai_dist_place(&r, &request, &failover));
    CHECK(failover.endpoint_id != first);
}

static void test_memory_admission(void) {
    ai_dist_registry r = make_topology();
    ai_dist_placement_request request = {
        .required_device_kind = AI_DEVICE_GPU,
        .required_memory_bytes = 79 * GiB,
        .compute_units = 1,
        .data_endpoint_id = 0,
        .input_bytes = 0,
        .deadline_budget_ns = 0,
        .prefer_local = false
    };
    ai_dist_placement_result result;
    CHECK(!ai_dist_place(&r, &request, &result));
}

static void test_collective_planning(void) {
    ai_dist_registry r = make_topology();
    u64 participants[3] = { r.endpoints[1].id, r.endpoints[2].id, r.endpoints[3].id };

    ai_dist_collective *c = ai_dist_add_collective(
        &r, "gradient-reduce", AI_COLLECTIVE_ALLREDUCE,
        participants, 3, 0, 256 * MiB
    );
    CHECK(c != NULL);
    CHECK(c->algorithm == AI_COLLECTIVE_ALGO_TREE);

    ai_dist_collective_plan plan;
    CHECK(ai_dist_plan_collective(&r, c->id, &plan));
    CHECK(plan.feasible);
    CHECK(plan.communication_rounds > 0);
    CHECK(plan.estimated_ns > 0);
}

static void test_ring_collective(void) {
    ai_dist_registry r = make_topology();
    u64 participants[4] = {
        r.endpoints[0].id, r.endpoints[1].id, r.endpoints[2].id, r.endpoints[3].id
    };
    ai_dist_collective *c = ai_dist_add_collective(
        &r, "allgather", AI_COLLECTIVE_ALLGATHER,
        participants, 4, 0, 64 * MiB
    );
    CHECK(c != NULL);
    CHECK(c->algorithm == AI_COLLECTIVE_ALGO_RING);

    ai_dist_collective_plan plan;
    CHECK(ai_dist_plan_collective(&r, c->id, &plan));
    CHECK(plan.algorithm == AI_COLLECTIVE_ALGO_RING);
    CHECK(plan.communication_rounds == 3);
}

static void test_link_failure_changes_path(void) {
    ai_dist_registry r = make_topology();
    u64 gpu0 = r.endpoints[1].id;
    u64 gpu2 = r.endpoints[3].id;

    ai_dist_path before;
    CHECK(ai_dist_find_path(&r, gpu0, gpu2, 256 * MiB,
                            AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA, &before));
    CHECK(before.hop_count == 2);

    /* Bring down gpu0<->gpu1; direct TCP path should remain usable. */
    CHECK(ai_dist_set_link_health(&r, r.links[1].id, AI_DIST_HEALTH_DOWN));
    ai_dist_path after;
    CHECK(ai_dist_find_path(&r, gpu0, gpu2, 256 * MiB,
                            AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA, &after));
    CHECK(after.hop_count == 1);
}

static void test_deadline_prediction_and_metrics(void) {
    ai_dist_registry r = make_topology();
    ai_dist_transfer_request req = {
        .src_endpoint_id = r.endpoints[1].id,
        .dst_endpoint_id = r.endpoints[3].id,
        .remote_tensor_id = 0,
        .bytes = 4 * GiB,
        .flags = AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA,
        .deadline_budget_ns = 1
    };
    ai_dist_transfer_plan plan;
    CHECK(ai_dist_plan_transfer(&r, &req, &plan));
    CHECK(plan.deadline_miss);

    const ai_dist_metrics *m = ai_dist_get_metrics(&r);
    CHECK(m != NULL);
    CHECK(m->transfers_planned == 1);
    CHECK(m->deadline_misses_predicted == 1);
}

static void test_staging_is_opt_in(void) {
    ai_dist_registry r;
    ai_dist_init(&r);
    ai_dist_endpoint *a = ai_dist_add_endpoint(&r, "a", AI_DEVICE_GPU, true, 0,
                                                8 * GiB, 8 * GiB, 1000000, 0);
    ai_dist_endpoint *b = ai_dist_add_endpoint(&r, "b", AI_DEVICE_GPU, false, 1,
                                                8 * GiB, 8 * GiB, 1000000, 0);
    ai_dist_endpoint *c = ai_dist_add_endpoint(&r, "c", AI_DEVICE_GPU, false, 2,
                                                8 * GiB, 8 * GiB, 1000000, 0);
    CHECK(a && b && c);
    CHECK(ai_dist_add_link(&r, a->id, b->id, AI_DIST_TRANSPORT_TCP,
                           GiB, 1000, 0, false));
    CHECK(ai_dist_add_link(&r, b->id, c->id, AI_DIST_TRANSPORT_TCP,
                           GiB, 1000, 0, false));

    ai_dist_path p;
    CHECK(!ai_dist_find_path(&r, a->id, c->id, MiB, 0, &p));
    CHECK(ai_dist_find_path(&r, a->id, c->id, MiB,
                            AI_DIST_TRANSFER_ALLOW_STAGING, &p));
    CHECK(p.hop_count == 2);
}

static void test_collective_root_membership(void) {
    ai_dist_registry r = make_topology();
    u64 participants[2] = { r.endpoints[1].id, r.endpoints[2].id };
    CHECK(ai_dist_add_collective(&r, "bad-root", AI_COLLECTIVE_BROADCAST,
                                 participants, 2, r.endpoints[3].id, MiB) == NULL);
}

static void test_directed_reduce_direction(void) {
    ai_dist_registry r;
    ai_dist_init(&r);
    ai_dist_endpoint *root = ai_dist_add_endpoint(&r, "root", AI_DEVICE_GPU, true, 0,
                                                   8 * GiB, 8 * GiB, 1000000, 0);
    ai_dist_endpoint *peer = ai_dist_add_endpoint(&r, "peer", AI_DEVICE_GPU, false, 1,
                                                   8 * GiB, 8 * GiB, 1000000, 0);
    CHECK(root && peer);
    CHECK(ai_dist_add_link(&r, peer->id, root->id, AI_DIST_TRANSPORT_TCP,
                           GiB, 1000, 0, false));
    u64 participants[2] = { root->id, peer->id };
    ai_dist_collective *reduce = ai_dist_add_collective(
        &r, "directed-reduce", AI_COLLECTIVE_REDUCE,
        participants, 2, root->id, MiB
    );
    CHECK(reduce != NULL);
    ai_dist_collective_plan plan;
    CHECK(ai_dist_plan_collective(&r, reduce->id, &plan));
    CHECK(plan.feasible);
}

static void test_transfer_tensor_reference_validation(void) {
    ai_dist_registry r = make_topology();
    ai_remote_tensor *tensor = ai_dist_add_remote_tensor(
        &r, "weights", 100, r.endpoints[1].id, 32 * MiB, 1, AI_REMOTE_TENSOR_READONLY
    );
    CHECK(tensor != NULL);

    ai_dist_transfer_request good = {
        .src_endpoint_id = r.endpoints[1].id,
        .dst_endpoint_id = r.endpoints[2].id,
        .remote_tensor_id = tensor->id,
        .bytes = 16 * MiB,
        .flags = AI_DIST_TRANSFER_REQUIRE_DIRECT,
        .deadline_budget_ns = 0
    };
    ai_dist_transfer_plan plan;
    CHECK(ai_dist_plan_transfer(&r, &good, &plan));

    ai_dist_transfer_request unknown = good;
    unknown.remote_tensor_id = 999999;
    CHECK(!ai_dist_plan_transfer(&r, &unknown, &plan));

    ai_dist_transfer_request wrong_source = good;
    wrong_source.src_endpoint_id = r.endpoints[0].id;
    wrong_source.dst_endpoint_id = r.endpoints[1].id;
    CHECK(!ai_dist_plan_transfer(&r, &wrong_source, &plan));

    ai_dist_transfer_request oversize = good;
    oversize.bytes = 64 * MiB;
    CHECK(!ai_dist_plan_transfer(&r, &oversize, &plan));
}

static void test_invalid_enum_rejection_and_validation(void) {
    ai_dist_registry r = make_topology();
    CHECK(!ai_dist_set_endpoint_health(&r, r.endpoints[0].id, (ai_dist_health)99));
    CHECK(!ai_dist_set_link_health(&r, r.links[0].id, (ai_dist_health)99));
    CHECK(ai_dist_add_link(&r, r.endpoints[0].id, r.endpoints[1].id,
                           (ai_dist_transport)99, GiB, 1, 0, true) == NULL);
    CHECK(ai_dist_validate(&r));

    r.endpoints[0].health = (ai_dist_health)99;
    CHECK(!ai_dist_validate(&r));
}


int main(void) {
    test_registry_and_validation();
    test_path_prefers_rdma_route();
    test_direct_constraint();
    test_remote_tensor_and_replica();
    test_placement_uses_topology_and_load();
    test_memory_admission();
    test_collective_planning();
    test_ring_collective();
    test_link_failure_changes_path();
    test_deadline_prediction_and_metrics();
    test_staging_is_opt_in();
    test_collective_root_membership();
    test_directed_reduce_direction();
    test_transfer_tensor_reference_validation();
    test_invalid_enum_rejection_and_validation();

    if (failures != 0) {
        fprintf(stderr, "Phase 9 tests: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    printf("Phase 9 tests: PASS\n");
    return EXIT_SUCCESS;
}
