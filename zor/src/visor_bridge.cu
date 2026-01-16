/*
 * visor_bridge.cu — The Cybernetic Organ
 *
 * Maps AGX Thor telemetry into the liquid cortex.
 * CPU → Left Hemisphere | GPU → Right Hemisphere
 *
 * "Watch the machine think."
 *
 * Compile: nvcc -O3 -arch=sm_100 visor_bridge.cu -o visor_bridge -lglfw -lGL -lGLEW
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cuda_runtime.h>
#include "../include/alpha_device.cuh"

#define DIM 256
#define VOXEL_COUNT (DIM * DIM * DIM)
#define WIN_W 1024
#define WIN_H 1024
#define STEPS_PER_FRAME 4

/* Injection site radius */
#define INJECT_RADIUS 12.0f

/* ═══════════════════════════════════════════════════════════════════════════
 * VITAL SIGNS — Reading the Body
 * ═══════════════════════════════════════════════════════════════════════════ */

struct VitalSigns {
    float cpu_load;   /* 0.0 - 1.0 */
    float gpu_load;   /* 0.0 - 1.0 */
    float mem_used;   /* 0.0 - 1.0 */
};

/* Read CPU usage from /proc/stat */
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

/* Read GPU usage from tegrastats or sysfs */
float read_gpu_load() {
    /* Try reading from sysfs (Tegra) */
    FILE* f = fopen("/sys/devices/gpu.0/load", "r");
    if (!f) {
        /* Fallback: try NVIDIA sysfs */
        f = fopen("/sys/class/drm/card0/device/gpu_busy_percent", "r");
    }
    if (!f) {
        /* Mock with sine wave if no GPU stats available */
        static float t = 0;
        t += 0.02f;
        return fabsf(sinf(t * 3.0f)) * 0.8f;
    }

    int load = 0;
    if (fscanf(f, "%d", &load) != 1) load = 0;
    fclose(f);
    return (float)load / 100.0f;
}

/* Read memory usage from /proc/meminfo */
float read_mem_used() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0.5f;

    long mem_total = 0, mem_avail = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %ld", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %ld", &mem_avail);
        }
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

/* Vector ops */
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

/* Distance helper */
__device__ inline float dist3(int x, int y, int z, int tx, int ty, int tz) {
    float dx = (float)(x - tx);
    float dy = (float)(y - ty);
    float dz = (float)(z - tz);
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

/* ═══════════════════════════════════════════════════════════════════════════ */

__device__ inline int idx(int x, int y, int z) {
    return z * DIM * DIM + y * DIM + x;
}

__global__ void liquid_update(LiquidLattice in, LiquidLattice out,
                               float cpu_val, float gpu_val, float viscosity) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;
    if (x >= DIM || y >= DIM || z >= DIM) return;
    int i = idx(x, y, z);

    /* Dendrites: 6-neighbor signal */
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

    /* Temporal smoothing (rounds diamond → sphere) */
    signal = (signal * 0.8f) + (in.output[i] * 0.2f);

    /* ═══════════════════════════════════════════════════════════════════════
     * INJECTION SITES — The Sensory Organs
     * ═══════════════════════════════════════════════════════════════════════ */

    /* CPU Injection: Left Hemisphere [64, 128, 128] */
    float cpu_dist = dist3(x, y, z, 64, 128, 128);
    if (cpu_dist < INJECT_RADIUS) {
        float falloff = 1.0f - (cpu_dist / INJECT_RADIUS);
        signal += cpu_val * 8.0f * falloff;
    }

    /* GPU Injection: Right Hemisphere [192, 128, 128] */
    float gpu_dist = dist3(x, y, z, 192, 128, 128);
    if (gpu_dist < INJECT_RADIUS) {
        float falloff = 1.0f - (gpu_dist / INJECT_RADIUS);
        signal += gpu_val * 8.0f * falloff;
    }

    /* Soma: liquid physics */
    float local_state[NODE_COUNT];
    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) local_state[n] = in.neurons[n][i];

    float prediction = alpha_step(signal, local_state);

    /* Axon — Viscous Liquid Physics */
    float delta = fabsf(signal - prediction);
    float decay = 0.85f + (viscosity * 0.10f);  /* Memory pressure increases viscosity */
    float energy = (delta > 0.112f) ? fminf(delta * 2.0f, 10.0f) : (signal * decay);
    out.output[i] = energy;

    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) out.neurons[n][i] = local_state[n];
}

/* ═══════════════════════════════════════════════════════════════════════════ */

__global__ void raymarch_kernel(uchar4* screen, float* volume, int width, int height,
                                 float3 cam_pos, float3 cam_target, float zoom) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int pixel_idx = y * width + x;

    /* Ray setup */
    float u = (x / (float)width) * 2.0f - 1.0f;
    float v = (y / (float)height) * 2.0f - 1.0f;

    float3 forward = normalize3(cam_target - cam_pos);
    float3 right = normalize3(make_float3(-forward.z, 0.0f, forward.x));
    float3 up = make_float3(0.0f, 1.0f, 0.0f);

    float fov = 1.5f / zoom;
    float3 ray_dir = normalize3(forward + right * (u * fov) + up * (v * fov));

    /* March */
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
                    /* Calm: Blue mist */
                    sc = make_float4(0.1f, 0.2f, 0.5f, val * 0.5f);
                } else if (val < 1.0f) {
                    /* Active: Orange/purple (CPU=orange, GPU=purple based on position) */
                    float lr = (p.x / (float)DIM);  /* 0=left, 1=right */
                    sc = make_float4(
                        0.9f * (1.0f - lr) + 0.5f * lr,   /* Orange left, purple right */
                        0.4f * (1.0f - lr) + 0.2f * lr,
                        0.2f * (1.0f - lr) + 0.8f * lr,
                        val * 0.5f
                    );
                } else {
                    /* Panic: White-hot plasma */
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

    /* Background: Dark gradient */
    float bg = 1.0f - color.w;
    color.x += 0.02f * bg;
    color.y += 0.03f * bg;
    color.z += 0.08f * bg;

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

/* ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  VISOR BRIDGE — The Cybernetic Organ                         ║\n");
    printf("║  \"Watch the machine think.\"                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* 1. Init GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "VISOR BRIDGE", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glewInit();

    printf(">> OpenGL: %s\n", glGetString(GL_VERSION));
    printf(">> Renderer: %s\n\n", glGetString(GL_RENDERER));

    /* 2. Allocate buffers */
    uchar4* h_screen = (uchar4*)malloc(WIN_W * WIN_H * sizeof(uchar4));
    uchar4* d_screen;
    cudaMalloc(&d_screen, WIN_W * WIN_H * sizeof(uchar4));

    LiquidLattice gridA, gridB;
    alloc_lattice(&gridA);
    alloc_lattice(&gridB);
    zero_lattice(&gridA);
    zero_lattice(&gridB);

    printf(">> Grid: %dx%dx%d = %d voxels\n", DIM, DIM, DIM, VOXEL_COUNT);
    printf(">> Injection Sites:\n");
    printf("   CPU → Left Hemisphere  [64, 128, 128]  (Orange)\n");
    printf("   GPU → Right Hemisphere [192, 128, 128] (Purple)\n");
    printf(">> IGNITION\n\n");

    /* 3. Main loop */
    dim3 phys_blocks(DIM/8, DIM/8, DIM/8);
    dim3 phys_threads(8, 8, 8);
    dim3 rend_blocks(WIN_W/16, WIN_H/16);
    dim3 rend_threads(16, 16);

    int t = 0;
    int frame_count = 0;
    double last_time = glfwGetTime();

    /* Prime the CPU load reader */
    read_cpu_load();

    while (!glfwWindowShouldClose(window)) {

        /* Read the body */
        VitalSigns vitals = read_vital_signs();

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
            WIN_W, WIN_H, cam_pos, cam_target, 1.0f
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
                     "VISOR | %d FPS | CPU: %.0f%% | GPU: %.0f%% | MEM: %.0f%%",
                     frame_count,
                     vitals.cpu_load * 100.0f,
                     vitals.gpu_load * 100.0f,
                     vitals.mem_used * 100.0f);
            glfwSetWindowTitle(window, title);
            printf("%s\n", title);
            frame_count = 0;
            last_time = now;
        }
    }

    /* Cleanup */
    free(h_screen);
    cudaFree(d_screen);
    glfwDestroyWindow(window);
    glfwTerminate();

    printf("\n\"It's all in the reflexes.\"\n");
    return 0;
}
