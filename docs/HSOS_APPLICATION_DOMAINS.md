# HSOS Application Domains

> Produced via Lincoln Manifold Method — RAW → NODES → REFLECT → SYNTHESIZE
> Date: 2026-03-20

---

## What HSOS Actually Is

HSOS is a deterministic, auditable, edge-deployable binary metric classifier with optional symbolic constraint resolution. Its fundamental operation is two steps:

1. **Compress**: linear layers map a rich input (sensor window, audio frame, byte sequence) to a 512-bit binary code
2. **Match**: Hamming distance comparison against frozen signature prototypes

Supporting primitives:
- **8-way parallel workers** — 8 signatures evaluated simultaneously per input
- **BubbleMachine** — distributed sort via OP_CSWAP; ranks matches by distance
- **ConstraintField** — CSP solver; resolves conflicts between multiple match candidates
- **Recording/replay** — deterministic tick-based execution; every decision is reproducible
- **Fixed 16-byte messages** — embedded-friendly, no dynamic allocation, real-time capable

The `-1` (no match) result is not a failure — it is information. It means the input is outside the trained distribution.

---

## Training Direction

The linear layers should be trained as a **binary metric learner**:

- **Objective**: learn weights such that same-class inputs sign-binarize to 512-bit codes with small intra-class Hamming distance, and different-class inputs land far apart
- **Loss function**: triplet loss over Hamming distance, or binary cross-entropy with code regularization (XNOR-Net / BNN training family)
- **Prototypes**: the frozen signatures in the chip ARE the class prototypes — cluster centers of the trained binary codes
- **Thresholds**: set from intra-class distance distribution during training (e.g., mean + 2σ)
- **Pipeline**: offline supervised training → extract 512-bit codes → store as chip signatures → freeze and deploy

**Training pipeline (implemented)**: `tools/python/train.py` trains a BNN encoder using ClampSTE activation (matching the C runtime's `clamp_to_int8` inter-layer behavior), exports int8-quantized weights and `.trix` chip files via `tools/python/export.py`. Prototype signatures and thresholds are computed from int8-simulated forward passes to match C runtime inference exactly.

**Recommended first training target**: industrial vibration fault detection (e.g., CWRU bearing dataset). Reasons: publicly available, stable class distribution, compressible signal structure, auditability required, natural binary classification that scales to multi-class.

---

## Primary Application Domains

### Tier 1 — Best Fit

#### Embedded Signal Classification
Sensor stream → linear compress → 512-bit code → Hamming match → class label.

- Vibration/acoustic anomaly detection
- Motor and bearing health monitoring
- HVAC and industrial equipment classification
- Activity recognition from accelerometer/gyroscope

**Why**: compressible physical signals, stable class distributions, edge deployment, auditability required in safety/industrial contexts, frozen deployment matches firmware update cadence.

#### Wake-Word / Keyword Spotting
Audio frame (MFCC or raw spectrogram window) → compress → match → keyword / not-keyword.

**Why**: classic embedded ML task, latency-critical, MCU deployment, stable target vocabulary, binary decision that scales to small vocabulary classification.

---

### Tier 2 — Strong Fit

#### Anomaly / Fault Detection
Train known-good patterns. Anomaly = all signature distances exceed threshold → returns `-1`.

- Industrial equipment monitoring (no known fault required — just "not normal")
- Network intrusion detection (known attack signatures enrolled)
- Medical vitals deviation alerting
- Hardware fault detection

**Why**: Hamming distance naturally quantifies deviation. You don't need to enumerate every failure mode — only known-good states. The `-1` result is the anomaly signal.

#### Multi-Sensor Fusion with Constraint Resolution
8 workers each match a different sensor channel simultaneously. CSP resolves conflicting channel results.

- Driver monitoring (eye tracking + steering + speed + head pose)
- Smart building (occupancy + HVAC + CO₂ + door state)
- UAV sensor fusion (IMU + GPS + barometer + optical flow)
- Medical triage (multiple vital signs assessed in parallel)

**Why**: uses both the 8-way parallelism AND the CSP layer together. This is where the full HSOS architecture justifies its design.

---

### Tier 3 — Moderate Fit

#### Protocol / Packet Classification
Network packet bytes → 512-bit code → protocol class or threat category.

Fits learned intrusion detection (Snort-like signature matching, but trained rather than hand-coded). Caveat: 16-byte fragment reassembly creates latency; best for near-real-time analysis, not line-rate.

---

### Not a Fit

| Domain | Reason |
|---|---|
| Natural language understanding | Unstructured; 512 bits cannot represent semantic space |
| Continuous regression | HSOS outputs class labels, not scalar values |
| Generative tasks | Architecturally incompatible |
| Online learning / RL adaptation | Frozen computation is the design |
| Large-context tasks (>64 bytes of raw signal) | Requires architectural changes to input pipeline |

---

## Off-Label Applications

These are non-obvious uses that fall out of HSOS's primitives when you look past the embedded-ML framing.

---

### Behavioral Biometrics

Enroll a user's typing rhythm, mouse dynamics, or touchscreen pressure pattern as a 512-bit binary code. Continuous passive authentication: "is this still the enrolled user?" Hamming distance tolerance handles natural behavioral variation. Drift beyond threshold → alert.

Frozen deployment is a feature here: the enrolled behavioral model cannot be poisoned post-deployment.

---

### Novelty Reward Signal for Reinforcement Learning

The `-1` (no match) result already means "this state is novel." Wire it directly into an RL intrinsic reward function:
- Match → familiar state → low intrinsic reward
- No match → novel state → high intrinsic reward

Extremely cheap curiosity signal. HSOS runs on-device at the edge of an RL loop, providing novelty detection without a learned value function or count-based density estimator.

---

### Counterfeit Detection / PUF Authentication

Physical Unclonable Functions produce noisy binary challenge-response pairs. Hamming distance tolerance is exactly the right comparison operator: enrolled response vs. current response within threshold. HSOS is structurally isomorphic to a PUF verifier — no adaptation required.

Applications: hardware device authentication, supply chain integrity, anti-counterfeiting in embedded products.

---

### Protocol Fuzzing Oracle

During guided fuzzing (AFL, libFuzzer), check whether the current program state or coverage bitmap matches any known crash signature. Fast deterministic oracle to steer the fuzzer toward unexplored or crash-adjacent territory — replaces expensive sanitizer checks on hot inner loops with a Hamming match.

The `-1` result means "novel state" → prioritize this input. A match to a known-crash signature → flag immediately.

---

### Content-Addressed Approximate Routing

Route messages not by explicit address but by content similarity. "Deliver this to whichever handler's signature best matches this payload." BubbleMachine + OP_CSWAP becomes a priority queue for approximate nearest-neighbor routing. The system routes by *what* the message is, not *where* it was sent.

Applications: event routing in reactive systems, approximate cache lookup, message deduplication pipelines.

---

### LLM Hallucination Detection

Extract features from LLM output (entity claims, numerical assertions, citation patterns, hedge language markers) → compress to 512-bit code → match against signatures of known hallucination patterns collected offline.

A tiny, deterministic layer that runs on-device without a round-trip to a validator. The frozen deployment model means the hallucination detector itself cannot hallucinate or drift.

---

### Distributed Ensemble Without a Coordinator

Run 8 chips with different signature sets — each trained on a different feature projection of the same input. Each worker returns a class vote. CSP resolves conflicts. BubbleMachine sorts votes by confidence.

Ensemble inference with no central aggregator. The HSOS architecture IS the ensemble logic. Useful in adversarial settings where a single classifier can be targeted but 8 independent classifiers are hard to simultaneously fool.

---

### Genomic k-mer Matching

DNA sequence windows → binary k-mer encoding → 512-bit code → Hamming match against known pathogen or variant signatures. Hamming tolerance naturally handles sequencing noise (base-calling errors produce nearby codes, not distant ones). Enables portable, edge-deployable pathogen screening without cloud connectivity.

---

### Compiler Provenance Fingerprinting

"Which toolchain compiled this binary?" Code section byte patterns (instruction density, opcode distribution, padding patterns) → 512-bit code → match against signatures for GCC -O2, Clang -Os, MSVC /O2, etc. Useful for supply chain verification, binary analysis pipelines, and license compliance (detecting GPL code compiled with specific toolchains).

---

### Deterministic Game AI

NPC behavior as a lookup: world state → 512-bit code → behavior class → action. The full decision is a Hamming match — O(1), deterministic, auditable.

Recording/replay means the exact same game sequence is perfectly reproducible. Applications: speedrun verification, tournament fairness certification, replay analysis, procedural AI behavior that can be shipped as a frozen chip file rather than a neural network.

---

## The Generalizing Principle

Anywhere you currently use a hash + equality check, HSOS's Hamming distance gives you approximate matching with a tunable tolerance threshold. That is the off-label generalization:

> HSOS is a fuzzy content-addressable store with parallel lookup, a distributed sort, and symbolic constraint resolution.

Most systems that do fuzzy lookup today do it expensively — LSH, FAISS, annoy, ScaNN. HSOS does it on a microcontroller in deterministic, bounded time with a cryptographically reproducible audit trail.

The training objective in all cases is the same: learn a binary hash function for your domain such that semantic similarity maps to Hamming proximity.

---

## Summary Table

| Domain | Primary? | Off-Label? | Uses CSP? | Uses Parallelism? |
|---|---|---|---|---|
| Embedded signal classification | ✓ | | | ✓ |
| Wake-word / keyword spotting | ✓ | | | |
| Anomaly / fault detection | ✓ | | | |
| Multi-sensor fusion | ✓ | | ✓ | ✓ |
| Protocol classification | ✓ | | | |
| Behavioral biometrics | | ✓ | | |
| RL novelty reward signal | | ✓ | | |
| PUF / counterfeit detection | | ✓ | | |
| Fuzzing oracle | | ✓ | | |
| Content-addressed routing | | ✓ | ✓ | ✓ |
| LLM hallucination detection | | ✓ | | |
| Distributed ensemble | | ✓ | ✓ | ✓ |
| Genomic k-mer matching | | ✓ | | |
| Compiler provenance | | ✓ | | |
| Deterministic game AI | | ✓ | | |
