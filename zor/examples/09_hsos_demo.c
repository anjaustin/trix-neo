/*
 * 09_hsos_demo.c — Hollywood Squares OS Demonstration
 *
 * "Structure is meaning."
 * "The wiring determines the behavior.
 *  The messages carry the computation.
 *  The trace tells the story."
 *
 * This demo shows:
 *   1. System boot and node ping
 *   2. BubbleMachine: Distributed sorting via compare-swap
 *   3. ConstraintField: CSP solving via constraint propagation
 *
 * Port of anjaustin/hollywood-squares-os to frozen C.
 */

#include <stdio.h>
#include <string.h>
#include "../include/hsos.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_array(const char *label, const uint8_t arr[8]) {
    printf("%s [", label);
    for (int i = 0; i < 8; i++) {
        printf("%3d", arr[i]);
        if (i < 7) printf(",");
    }
    printf(" ]\n");
}

static void print_separator(const char *title) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("═══════════════════════════════════════════════════════════════\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEMO 1: SYSTEM BOOT
 * ═══════════════════════════════════════════════════════════════════════════ */

static void demo_boot(hsos_system_t *sys) {
    print_separator("DEMO 1: SYSTEM BOOT");

    printf("Initializing Hollywood Squares OS...\n\n");
    hsos_init(sys);

    printf("Booting 8 worker nodes...\n");
    int online = hsos_boot(sys);
    printf("  Online: %d / 8 workers\n\n", online);

    printf("Pinging each node:\n");
    for (int i = 1; i <= 8; i++) {
        bool ok = hsos_ping(sys, i);
        printf("  Node %d: %s\n", i, ok ? "PONG" : "TIMEOUT");
    }

    uint32_t ticks, delivered, dropped;
    hsos_stats(sys, &ticks, &delivered, &dropped);
    printf("\nSystem stats:\n");
    printf("  Ticks:     %u\n", ticks);
    printf("  Delivered: %u messages\n", delivered);
    printf("  Dropped:   %u messages\n", dropped);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEMO 2: BUBBLE MACHINE — Distributed Sorting
 * ═══════════════════════════════════════════════════════════════════════════ */

static void demo_bubble_machine(hsos_system_t *sys) {
    print_separator("DEMO 2: BUBBLE MACHINE (Distributed Sort)");

    printf("\"Every comparison is a message. Every swap is traceable.\n");
    printf(" Every execution is replayable.\"\n\n");

    printf("The BubbleMachine sorts values across 8 distributed nodes\n");
    printf("using odd-even transposition sort.\n\n");

    printf("Topology: LINE (odd-even phases)\n");
    printf("  EVEN phase: pairs (1,2), (3,4), (5,6), (7,8)\n");
    printf("  ODD phase:  pairs (2,3), (4,5), (6,7)\n\n");

    hsos_bubble_t bm;
    hsos_bubble_init(&bm, sys, TOPO_LINE);

    /* Same input as original Python demo */
    uint8_t input[8] = {64, 25, 12, 22, 11, 90, 42, 7};
    print_array("Input: ", input);

    hsos_bubble_load(&bm, input);
    printf("\nRunning bubble sort...\n");

    int cycles = hsos_bubble_run(&bm);

    uint8_t output[8];
    hsos_bubble_read(&bm, output);

    printf("\n");
    print_array("Output:", output);

    printf("\nStatistics:\n");
    printf("  Cycles:   %u\n", bm.cycles);
    printf("  Swaps:    %u\n", bm.swaps);
    printf("  Messages: %u\n", bm.messages);

    /* Verify sorted */
    bool sorted = true;
    for (int i = 0; i < 7; i++) {
        if (output[i] > output[i+1]) {
            sorted = false;
            break;
        }
    }
    printf("\nResult: %s\n", sorted ? "SORTED ✓" : "NOT SORTED ✗");

    printf("\n");
    printf("Key insight: The TOPOLOGY determines the algorithm.\n");
    printf("Same handlers + different wiring = different behavior.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEMO 3: CONSTRAINT FIELD — CSP Solver
 * ═══════════════════════════════════════════════════════════════════════════ */

static void demo_constraint_field(hsos_system_t *sys) {
    print_separator("DEMO 3: CONSTRAINT FIELD (CSP Solver)");

    printf("\"Every elimination has a reason. Every reason is traceable.\n");
    printf(" Every solution is explainable.\"\n\n");

    printf("The ConstraintField solves constraint satisfaction problems\n");
    printf("via distributed domain elimination. No search. No backtracking.\n\n");

    printf("Constraint: ALL-DIFFERENT (8 cells, values 1-9)\n\n");

    hsos_constraint_t cf;
    hsos_constraint_init(&cf, sys);

    /* Set strategic givens that cascade to a solution */
    printf("Setting givens:\n");
    printf("  Cell 0 = 3\n");
    printf("  Cell 2 = 5\n");
    printf("  Cell 4 = 8\n");
    printf("  Cell 6 = 2\n\n");

    hsos_constraint_set_given(&cf, 0, 3);
    hsos_constraint_set_given(&cf, 2, 5);
    hsos_constraint_set_given(&cf, 4, 8);
    hsos_constraint_set_given(&cf, 6, 2);

    printf("Initial domains:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t val = hsos_constraint_get_value(&cf, i);
        if (val) {
            printf("  Cell %d = %d (given)\n", i, val);
        } else {
            printf("  Cell %d = {", i);
            for (int v = 1; v <= 9; v++) {
                if (cf.domains[i] & (1 << (v-1))) printf(" %d", v);
            }
            printf(" }\n");
        }
    }

    printf("\nPropagating constraints...\n");
    int eliminations = hsos_constraint_propagate(&cf);
    printf("  Eliminations: %d\n", eliminations);
    printf("  Messages:     %u\n", cf.messages);

    printf("\nFinal domains:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t val = hsos_constraint_get_value(&cf, i);
        if (val) {
            printf("  Cell %d = %d\n", i, val);
        } else {
            printf("  Cell %d = {", i);
            for (int v = 1; v <= 9; v++) {
                if (cf.domains[i] & (1 << (v-1))) printf(" %d", v);
            }
            printf(" }\n");
        }
    }

    /* Explain one cell's derivation */
    printf("\nExplainability: WHY is cell 1 what it is?\n");
    hsos_constraint_why(&cf, 1);

    printf("\n");
    printf("Key insight: \"This system doesn't search for solutions.\n");
    printf("              It relaxes toward them.\"\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEMO 4: CASCADE — Forced Solution
 * ═══════════════════════════════════════════════════════════════════════════ */

static void demo_cascade(hsos_system_t *sys) {
    print_separator("DEMO 4: CASCADE (Forced Solution)");

    printf("When 7 of 8 cells use 7 of 8 possible values, the 8th is FORCED.\n");
    printf("(We have 8 cells with all-different constraint, values 1-9)\n");
    printf("Constraint propagation proves this without search.\n\n");

    hsos_constraint_t cf;
    hsos_constraint_init(&cf, sys);

    /* Set custom domains to simulate 8 cells with values 1-8 only */
    /* Give 7 cells distinct values from 1-8, cell 7 will be forced */
    printf("Setting 7 givens: 1, 2, 3, 4, 5, 6, 7\n");
    printf("Possible values for cell 7: {1-8} (not 9)\n\n");

    /* First, restrict all domains to {1-8} by eliminating 9 */
    for (int i = 0; i < 8; i++) {
        cf.domains[i] = 0x00FF;  /* Values 1-8 only, no 9 */
        cf.sys->workers[i].memory[0] = 0xFF;
        cf.sys->workers[i].memory[1] = 0x00;
    }

    hsos_constraint_set_given(&cf, 0, 1);
    hsos_constraint_set_given(&cf, 1, 2);
    hsos_constraint_set_given(&cf, 2, 3);
    hsos_constraint_set_given(&cf, 3, 4);
    hsos_constraint_set_given(&cf, 4, 5);
    hsos_constraint_set_given(&cf, 5, 6);
    hsos_constraint_set_given(&cf, 6, 7);

    printf("Cell 7 domain before propagation: {");
    for (int v = 1; v <= 9; v++) {
        if (cf.domains[7] & (1 << (v-1))) printf(" %d", v);
    }
    printf(" }\n\n");

    printf("Propagating...\n");
    int elim = hsos_constraint_propagate(&cf);
    printf("  Eliminations: %d\n\n", elim);

    uint8_t val7 = hsos_constraint_get_value(&cf, 7);
    printf("Cell 7 after propagation: ");
    if (val7) {
        printf("%d (FORCED by constraints!)\n", val7);
    } else {
        printf("{");
        for (int v = 1; v <= 9; v++) {
            if (cf.domains[7] & (1 << (v-1))) printf(" %d", v);
        }
        printf(" }\n");
    }

    hsos_constraint_why(&cf, 7);

    printf("\nNo search. No backtracking. Just constraint propagation.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEMO 5: RECORDING & REPLAY
 * ═══════════════════════════════════════════════════════════════════════════ */

static void demo_replay(hsos_system_t *sys) {
    print_separator("DEMO 5: RECORDING & REPLAY");

    printf("HSOS provides deterministic recording and replay.\n");
    printf("Same input sequence → Same output, always.\n\n");

    /* Reset system */
    hsos_init(sys);
    hsos_boot(sys);

    printf("Recording a sort operation...\n");
    hsos_start_recording(sys);

    hsos_bubble_t bm;
    hsos_bubble_init(&bm, sys, TOPO_LINE);
    uint8_t input[8] = {99, 33, 77, 11, 55, 22, 88, 44};
    hsos_bubble_load(&bm, input);
    hsos_bubble_run(&bm);

    int msg_count = hsos_stop_recording(sys);
    printf("  Recorded %d messages\n\n", msg_count);

    uint8_t output1[8];
    hsos_bubble_read(&bm, output1);
    print_array("Original result:", output1);

    printf("\nReplaying...\n");
    hsos_replay(sys);

    uint8_t output2[8];
    hsos_bubble_read(&bm, output2);
    print_array("Replayed result:", output2);

    /* Verify identical */
    bool identical = (memcmp(output1, output2, 8) == 0);
    printf("\nResults: %s\n", identical ? "IDENTICAL ✓ (Deterministic)" :
                                          "DIFFERENT ✗ (Bug!)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         HOLLYWOOD SQUARES OS — TriX Implementation           ║\n");
    printf("║                                                              ║\n");
    printf("║     \"Structure is meaning.\"                                  ║\n");
    printf("║                                                              ║\n");
    printf("║     Port of anjaustin/hollywood-squares-os to frozen C.      ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    hsos_system_t sys;

    /* Run all demos */
    demo_boot(&sys);
    demo_bubble_machine(&sys);
    demo_constraint_field(&sys);
    demo_cascade(&sys);
    demo_replay(&sys);

    print_separator("SUMMARY");

    printf("Hollywood Squares OS demonstrates:\n\n");
    printf("  1. DETERMINISTIC MESSAGE PASSING\n");
    printf("     Fixed 16-byte frames, traceable delivery\n\n");
    printf("  2. BOUNDED LOCAL SEMANTICS\n");
    printf("     Each node: mailbox + dispatcher + scheduler\n\n");
    printf("  3. ENFORCED OBSERVABILITY\n");
    printf("     Recording, replay, trace, explainability\n\n");
    printf("  4. TOPOLOGY = ALGORITHM\n");
    printf("     Same handlers + different wiring = different behavior\n\n");

    printf("Result: Global convergence with inherited correctness.\n\n");

    printf("\"The wiring determines the behavior.\n");
    printf(" The messages carry the computation.\n");
    printf(" The trace tells the story.\"\n\n");

    printf("\"It's all in the reflexes.\"\n\n");

    return 0;
}
