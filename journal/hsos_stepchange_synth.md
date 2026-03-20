# HSOS Step-Change — SYNTHESIS

*Phase 4 of Lincoln Manifold Method. Actionable output. 2026-03-19.*

---

## The Step-Change in One Sentence

Make inference a first-class HSOS computation with intrinsic provenance,
faithful replay, and dynamic worker assignment — delivered as four
sequenced, independently testable changes.

---

## What We Are NOT Building (Resolved Deferrals)

- **Parallel tick** — deferred. No performance data justifies it yet.
  Revisit when signature counts are large enough to measure.
- **Full model-checker integration** — deferred. Machine-readable debug
  assertions are the right form now.
- **Compiler-target direction** (HSOS as chip design verifier) — noted,
  not pursued. Future research, not next step.

---

## The Four Changes — Sequenced and Testable

### Change 1: Message Fragmentation

**What:** Implement fragment send and reassembly in `hsos.c`.

**Why it's first:** Every other change depends on carrying 64+ bytes across
the bus. This is the root blocker.

**Spec:**
- `hsos_send_fragmented(node, msg_type, dst, data, len)` — splits `data`
  into 10-byte chunks, sets `MSG_FLAG_FRAGMENT` on all but the last,
  sets `MSG_FLAG_LAST_FRAG` on the last, sends in sequence.
- Each worker node accumulates fragments in a per-sender reassembly buffer
  (indexed by src node ID). On `MSG_FLAG_LAST_FRAG`, the buffer is complete
  and the message is dispatched to the handler.
- Reassembly buffer: fixed 64-byte per sender (8 senders max = 512 bytes
  per node, fits in the 256-byte scratchpad only if kept out-of-band).
  Recommendation: add `uint8_t frag_buf[8][64]` and `uint8_t frag_len[8]`
  to `hsos_node_t`.

**Test:** Send 64-byte payload from master to each worker via fragmented
messages. Verify reassembly is exact. Verify `MSG_FLAG_LAST_FRAG` triggers
dispatch. Verify partial fragments don't dispatch early.

**Estimated scope:** ~80 lines in `hsos.c`, ~12 bytes per sender added to
`hsos_node_t`.

---

### Change 2: Faithful Replay

**What:** Route replay messages through `bus_deliver()` instead of directly
into node inboxes.

**Why it's second:** Fragmentation produces more messages per inference.
Replay must remain exact through the fragmentation layer.

**Spec:**
- Add `bool replay_mode` flag to `hsos_system_t`.
- `hsos_replay()` sets `sys->replay_mode = true`, then calls
  `hsos_send()` for each recorded message from node 0 (master), then
  calls `hsos_run()` normally.
- In `bus_deliver()`, add: if `sys->replay_mode && sys->recording`, skip
  re-recording replayed messages (suppress the `record_buffer` write).
- After `hsos_run()` completes, clear `replay_mode`.

**Test:** Record an inference run. Replay it. Assert that worker memory
states after replay exactly match worker memory states after original run.
Assert that `bus_delivered` counts match.

**Estimated scope:** ~20 lines across `hsos.c` + 1 bool field in
`hsos_system_t`.

---

### Change 3: Dynamic Record Buffer

**What:** Replace the fixed 256-message `record_buffer[256]` with a
configurable ring buffer.

**Why it's third:** OP_COMPUTE inference generates ~65 messages per call
with fragmentation. The fixed buffer caps out at ~4 inferences.

**Spec:**
- Replace `hsos_msg_t record_buffer[256]` in `hsos_system_t` with
  `hsos_msg_t *record_buffer` (pointer) and `uint16_t record_capacity`.
- `hsos_init()` allocates with `calloc(HSOS_DEFAULT_RECORD_CAP,
  sizeof(hsos_msg_t))` where `HSOS_DEFAULT_RECORD_CAP = 4096`.
- `hsos_init_with_capacity(sys, record_cap)` for custom sizes.
- `hsos_system_free(sys)` frees the buffer.
- Ring behavior: when full, oldest messages are overwritten (current code
  silently truncates — ring is strictly better for long traces).

**Test:** Record >256 messages. Verify ring wraps correctly. Verify
`record_count` saturates at `record_capacity`. Verify replay uses full
ring correctly.

**Estimated scope:** ~30 lines in `hsos.c` + `hsos_system_free()` added
to public API.

---

### Change 4: OP_COMPUTE / OP_COMPUTE_OK — HSOS Native Inference

**What:** Implement distributed Hamming distance inference over HSOS workers.

**Why it's fourth:** Depends on fragmentation (Change 1), faithful replay
(Change 2), and sufficient record capacity (Change 3).

**Spec:**

*Master side (in `hsos_exec_infer()` — new public API function):*
```c
trix_result_t hsos_exec_infer(hsos_system_t *sys,
                               trix_chip_t *chip,
                               const uint8_t input[64]);
```
1. Assign shards: for each worker i, assign signature range
   `[i * shard_size .. (i+1) * shard_size)`. Skip workers where shard
   is empty (handles N < 8 gracefully).
2. For each assigned worker: send shard metadata (start index, count) via
   `OP_LOAD` messages, then send the 64-byte input via fragmented
   `OP_COMPUTE` messages.
3. Run until all assigned workers have replied `OP_COMPUTE_OK` or timeout
   (100 ticks).
4. Aggregate replies: pick minimum distance across all `OP_COMPUTE_OK`
   payloads.
5. Return `trix_result_t` with `trace_tick_start` and `trace_tick_end`
   filled from `sys->tick`.

*Worker side (`handle_compute()` — new static function in `hsos.c`):*
1. Receive reassembled 64-byte input from fragmentation layer.
2. Read shard metadata (start index, count) from `node->memory`.
3. For each signature in shard: call `popcount64(input, chip->signatures[i])`.
4. Track minimum distance and its index, threshold, and label.
5. Send `OP_COMPUTE_OK` to master: payload = `{match_idx(2), distance(2),
   threshold(2), label_len(1), label(up to 3 chars truncated)}` — fits in
   10 bytes. Full label retrieval via separate `OP_EXEC` / `trix_label()`
   call if needed.

*Protocol contract (debug assertions):*
```c
HSOS_ASSERT(OP_COMPUTE,
    pre:  payload reassembled to exactly 64 bytes,
    post: OP_COMPUTE_OK sent with valid match_idx or -1);
HSOS_ASSERT(OP_COMPUTE_OK,
    pre:  payload[0..1] = match_idx (int16), payload[2..3] = distance,
    post: master aggregated result includes this worker's candidate);
```

**Test:**
- Load a chip with 8 signatures. Run `hsos_exec_infer()`. Assert result
  matches `trix_infer()` output exactly.
- Load a chip with 1 signature (small N). Assert correct dynamic assignment
  (1 worker used, 7 idle).
- Record inference run. Replay. Assert identical result.
- Run 10 consecutive inferences. Assert record buffer capacity not exceeded.

**Estimated scope:** ~150 lines in `hsos.c`, ~30 lines in `runtime.h`
(new API), ~40 lines of test code.

---

## Success Criteria

A reviewer can confirm the step-change is complete when:

1. `hsos_exec_infer(sys, chip, input)` returns a `trix_result_t` that
   is bit-identical to `trix_infer(chip, input)` for all test chips.
2. Recording + replay of an inference run produces the same result as
   the original run, verified by the existing `determinism_test.c`
   framework.
3. A chip with N = 1 signature and a chip with N = 64 signatures both
   produce correct results (small-N and large-N correctness).
4. The record buffer does not overflow for 10 consecutive inferences on
   a chip with 64 signatures.
5. The HSOS trace from any inference run includes the tick range, worker
   assignments, and per-worker match results — viewable via
   `hsos_dump_trace()`.

---

## What This Enables Next

Once the four changes are in:
- **Parallel tick** — revisit with real throughput measurements
- **Typed contract verification** — extend HSOS_ASSERT macros to a
  machine-checkable format
- **HSOS as compiler target** — the constraint field + OP_COMPUTE
  infrastructure is the foundation for running chip design verification
  through the same runtime

---

*See also: `journal/hsos_stepchange_raw.md`, `_nodes.md`, `_reflect.md`*
*and `docs/HSOS_STEP_CHANGE.md` for full architectural background.*
