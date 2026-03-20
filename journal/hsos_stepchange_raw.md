# HSOS Step-Change — RAW

*Phase 1 of Lincoln Manifold Method. Unfiltered. 2026-03-19.*

---

The obvious move is to implement OP_COMPUTE and call it done. But that feels
too thin. It's a handler, not a step-change. The opcodes being reserved is a
clue, not a plan.

What actually frightens me here: HSOS is beautiful in simulation and may be
completely impractical when you try to attach it to real inference. The tick
model is cooperative — everything yields at message boundaries. That's fine
when you're sorting 8 integers. Is it fine when a worker needs to run
popcount64 over 512 bits against a thousand signatures? That's real compute
happening inside a tick. Does the tick model survive that? Or does one
overloaded worker stall the whole system?

The record buffer is 256 messages. A single inference across 8 workers is
already: 1 broadcast + 8 replies = 9 messages minimum. Add fragmentation for
the 64-byte payload (10-byte max payload = 7 fragments per input) and you're
at 56 messages just for input delivery. You hit the 256-message cap in 4
inferences. That's not a quirk, that's a structural blocker.

Fragmentation is undefined. MSG_FLAG_FRAGMENT and MSG_FLAG_LAST_FRAG are
defined in the header but there is zero reassembly code anywhere. So the
"carry a 64-byte input in a 10-byte payload" problem is genuinely unsolved.
This is the most concrete blocker. Everything else is architecture; this is
plumbing.

Parallel tick feels exciting but I'm not sure the safety argument is as clean
as I said. "Nodes share no mutable state" — is that actually true? The
fabric directory is shared state. `sys->tick` is shared state. The
`bus_delivered` counter is shared state. The trace buffer is shared state.
The record buffer is shared state. So "parallel node execution is safe" holds
for the nodes themselves, but the system-level counters are all races. You'd
need atomics or you'd need to move those updates to the serial bus_deliver
phase. That's fixable but it's not free.

The sharding idea is good but it introduces a new problem: what happens when
N is not divisible by 8? What happens when a chip has 3 signatures? One
worker gets 1, six workers get 0. Idle workers = wasted round trips = worse
performance than just doing it serially. There needs to be a minimum
signature count threshold below which HSOS inference doesn't engage — fall
back to the tight loop in runtime.c. Hybrid routing. That's more complex than
"shard everything."

The provenance angle is the strongest argument for doing this at all. Right
now trix_result_t tells you what matched but not why the system reached that
answer. With HSOS inference you get a trace — every comparison, every
rejection, the tick at which worker 3 beat worker 5. That's what makes this
certifiable. That's what makes it auditable. That's what makes it different
from every other edge inference runtime. The performance story is fine but
it's not unique. The provenance story is unique.

Typed protocol contracts sound rigorous but I'm worried they're busywork at
this stage. The real question is: do they enable verification, or do they just
document what's already there? If you're not running a model checker, they're
just comments. And running TLA+ or SPIN on this system is a significant
undertaking. What's the minimum useful version? Maybe: machine-readable
pre/post annotations per opcode that get checked at runtime in debug builds.
NDEBUG strips them. That's achievable.

The replay bug I noticed: hsos_replay() injects messages directly into
inboxes, bypassing bus_deliver(). That means replayed messages don't get
recorded (if recording is active during replay), don't increment
bus_delivered, and don't go through the routing logic. So replay is not
actually a faithful re-execution — it's a shortcut that happens to produce
the same end state only because the nodes are deterministic and the messages
are identical. But if you ever add side effects at the bus level (logging,
rate limiting, priority reordering), replay breaks silently. That's a
correctness time bomb.

What if the real step-change isn't any of the above? What if it's making
HSOS the compiler target, not the runtime? Instead of running inference inside
HSOS ticks, you use HSOS to generate the inference code. The constraint field
already does something like this — it propagates until fixed point. What if
you ran the chip design process itself through HSOS, and the output of HSOS
execution was a verified chip spec? That's a completely different direction.
Probably too far out. But worth noting.

The thing I keep coming back to: the step-change isn't adding features. It's
closing the loop between the two systems that already exist. runtime.c and
hsos.c are both working. They need to be connected. Everything else —
parallelism, contracts, extended replay — falls out of that connection being
made correctly.

Open questions:
1. How do you carry 64 bytes through a 10-byte payload max without
   fragmentation reassembly?
2. At what signature count does HSOS inference beat serial inference?
3. What does "replay faithfulness" actually require — is the current approach
   sufficient for the certification claim?
4. Can you make the parallel tick safe without touching the serial bus phase?
5. Is typed protocol contract verification achievable without a model checker?
6. Is there a signature count floor below which HSOS inference should not
   engage?
