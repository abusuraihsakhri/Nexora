#include <kernel/printk.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <ai/tensor.h>
#include <ai/backing.h>
#include <ai/mapping.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <ai/capability.h>

static void print_tensor(const ai_tensor *t) {
    kputs("tensor ");
    kputs(t->name);
    kputs(" id=");
    kprint_u64(t->id);
    kputs(" dtype=");
    kputs(ai_dtype_name(t->dtype));
    kputs(" bytes=");
    kprint_u64(t->bytes);
    kputs(" location=");
    kputs(ai_location_name(t->location));
    kputs("\n");
}

static void print_work(const ai_work_node *node) {
    kputs("work node ");
    kprint_u64(node->id);
    kputs(" ");
    kputs(node->name);
    kputs(" op=");
    kputs(ai_op_name(node->op));
    kputs(" state=");
    kputs(ai_work_state_name(node->state));
    kputs(" priority=");
    kprint_u64(node->priority);
    kputs("\n");
}

static void print_domain(const ai_domain *domain) {
    kputs("domain ");
    kprint_u64(domain->id);
    kputs(" ");
    kputs(domain->name);
    kputs(" state=");
    kputs(ai_domain_state_name(domain->state));
    kputs(" memory=");
    kprint_u64(domain->memory_used_bytes);
    kputs(" tensors=");
    kprint_u64(domain->tensor_count);
    kputs(" work=");
    kprint_u64(domain->work_node_count);
    kputs(" handles=");
    kprint_u64(ai_handle_live_count(domain));
    kputs(" mappings=");
    kprint_u64(ai_mapping_live_count(domain));
    kputs("\n");
}


static void require_demo(bool condition, const char *message) {
    kputs(message);
    kputs(condition ? ": PASS\n" : ": FAIL\n");
    if (!condition) {
        panic("Phase 4 self-test failed");
    }
}

static void print_rights(ai_handle_rights_t rights) {
    bool first = true;

    if ((rights & AI_HANDLE_RIGHT_READ) != 0) {
        kputs("READ");
        first = false;
    }
    if ((rights & AI_HANDLE_RIGHT_WRITE) != 0) {
        kputs(first ? "WRITE" : "|WRITE");
        first = false;
    }
    if ((rights & AI_HANDLE_RIGHT_MAP) != 0) {
        kputs(first ? "MAP" : "|MAP");
        first = false;
    }
    if ((rights & AI_HANDLE_RIGHT_SHARE) != 0) {
        kputs(first ? "SHARE" : "|SHARE");
        first = false;
    }
    if ((rights & AI_HANDLE_RIGHT_TRANSFER) != 0) {
        kputs(first ? "TRANSFER" : "|TRANSFER");
        first = false;
    }
    if ((rights & AI_HANDLE_RIGHT_ADMIN) != 0) {
        kputs(first ? "ADMIN" : "|ADMIN");
        first = false;
    }
    if (first) {
        kputs("NONE");
    }
}

static void run_handle_demo(void) {
    ai_domain_limits unlimited = {
        .memory_bytes = 0,
        .max_tensors = 0,
        .max_work_nodes = 0
    };

    ai_domain *owner = ai_domain_create("rights-owner", 0, unlimited);
    ai_domain *foreign = ai_domain_create("foreign-domain", 0, unlimited);
    ai_domain_activate(owner);
    ai_domain_activate(foreign);

    u64 shape[1] = {16};
    ai_tensor *tensor = ai_tensor_create(
        "rights-demo-tensor",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *readonly_tensor = ai_tensor_create(
        "readonly-demo-tensor",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY
    );

    kputs("\n[handle rights demo]\n");

    ai_handle_t invalid_write = ai_handle_install(
        owner,
        readonly_tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE
    );
    require_demo(
        invalid_write == AI_HANDLE_INVALID,
        "read-only tensor refuses WRITE authority"
    );

    ai_handle_t readonly_handle = ai_handle_install(
        owner,
        readonly_tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    require_demo(
        readonly_handle != AI_HANDLE_INVALID,
        "read-only tensor accepts READ|MAP authority"
    );
    require_demo(ai_handle_close(owner, readonly_handle),
                 "close read-only tensor handle");

    ai_handle_rights_t initial_rights =
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER;

    ai_handle_t first = ai_handle_install(
        owner,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        initial_rights
    );

    kputs("owner handle: ");
    kprint_hex(first);
    kputs(" rights=");
    print_rights(ai_handle_get_rights(owner, first));
    kputs("\n");

    require_demo(first != AI_HANDLE_INVALID,
                 "install rights-bearing tensor handle");
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
        ) == tensor,
        "READ|MAP resolution succeeds"
    );
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_ADMIN
        ) == NULL,
        "missing ADMIN right is rejected"
    );
    require_demo(
        ai_handle_resolve(
            foreign,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "foreign domain is rejected"
    );
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_WORK,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "object type mismatch is rejected"
    );

    ai_handle_rights_t attenuated =
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE;

    require_demo(
        ai_handle_restrict_rights(owner, first, attenuated),
        "rights attenuation succeeds"
    );
    require_demo(
        ai_handle_get_rights(owner, first) == attenuated,
        "attenuated rights are recorded"
    );
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_WRITE
        ) == NULL,
        "WRITE is rejected after attenuation"
    );
    require_demo(
        !ai_handle_restrict_rights(owner, first, initial_rights),
        "rights escalation is rejected"
    );
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == tensor,
        "retained READ authority still resolves"
    );

    require_demo(ai_handle_close(owner, first),
                 "close attenuated handle");
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "closed handle becomes stale"
    );

    ai_handle_t second = ai_handle_install(
        owner,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );

    require_demo(second != AI_HANDLE_INVALID && second != first,
                 "reused slot gets new generation");
    require_demo(
        ai_handle_generation(second) != ai_handle_generation(first),
        "generation advances on slot reuse"
    );
    require_demo(
        ai_handle_resolve(
            owner,
            first,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "old generation stays invalid"
    );
    require_demo(ai_handle_live_count(owner) == 1,
                 "live handle accounting");

    require_demo(ai_domain_begin_quiesce(owner),
                 "owner enters quiescing state");
    require_demo(
        !ai_domain_destroy(owner),
        "quiescing domain with live handle cannot be destroyed"
    );
    require_demo(ai_handle_close(owner, second),
                 "close final handle while quiescing");
    require_demo(ai_handle_live_count(owner) == 0,
                 "handle table drains to zero");

    ai_domain_begin_quiesce(foreign);
    require_demo(ai_domain_destroy(owner),
                 "owner destroys after handle drain");
    require_demo(ai_domain_destroy(foreign),
                 "foreign domain destroys cleanly");
}


static void run_delegation_demo(void) {
    ai_domain_limits unlimited = {
        .memory_bytes = 0,
        .max_tensors = 0,
        .max_work_nodes = 0
    };

    ai_domain *producer = ai_domain_create("share-producer", 0, unlimited);
    ai_domain *consumer = ai_domain_create("share-consumer", 0, unlimited);
    ai_domain *receiver = ai_domain_create("transfer-receiver", 0, unlimited);
    ai_domain_activate(producer);
    ai_domain_activate(consumer);
    ai_domain_activate(receiver);

    u64 shape[2] = {8, 8};
    ai_tensor *tensor = ai_tensor_create(
        "delegated-tensor",
        AI_DTYPE_F32,
        2,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    kputs("\n[cross-domain delegation demo]\n");

    ai_handle_t source = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER
    );
    require_demo(source != AI_HANDLE_INVALID,
                 "producer installs delegable handle");

    ai_handle_t shared = ai_handle_share(
        producer,
        source,
        consumer,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE
    );
    require_demo(shared != AI_HANDLE_INVALID,
                 "SHARE mints recipient-local handle");
    require_demo(ai_handle_domain_tag(shared) == (u32)consumer->id,
                 "recipient handle carries recipient domain tag");
    require_demo(
        ai_handle_resolve(
            consumer,
            shared,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
        ) == tensor,
        "recipient resolves same tensor object"
    );
    require_demo(
        ai_handle_resolve(
            producer,
            source,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_WRITE
        ) == tensor,
        "SHARE preserves source authority"
    );
    require_demo(
        ai_handle_resolve(
            consumer,
            shared,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_WRITE
        ) == NULL,
        "shared rights are attenuated"
    );
    require_demo(
        ai_handle_resolve(
            consumer,
            source,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "copied source token remains foreign"
    );

    ai_handle_t reshared = ai_handle_share(
        consumer,
        shared,
        receiver,
        AI_HANDLE_RIGHT_READ
    );
    require_demo(reshared != AI_HANDLE_INVALID,
                 "delegated SHARE supports attenuated re-share");

    ai_handle_t movable = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_TRANSFER
    );
    require_demo(movable != AI_HANDLE_INVALID,
                 "install transferable source handle");

    ai_handle_t moved = ai_handle_transfer(
        producer,
        movable,
        receiver,
        AI_HANDLE_RIGHT_READ
    );
    require_demo(moved != AI_HANDLE_INVALID,
                 "TRANSFER creates destination handle");
    require_demo(
        ai_handle_resolve(
            producer,
            movable,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == NULL,
        "successful TRANSFER invalidates source handle"
    );
    require_demo(
        ai_handle_resolve(
            receiver,
            moved,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == tensor,
        "transferred handle resolves same tensor object"
    );
    require_demo(
        ai_handle_resolve(
            receiver,
            moved,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_WRITE
        ) == NULL,
        "TRANSFER may attenuate rights"
    );

    ai_handle_t no_transfer = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );
    require_demo(no_transfer != AI_HANDLE_INVALID,
                 "install non-transferable handle");
    require_demo(
        ai_handle_transfer(
            producer,
            no_transfer,
            receiver,
            AI_HANDLE_RIGHT_READ
        ) == AI_HANDLE_INVALID,
        "TRANSFER without TRANSFER right is rejected"
    );
    require_demo(
        ai_handle_resolve(
            producer,
            no_transfer,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ
        ) == tensor,
        "failed TRANSFER leaves source intact"
    );

    require_demo(ai_handle_close(receiver, reshared),
                 "close re-shared handle");
    require_demo(ai_handle_close(receiver, moved),
                 "close transferred handle");
    require_demo(ai_handle_close(consumer, shared),
                 "close shared handle");
    require_demo(ai_handle_close(producer, no_transfer),
                 "close non-transferable handle");
    require_demo(ai_handle_close(producer, source),
                 "close source handle");

    require_demo(ai_domain_begin_quiesce(producer),
                 "producer enters quiescing state");
    require_demo(ai_domain_begin_quiesce(consumer),
                 "consumer enters quiescing state");
    require_demo(ai_domain_begin_quiesce(receiver),
                 "receiver enters quiescing state");
    require_demo(ai_domain_destroy(producer),
                 "producer destroys after delegation drain");
    require_demo(ai_domain_destroy(consumer),
                 "consumer destroys after delegation drain");
    require_demo(ai_domain_destroy(receiver),
                 "receiver destroys after delegation drain");
}



static void run_mapping_demo(void) {
    ai_domain_limits unlimited = {
        .memory_bytes = 0,
        .max_tensors = 0,
        .max_work_nodes = 0
    };

    ai_domain *producer = ai_domain_create("map-producer", 0, unlimited);
    ai_domain *consumer = ai_domain_create("map-consumer", 0, unlimited);
    ai_domain_activate(producer);
    ai_domain_activate(consumer);

    u64 shape[1] = {2048};
    ai_tensor *tensor = ai_tensor_create(
        "zero-copy-demo",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes,
        AI_BACKING_ZEROED
    );

    kputs("\n[zero-copy mapping demo]\n");

    require_demo(backing != NULL,
                 "allocate page-backed tensor storage");
    require_demo(ai_tensor_attach_backing(tensor, backing, 0),
                 "attach backing to tensor metadata");

    ai_handle_t owner = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE
    );
    require_demo(owner != AI_HANDLE_INVALID,
                 "producer installs mappable tensor handle");

    ai_handle_t shared = ai_handle_share(
        producer,
        owner,
        consumer,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    require_demo(shared != AI_HANDLE_INVALID,
                 "consumer receives READ|MAP handle");

    ai_mapping_id_t producer_map = ai_tensor_map(
        producer,
        owner,
        AI_MAP_PROT_READ | AI_MAP_PROT_WRITE
    );
    ai_mapping_id_t consumer_map = ai_tensor_map(
        consumer,
        shared,
        AI_MAP_PROT_READ
    );

    require_demo(producer_map != AI_MAPPING_INVALID,
                 "producer creates writable mapping");
    require_demo(consumer_map != AI_MAPPING_INVALID,
                 "consumer creates read-only mapping");
    require_demo(backing->mapping_count == 2,
                 "backing tracks two live mappings");

    u64 producer_va = ai_mapping_virtual_address(producer, producer_map);
    u64 consumer_va = ai_mapping_virtual_address(consumer, consumer_map);
    u64 producer_pa = ai_mapping_physical_address(producer, producer_map);
    u64 consumer_pa = ai_mapping_physical_address(consumer, consumer_map);

    kputs("producer VA=");
    kprint_hex(producer_va);
    kputs(" consumer VA=");
    kprint_hex(consumer_va);
    kputs("\nshared PA=");
    kprint_hex(producer_pa);
    kputs("\n");

    require_demo(producer_va != consumer_va,
                 "domains receive distinct logical virtual addresses");
    require_demo(producer_pa != 0 && producer_pa == consumer_pa,
                 "both mappings resolve to same physical backing");

    u32 *producer_ptr = (u32 *)ai_mapping_kernel_address(
        producer,
        producer_map
    );
    const u32 *consumer_ptr = (const u32 *)ai_mapping_kernel_address(
        consumer,
        consumer_map
    );

    require_demo(producer_ptr != NULL && consumer_ptr != NULL,
                 "kernel can access mapped backing");
    producer_ptr[0] = 0x4e45584fu; /* 'NEXO' */
    require_demo(consumer_ptr[0] == 0x4e45584fu,
                 "producer write is visible without tensor copy");

    require_demo(
        ai_tensor_map(
            consumer,
            shared,
            AI_MAP_PROT_READ | AI_MAP_PROT_WRITE
        ) == AI_MAPPING_INVALID,
        "consumer cannot exceed delegated mapping rights"
    );

    require_demo(ai_handle_close(producer, owner),
                 "close producer tensor handle");
    require_demo(ai_handle_close(consumer, shared),
                 "close consumer tensor handle");
    require_demo(ai_domain_begin_quiesce(producer),
                 "mapping producer enters quiescing state");
    require_demo(ai_domain_begin_quiesce(consumer),
                 "mapping consumer enters quiescing state");
    require_demo(!ai_domain_destroy(producer),
                 "live mapping blocks domain destruction");
    require_demo(!ai_domain_destroy(consumer),
                 "shared mapping blocks consumer destruction");

    require_demo(ai_tensor_unmap(producer, producer_map),
                 "producer unmaps shared backing");
    require_demo(ai_tensor_unmap(consumer, consumer_map),
                 "consumer unmaps shared backing");
    require_demo(backing->mapping_count == 0,
                 "mapping count drains to zero");
    require_demo(ai_tensor_put(tensor),
                 "release mapping-demo tensor creator reference");
    require_demo(ai_backing_put(backing),
                 "release mapping-demo backing creator reference");
    require_demo(ai_domain_destroy(producer),
                 "producer destroys after unmap");
    require_demo(ai_domain_destroy(consumer),
                 "consumer destroys after unmap");
}

static void run_lifetime_demo(void) {
    ai_domain_limits unlimited = {
        .memory_bytes = 0,
        .max_tensors = 0,
        .max_work_nodes = 0
    };

    u64 tensors_before = ai_tensor_count();
    u64 backings_before = ai_backing_count();

    ai_domain *producer = ai_domain_create("lifetime-producer", 0, unlimited);
    ai_domain *consumer = ai_domain_create("lifetime-consumer", 0, unlimited);
    ai_domain_activate(producer);
    ai_domain_activate(consumer);

    u64 shape[1] = {1024};
    ai_tensor *tensor = ai_tensor_create(
        "lifetime-demo",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );
    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes,
        AI_BACKING_ZEROED
    );

    kputs("\n[reference lifetime demo]\n");

    require_demo(backing != NULL && ai_tensor_attach_backing(tensor, backing, 0),
                 "create managed tensor and backing");
    require_demo(ai_tensor_refcount(tensor) == 1,
                 "tensor starts with creator reference");
    require_demo(ai_backing_refcount(backing) == 2,
                 "attached backing has creator plus tensor reference");

    ai_handle_t owner = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE
    );
    ai_handle_t shared = ai_handle_share(
        producer,
        owner,
        consumer,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    require_demo(owner != AI_HANDLE_INVALID && shared != AI_HANDLE_INVALID,
                 "handles retain tensor references");
    require_demo(ai_tensor_refcount(tensor) == 3,
                 "creator plus two handle references accounted");

    ai_mapping_id_t producer_map = ai_tensor_map(
        producer,
        owner,
        AI_MAP_PROT_READ | AI_MAP_PROT_WRITE
    );
    ai_mapping_id_t consumer_map = ai_tensor_map(
        consumer,
        shared,
        AI_MAP_PROT_READ
    );
    require_demo(producer_map != AI_MAPPING_INVALID &&
                 consumer_map != AI_MAPPING_INVALID,
                 "mappings retain tensor and backing references");
    require_demo(ai_tensor_refcount(tensor) == 5,
                 "tensor reference graph includes two mappings");
    require_demo(ai_backing_refcount(backing) == 4 &&
                 backing->mapping_count == 2,
                 "backing reference graph includes mappings");

    require_demo(ai_tensor_put(tensor),
                 "drop tensor creator reference");
    require_demo(ai_backing_put(backing),
                 "drop backing creator reference");
    require_demo(ai_handle_close(producer, owner) &&
                 ai_handle_close(consumer, shared),
                 "close all tensor handles");
    require_demo(ai_tensor_refcount(tensor) == 2 && ai_tensor_is_live(tensor),
                 "live mappings keep tensor alive after handle close");

    require_demo(ai_tensor_unmap(producer, producer_map),
                 "release first mapping reference");
    require_demo(ai_tensor_refcount(tensor) == 1 &&
                 ai_backing_refcount(backing) == 2,
                 "final mapping owns final tensor reference");
    require_demo(ai_tensor_unmap(consumer, consumer_map),
                 "release final mapping reference");

    require_demo(tensor->lifetime_state == AI_TENSOR_RETIRED &&
                 tensor->refcount == 0,
                 "final tensor reference retires tensor metadata");
    require_demo(backing->lifetime_state == AI_BACKING_RETIRED &&
                 backing->refcount == 0,
                 "final backing reference retires backing storage object");
    require_demo(ai_tensor_count() == tensors_before &&
                 ai_backing_count() == backings_before,
                 "retired objects leave live registries");
    require_demo(!ai_tensor_get(tensor) && !ai_backing_get(backing),
                 "retired objects cannot be resurrected");

    require_demo(ai_domain_begin_quiesce(producer) &&
                 ai_domain_begin_quiesce(consumer),
                 "lifetime domains enter quiescing state");
    require_demo(ai_domain_destroy(producer) && ai_domain_destroy(consumer),
                 "lifetime domains destroy after reference drain");
}

static void run_domain_demo(void) {
    ai_domain_limits producer_limits = {
        .memory_bytes = 1024 * 1024,
        .max_tensors = 4,
        .max_work_nodes = 8
    };

    ai_domain_limits consumer_limits = {
        .memory_bytes = 512 * 1024,
        .max_tensors = 2,
        .max_work_nodes = 4
    };

    ai_domain *producer = ai_domain_create(
        "producer",
        AI_DOMAIN_TRUSTED,
        producer_limits
    );

    ai_domain *consumer = ai_domain_create(
        "consumer",
        0,
        consumer_limits
    );

    ai_domain_activate(producer);
    ai_domain_activate(consumer);

    kputs("\n[work-domain demo]\n");
    print_domain(producer);
    print_domain(consumer);

    kputs("producer charge 256 KiB: ");
    kputs(ai_domain_charge_memory(producer, 256 * 1024) ? "ok\n" : "rejected\n");

    kputs("consumer charge 768 KiB: ");
    kputs(ai_domain_charge_memory(consumer, 768 * 1024) ? "ok\n" : "rejected\n");

    kputs("producer tensor reservation: ");
    kputs(ai_domain_reserve_tensor(producer) ? "ok\n" : "rejected\n");

    print_domain(producer);

    ai_domain_release_tensor(producer);
    ai_domain_uncharge_memory(producer, 256 * 1024);
    ai_domain_begin_quiesce(producer);

    kputs("producer destroy: ");
    kputs(ai_domain_destroy(producer) ? "ok\n" : "rejected\n");

    kputs("live domains: ");
    kprint_u64(ai_domain_count());
    kputs("\n");
}


static void run_revocation_demo(void) {
    ai_domain_limits unlimited = {
        .memory_bytes = 0,
        .max_tensors = 0,
        .max_work_nodes = 0
    };

    ai_domain *domain = ai_domain_create("revocation-demo", 0, unlimited);
    require_demo(domain != NULL && ai_domain_activate(domain),
                 "create revocation demo domain");

    u64 shape[1] = {16};
    ai_tensor *tensor = ai_tensor_create(
        "revocation-demo-tensor",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_handle_t handle = ai_handle_install(
        domain,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );

    kputs("\n[revocation + concurrency-safety demo]\n");
    require_demo(handle != AI_HANDLE_INVALID,
                 "install revocable tensor handle");

    ai_tensor *pin = (ai_tensor *)ai_handle_acquire(
        domain,
        handle,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );
    require_demo(pin == tensor, "acquire stable tensor pin");

    require_demo(ai_tensor_put(tensor), "drop creator tensor reference");
    require_demo(ai_handle_revoke(domain, handle), "revoke handle");
    require_demo(ai_handle_is_revoked(domain, handle),
                 "revoked state is visible");
    require_demo(
        ai_handle_acquire(
            domain,
            handle,
            AI_HANDLE_OBJECT_TENSOR,
            AI_HANDLE_RIGHT_READ) == NULL,
        "revocation blocks new acquisitions"
    );
    require_demo(ai_handle_reap_revoked(domain) == 1,
                 "reap revoked namespace entry");
    require_demo(ai_tensor_is_live(tensor),
                 "pre-revocation pin survives namespace revocation");
    require_demo(ai_handle_release_object(pin, AI_HANDLE_OBJECT_TENSOR),
                 "release stable tensor pin");
    require_demo(!ai_tensor_is_live(tensor),
                 "final pinned reference retires tensor exactly once");

    u64 object_a = 1;
    u64 object_b = 2;
    ai_handle_t stale = ai_handle_install(
        domain,
        &object_a,
        AI_HANDLE_OBJECT_GENERIC,
        AI_HANDLE_RIGHT_READ
    );
    u32 stale_generation = ai_handle_generation(stale);
    require_demo(ai_handle_revoke(domain, stale), "revoke generic handle");
    require_demo(ai_handle_reap_revoked(domain) == 1,
                 "reap generic revoked handle");

    ai_handle_t fresh = ai_handle_install(
        domain,
        &object_b,
        AI_HANDLE_OBJECT_GENERIC,
        AI_HANDLE_RIGHT_READ
    );
    require_demo(fresh != AI_HANDLE_INVALID,
                 "reuse handle slot with fresh generation");
    require_demo(ai_handle_generation(fresh) != stale_generation,
                 "generation changes after revocation/reap");
    require_demo(
        ai_handle_resolve(
            domain,
            stale,
            AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ) == NULL,
        "stale token remains invalid"
    );
    require_demo(ai_handle_close(domain, fresh), "close fresh handle");
    require_demo(ai_domain_begin_quiesce(domain),
                 "quiesce revocation demo domain");
    require_demo(ai_domain_destroy(domain),
                 "destroy empty revocation demo domain");
}

static void run_demo(void) {
    kputs("\n[AIKernel demo]\n");

    u64 input_shape[2]   = {1, 4096};
    u64 weight_shape[2]  = {4096, 4096};
    u64 hidden_shape[2]  = {1, 4096};

    ai_tensor *input = ai_tensor_create(
        "input",
        AI_DTYPE_F16,
        2,
        input_shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *weights = ai_tensor_create(
        "weights",
        AI_DTYPE_F16,
        2,
        weight_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY
    );

    ai_tensor *hidden = ai_tensor_create(
        "hidden",
        AI_DTYPE_F16,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *output = ai_tensor_create(
        "output",
        AI_DTYPE_F16,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    print_tensor(input);
    print_tensor(weights);
    print_tensor(hidden);
    print_tensor(output);

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *matmul = ai_work_add(
        &graph,
        "projection",
        AI_OP_MATMUL,
        100,
        1000000,
        AI_DEVICE_GPU
    );
    ai_work_add_input(matmul, input);
    ai_work_add_input(matmul, weights);
    ai_work_add_output(matmul, hidden);

    ai_work_node *activation = ai_work_add(
        &graph,
        "activation",
        AI_OP_ACTIVATION,
        90,
        2000000,
        AI_DEVICE_GPU
    );
    ai_work_add_dependency(activation, matmul->id);
    ai_work_add_input(activation, hidden);
    ai_work_add_output(activation, output);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    print_work(matmul);
    print_work(activation);

    ai_work_node *selected = ai_scheduler_pick(&scheduler);
    kputs("scheduler selected node ");
    kprint_u64(selected ? selected->id : 0);
    kputs("\n");

    ai_scheduler_mark_running(&scheduler, selected);
    ai_scheduler_mark_done(&scheduler, selected);

    selected = ai_scheduler_pick(&scheduler);
    kputs("scheduler selected node ");
    kprint_u64(selected ? selected->id : 0);
    kputs("\n");

    ai_scheduler_mark_running(&scheduler, selected);
    ai_scheduler_mark_done(&scheduler, selected);

    ai_capability agent_cap = ai_cap_create(
        42,
        AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );

    kputs("agent 42 GPU capability: ");
    kputs(ai_cap_has(&agent_cap, AI_CAP_GPU_USE) ? "yes\n" : "no\n");

    kputs("early heap used: ");
    kprint_u64(early_heap_used());
    kputs(" / ");
    kprint_u64(early_heap_capacity());
    kputs(" bytes\n");
}

void kmain(void) {
    console_init();

    kputs("AIKernel x86_64 booted.\n");

    early_heap_init();
    kputs("Early heap initialized.\n");

    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();
    kputs("AI runtime metadata initialized.\n");

    run_domain_demo();
    run_handle_demo();
    run_delegation_demo();
    run_mapping_demo();
    run_lifetime_demo();
    run_revocation_demo();
    run_demo();

    kputs("\nAIKernel initialization complete.\n");
    kputs("Halting CPU.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
