/*
 * visor_reflex.cu — The Cybernetic Immune System
 *
 * A cortex that doesn't just feel pain — it fights back.
 *
 * When total system stress exceeds threshold:
 *   1. Detect the collision (white-hot core)
 *   2. Identify the aggressor (top CPU consumer)
 *   3. Neutralize the threat (kill -STOP)
 *
 * "The machine that protects itself."
 *
 * Compile: nvcc -O3 -arch=sm_100 visor_reflex.cu -o visor_reflex -lglfw -lGL -lGLEW
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cuda_runtime.h>
#include "../include/alpha_device.cuh"

#define DIM 256
#define VOXEL_COUNT (DIM * DIM * DIM)
#define WIN_W 1024
#define WIN_H 1024
#define STEPS_PER_FRAME 4

#define INJECT_RADIUS 12.0f

/* ═══════════════════════════════════════════════════════════════════════════
 * REFLEX CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define PANIC_THRESHOLD 100000.0f   /* Total energy threshold for reflex */
#define COOLDOWN_FRAMES 300         /* Frames to wait after reflex fires */
#define REFLEX_ENABLED 1            /* Set to 0 to disable kill switch */

/* Global device variable for energy accumulation */
__device__ float d_total_energy;

/* ═══════════════════════════════════════════════════════════════════════════
 * VITAL SIGNS
 * ═══════════════════════════════════════════════════════════════════════════ */

struct VitalSigns {
    float cpu_load;
    float gpu_load;
    float mem_used;
};

float read_cpu_load() {
    static long prev_idle = 0, prev_total = 0;
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return 0.0f;

    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0.0f; }
    fclose(f);

    long user, nice, sys, idle, iowait, irq, softirq;
    sscanf(buf, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &sys, &idle, &iowait, &irq, &softirq);

    long total = user + nice + sys + idle + iowait + irq + softirq;
    long idle_time = idle + iowait;
    long diff_idle = idle_time - prev_idle;
    long diff_total = total - prev_total;
    prev_idle = idle_time;
    prev_total = total;

    if (diff_total == 0) return 0.0f;
    return 1.0f - (float)diff_idle / (float)diff_total;
}

float read_gpu_load() {
    FILE* f = fopen("/sys/devices/gpu.0/load", "r");
    if (!f) f = fopen("/sys/class/drm/card0/device/gpu_busy_percent", "r");
    if (!f) {
        static float t = 0;
        t += 0.02f;
        return fabsf(sinf(t * 3.0f)) * 0.8f;
    }
    int load = 0;
    if (fscanf(f, "%d", &load) != 1) load = 0;
    fclose(f);
    return (float)load / 100.0f;
}

float read_mem_used() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0.5f;
    long mem_total = 0, mem_avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line, "MemTotal: %ld", &mem_total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line, "MemAvailable: %ld", &mem_avail);
    }
    fclose(f);
    if (mem_total == 0) return 0.5f;
    return 1.0f - (float)mem_avail / (float)mem_total;
}

VitalSigns read_vital_signs() {
    return (VitalSigns){
        .cpu_load = read_cpu_load(),
        .gpu_load = read_gpu_load(),
        .mem_used = read_mem_used()
    };
}

/* ═══════════════════════════════════════════════════════════════════════════ */

struct LiquidLattice {
    float* neurons[NODE_COUNT];
    float* output;
};

__device__ __host__ inline float3 operator+(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}
__device__ __host__ inline float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}
__device__ __host__ inline float3 operator*(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}
__device__ __host__ inline float dot3(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
__device__ __host__ inline float3 normalize3(float3 v) {
    float len = sqrtf(dot3(v, v));
    return (len > 0.0001f) ? make_float3(v.x/len, v.y/len, v.z/len) : v;
}

__device__ inline float dist3(int x, int y, int z, int tx, int ty, int tz) {
    float dx = (float)(x - tx);
    float dy = (float)(y - ty);
    float dz = (float)(z - tz);
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

__device__ inline int idx(int x, int y, int z) {
    return z * DIM * DIM + y * DIM + x;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RESET ENERGY ACCUMULATOR
 * ═══════════════════════════════════════════════════════════════════════════ */

__global__ void reset_energy() {
    d_total_energy = 0.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LIQUID UPDATE WITH ENERGY TRACKING
 * ═══════════════════════════════════════════════════════════════════════════ */

__global__ void liquid_update(LiquidLattice in, LiquidLattice out,
                               float cpu_val, float gpu_val, float viscosity) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;
    if (x >= DIM || y >= DIM || z >= DIM) return;
    int i = idx(x, y, z);

    /* Dendrites */
    float signal = 0.0f;
    int xm = (x > 0)       ? i - 1           : i;
    int xp = (x < DIM - 1) ? i + 1           : i;
    int ym = (y > 0)       ? i - DIM         : i;
    int yp = (y < DIM - 1) ? i + DIM         : i;
    int zm = (z > 0)       ? i - (DIM * DIM) : i;
    int zp = (z < DIM - 1) ? i + (DIM * DIM) : i;

    signal += in.output[xm] + in.output[xp];
    signal += in.output[ym] + in.output[yp];
    signal += in.output[zm] + in.output[zp];
    signal /= 6.0f;

    /* Temporal smoothing */
    signal = (signal * 0.8f) + (in.output[i] * 0.2f);

    /* Injection Sites */
    float cpu_dist = dist3(x, y, z, 64, 128, 128);
    if (cpu_dist < INJECT_RADIUS) {
        float falloff = 1.0f - (cpu_dist / INJECT_RADIUS);
        signal += cpu_val * 8.0f * falloff;
    }

    float gpu_dist = dist3(x, y, z, 192, 128, 128);
    if (gpu_dist < INJECT_RADIUS) {
        float falloff = 1.0f - (gpu_dist / INJECT_RADIUS);
        signal += gpu_val * 8.0f * falloff;
    }

    /* Soma */
    float local_state[NODE_COUNT];
    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) local_state[n] = in.neurons[n][i];

    float prediction = alpha_step(signal, local_state);

    /* Axon */
    float delta = fabsf(signal - prediction);
    float decay = 0.85f + (viscosity * 0.10f);
    float energy = (delta > 0.112f) ? fminf(delta * 2.0f, 10.0f) : (signal * decay);

    /* Track panic energy (only count high-energy voxels) */
    if (energy > 1.0f) {
        atomicAdd(&d_total_energy, energy);
    }

    out.output[i] = energy;

    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) out.neurons[n][i] = local_state[n];
}

/* ═══════════════════════════════════════════════════════════════════════════ */

__global__ void raymarch_kernel(uchar4* screen, float* volume, int width, int height,
                                 float3 cam_pos, float3 cam_target, float zoom,
                                 int reflex_active) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int pixel_idx = y * width + x;

    float u = (x / (float)width) * 2.0f - 1.0f;
    float v = (y / (float)height) * 2.0f - 1.0f;

    float3 forward = normalize3(cam_target - cam_pos);
    float3 right = normalize3(make_float3(-forward.z, 0.0f, forward.x));
    float3 up = make_float3(0.0f, 1.0f, 0.0f);

    float fov = 1.5f / zoom;
    float3 ray_dir = normalize3(forward + right * (u * fov) + up * (v * fov));

    float4 color = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    float t = 0.0f;

    for (int i = 0; i < 300; i++) {
        float3 p = cam_pos + ray_dir * t;

        if (p.x >= 0 && p.x < DIM && p.y >= 0 && p.y < DIM && p.z >= 0 && p.z < DIM) {
            int vi = (int)p.z * DIM * DIM + (int)p.y * DIM + (int)p.x;
            float val = volume[vi];

            if (val > 0.01f) {
                float4 sc;
                if (val < 0.1f) {
                    sc = make_float4(0.1f, 0.2f, 0.5f, val * 0.5f);
                } else if (val < 1.0f) {
                    float lr = (p.x / (float)DIM);
                    sc = make_float4(
                        0.9f * (1.0f - lr) + 0.5f * lr,
                        0.4f * (1.0f - lr) + 0.2f * lr,
                        0.2f * (1.0f - lr) + 0.8f * lr,
                        val * 0.5f
                    );
                } else {
                    sc = make_float4(1.0f, 1.0f, 1.0f, 0.9f);
                }

                float a = sc.w * (1.0f - color.w);
                color.x += sc.x * a;
                color.y += sc.y * a;
                color.z += sc.z * a;
                color.w += a;

                if (color.w > 0.95f) break;
            }
        }
        t += 1.0f;
    }

    /* Background - red tint when reflex is active */
    float bg = 1.0f - color.w;
    if (reflex_active) {
        color.x += 0.15f * bg;  /* Red alert background */
        color.y += 0.02f * bg;
        color.z += 0.02f * bg;
    } else {
        color.x += 0.02f * bg;
        color.y += 0.03f * bg;
        color.z += 0.08f * bg;
    }

    screen[pixel_idx] = make_uchar4(
        (unsigned char)(fminf(color.x, 1.0f) * 255),
        (unsigned char)(fminf(color.y, 1.0f) * 255),
        (unsigned char)(fminf(color.z, 1.0f) * 255),
        255
    );
}

/* ═══════════════════════════════════════════════════════════════════════════ */

void alloc_lattice(LiquidLattice* l) {
    cudaMalloc(&l->output, VOXEL_COUNT * sizeof(float));
    for (int i = 0; i < NODE_COUNT; i++)
        cudaMalloc(&l->neurons[i], VOXEL_COUNT * sizeof(float));
}

void zero_lattice(LiquidLattice* l) {
    cudaMemset(l->output, 0, VOXEL_COUNT * sizeof(float));
    for (int i = 0; i < NODE_COUNT; i++)
        cudaMemset(l->neurons[i], 0, VOXEL_COUNT * sizeof(float));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * THE REFLEX — Kill the aggressor
 * ═══════════════════════════════════════════════════════════════════════════ */

void execute_reflex() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ⚠️  CORTEX REFLEX TRIGGERED                                  ║\n");
    printf("║  CRITICAL STRESS DETECTED — INITIATING COOLING PROTOCOL      ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    /* Find top CPU consumer (excluding self) and pause it */
    printf(">> Identifying threat...\n");

    FILE* fp = popen("ps -eo pid,pcpu,comm --sort=-pcpu | grep -v visor | grep -v PID | head -1", "r");
    if (fp) {
        int pid;
        float cpu;
        char comm[256];
        if (fscanf(fp, "%d %f %255s", &pid, &cpu, comm) == 3) {
            printf(">> Target acquired: PID %d (%s) using %.1f%% CPU\n", pid, comm, cpu);

            if (cpu > 20.0f) {  /* Only kill if actually consuming significant CPU */
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "kill -STOP %d 2>/dev/null", pid);
                int ret = system(cmd);
                if (ret == 0) {
                    printf(">> THREAT NEUTRALIZED: Process %d frozen\n", pid);
                    printf(">> To resume: kill -CONT %d\n", pid);
                } else {
                    printf(">> Could not freeze process (permission denied?)\n");
                }
            } else {
                printf(">> Threat level too low (%.1f%%), standing down\n", cpu);
            }
        }
        pclose(fp);
    }

    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  VISOR REFLEX — The Cybernetic Immune System                 ║\n");
    printf("║  \"The machine that protects itself.\"                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "VISOR REFLEX", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glewInit();

    printf(">> OpenGL: %s\n", glGetString(GL_VERSION));
    printf(">> Renderer: %s\n\n", glGetString(GL_RENDERER));

    uchar4* h_screen = (uchar4*)malloc(WIN_W * WIN_H * sizeof(uchar4));
    uchar4* d_screen;
    cudaMalloc(&d_screen, WIN_W * WIN_H * sizeof(uchar4));

    LiquidLattice gridA, gridB;
    alloc_lattice(&gridA);
    alloc_lattice(&gridB);
    zero_lattice(&gridA);
    zero_lattice(&gridB);

    printf(">> Grid: %dx%dx%d = %d voxels\n", DIM, DIM, DIM, VOXEL_COUNT);
    printf(">> Panic Threshold: %.0f\n", PANIC_THRESHOLD);
    printf(">> Reflex: %s\n", REFLEX_ENABLED ? "ARMED" : "DISABLED");
    printf(">> IGNITION\n\n");

    dim3 phys_blocks(DIM/8, DIM/8, DIM/8);
    dim3 phys_threads(8, 8, 8);
    dim3 rend_blocks(WIN_W/16, WIN_H/16);
    dim3 rend_threads(16, 16);

    int t = 0;
    int frame_count = 0;
    double last_time = glfwGetTime();
    int reflex_cooldown = 0;
    int reflex_active = 0;

    read_cpu_load();  /* Prime */

    /* Get pointer to device energy variable */
    float* d_energy_ptr;
    cudaGetSymbolAddress((void**)&d_energy_ptr, d_total_energy);

    while (!glfwWindowShouldClose(window)) {

        VitalSigns vitals = read_vital_signs();

        /* Reset energy accumulator */
        reset_energy<<<1, 1>>>();
        cudaDeviceSynchronize();

        /* A. PHYSICS */
        for (int s = 0; s < STEPS_PER_FRAME; s++) {
            LiquidLattice* in  = (t % 2 == 0) ? &gridA : &gridB;
            LiquidLattice* out = (t % 2 == 0) ? &gridB : &gridA;

            liquid_update<<<phys_blocks, phys_threads>>>(
                *in, *out,
                vitals.cpu_load,
                vitals.gpu_load,
                vitals.mem_used
            );
            t++;
        }
        cudaDeviceSynchronize();

        /* Read total panic energy */
        float total_energy = 0.0f;
        cudaMemcpy(&total_energy, d_energy_ptr, sizeof(float), cudaMemcpyDeviceToHost);

        /* Check for reflex trigger */
        if (reflex_cooldown > 0) {
            reflex_cooldown--;
            reflex_active = (reflex_cooldown > COOLDOWN_FRAMES - 60);  /* Flash for 1 sec */
        } else if (REFLEX_ENABLED && total_energy > PANIC_THRESHOLD) {
            execute_reflex();
            reflex_cooldown = COOLDOWN_FRAMES;
            reflex_active = 1;
        } else {
            reflex_active = 0;
        }

        /* B. RENDER */
        float theta = t * 0.003f;
        float3 cam_pos = make_float3(
            DIM/2.0f + sinf(theta) * 350.0f,
            DIM/2.0f + 30.0f,
            DIM/2.0f + cosf(theta) * 350.0f
        );
        float3 cam_target = make_float3(DIM/2.0f, DIM/2.0f, DIM/2.0f);

        LiquidLattice* current = (t % 2 == 0) ? &gridA : &gridB;
        raymarch_kernel<<<rend_blocks, rend_threads>>>(
            d_screen, current->output,
            WIN_W, WIN_H, cam_pos, cam_target, 1.0f, reflex_active
        );
        cudaDeviceSynchronize();

        /* C. THE BRIDGE */
        cudaMemcpy(h_screen, d_screen, WIN_W * WIN_H * sizeof(uchar4), cudaMemcpyDeviceToHost);

        /* D. DISPLAY */
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawPixels(WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, h_screen);
        glfwSwapBuffers(window);
        glfwPollEvents();

        /* FPS + Telemetry */
        frame_count++;
        double now = glfwGetTime();
        if (now - last_time >= 1.0) {
            char title[256];
            snprintf(title, sizeof(title),
                     "VISOR | %d FPS | CPU: %.0f%% | GPU: %.0f%% | Energy: %.0f %s",
                     frame_count,
                     vitals.cpu_load * 100.0f,
                     vitals.gpu_load * 100.0f,
                     total_energy,
                     reflex_active ? "⚠️ REFLEX" : "");
            glfwSetWindowTitle(window, title);
            printf("CPU: %5.1f%% | GPU: %5.1f%% | Energy: %10.0f %s\n",
                   vitals.cpu_load * 100.0f,
                   vitals.gpu_load * 100.0f,
                   total_energy,
                   reflex_active ? "<<< REFLEX ACTIVE >>>" : "");
            frame_count = 0;
            last_time = now;
        }
    }

    free(h_screen);
    cudaFree(d_screen);
    glfwDestroyWindow(window);
    glfwTerminate();

    printf("\n\"It's all in the reflexes.\"\n");
    return 0;
}
