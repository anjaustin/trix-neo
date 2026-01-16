/*
 * generate_dirty_data.c — Create realistic test data for The Forge
 *
 * Simulates various real-world sensor patterns with noise, drift, and jitter.
 *
 * Usage:
 *   ./generate_dirty_data motor > motor_vibration.csv
 *   ./generate_dirty_data cpu   > cpu_load.csv
 *   ./generate_dirty_data voice > voice_pitch.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define SAMPLES 4096

/* Simple RNG for reproducibility */
static unsigned long rng_state = 12345;

float randf(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (float)((rng_state >> 16) & 0x7FFF) / 32768.0f;
}

float randf_range(float a, float b) {
    return a + randf() * (b - a);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MOTOR VIBRATION PATTERN
 *
 * Simulates accelerometer on a rotating motor:
 * - Primary rotation frequency
 * - Bearing harmonics (2x, 3x, 5x)
 * - Random mechanical noise
 * - Occasional resonance peaks
 * - Slow drift from temperature changes
 * ═══════════════════════════════════════════════════════════════════════════ */

void generate_motor_data(void) {
    fprintf(stderr, "Generating motor vibration data (%d samples)...\n", SAMPLES);

    float drift = 0.0f;
    float drift_velocity = 0.0f;

    for (int t = 0; t < SAMPLES; t++) {
        /* Primary rotation (e.g., 60 Hz motor) */
        float phase = t * 0.12f;
        float primary = sinf(phase) * 2.5f;

        /* Bearing harmonics */
        float h2 = sinf(phase * 2.0f) * 0.8f;
        float h3 = sinf(phase * 3.0f + 0.5f) * 0.4f;
        float h5 = sinf(phase * 5.0f + 1.2f) * 0.15f;

        /* Mechanical noise (wideband) */
        float noise = randf_range(-0.3f, 0.3f);

        /* Slow drift (simulates temperature/load changes) */
        drift_velocity += randf_range(-0.001f, 0.001f);
        drift_velocity *= 0.99f;  /* Damping */
        drift += drift_velocity;

        /* Occasional resonance spike (5% chance) */
        float spike = 0.0f;
        if (randf() < 0.05f) {
            spike = randf_range(0.5f, 1.5f);
        }

        /* Micro-jitter (quantization noise from ADC) */
        float jitter = randf_range(-0.05f, 0.05f);

        /* Combine all components */
        float value = primary + h2 + h3 + h5 + noise + drift + spike + jitter;

        /* Scale to realistic mV range */
        value = value * 100.0f + 500.0f;  /* 0-1000 mV range */

        printf("%.4f\n", value);
    }

    fprintf(stderr, "Done.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU LOAD PATTERN
 *
 * Simulates server CPU load:
 * - Periodic load from cron jobs
 * - Random task bursts
 * - Slow overall trend (business hours)
 * - Occasional spikes (GC, cache clear)
 * ═══════════════════════════════════════════════════════════════════════════ */

void generate_cpu_data(void) {
    fprintf(stderr, "Generating CPU load data (%d samples)...\n", SAMPLES);

    float baseline = 20.0f;  /* Base load */
    float trend = 0.0f;

    for (int t = 0; t < SAMPLES; t++) {
        /* Slow trend (day/night cycle, compressed) */
        float hour_phase = sinf(t * 0.003f) * 0.5f + 0.5f;
        trend = hour_phase * 30.0f;  /* +0-30% during "day" */

        /* Periodic cron jobs (every ~100 samples) */
        float cron = 0.0f;
        if ((t % 100) < 10) {
            cron = 15.0f;  /* 10 samples of +15% load */
        }

        /* Random task activity */
        float random_load = randf_range(-5.0f, 10.0f);

        /* Occasional spike (3% chance) */
        float spike = 0.0f;
        if (randf() < 0.03f) {
            spike = randf_range(20.0f, 50.0f);
        }

        /* Noise */
        float noise = randf_range(-2.0f, 2.0f);

        /* Combine */
        float value = baseline + trend + cron + random_load + spike + noise;

        /* Clamp to 0-100% */
        if (value < 0.0f) value = 0.0f;
        if (value > 100.0f) value = 100.0f;

        printf("%.2f\n", value);
    }

    fprintf(stderr, "Done.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VOICE PITCH PATTERN
 *
 * Simulates extracted fundamental frequency (F0) from voice:
 * - Base pitch with vibrato
 * - Pitch slides
 * - Voiced/unvoiced gaps
 * - Microphone noise
 * ═══════════════════════════════════════════════════════════════════════════ */

void generate_voice_data(void) {
    fprintf(stderr, "Generating voice pitch data (%d samples)...\n", SAMPLES);

    float base_pitch = 150.0f;  /* Hz - typical male speaking voice */
    float current_pitch = base_pitch;
    int voiced = 1;
    int voiced_duration = 50;

    for (int t = 0; t < SAMPLES; t++) {
        /* Switch between voiced/unvoiced */
        voiced_duration--;
        if (voiced_duration <= 0) {
            voiced = !voiced;
            voiced_duration = (int)randf_range(30.0f, 100.0f);
            if (voiced) {
                /* New pitch target */
                base_pitch = randf_range(100.0f, 200.0f);
            }
        }

        if (voiced) {
            /* Smooth transition to target pitch */
            current_pitch += (base_pitch - current_pitch) * 0.05f;

            /* Vibrato (5-7 Hz oscillation) */
            float vibrato = sinf(t * 0.4f) * 5.0f;

            /* Microintonation */
            float micro = randf_range(-2.0f, 2.0f);

            /* Output */
            float value = current_pitch + vibrato + micro;
            printf("%.2f\n", value);
        } else {
            /* Unvoiced - output noise near zero */
            float value = randf_range(-5.0f, 5.0f);
            printf("%.2f\n", value);
        }
    }

    fprintf(stderr, "Done.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv) {
    rng_state = (unsigned long)time(NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <motor|cpu|voice>\n", argv[0]);
        fprintf(stderr, "  Output goes to stdout (pipe to file)\n");
        return 1;
    }

    if (strcmp(argv[1], "motor") == 0) {
        generate_motor_data();
    } else if (strcmp(argv[1], "cpu") == 0) {
        generate_cpu_data();
    } else if (strcmp(argv[1], "voice") == 0) {
        generate_voice_data();
    } else {
        fprintf(stderr, "Unknown pattern: %s\n", argv[1]);
        fprintf(stderr, "Try: motor, cpu, or voice\n");
        return 1;
    }

    return 0;
}
