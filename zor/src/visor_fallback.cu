/*
 * visor_fallback.cu — CPU Bridge Mode
 *
 * Bypasses CUDA-GL interop entirely.
 * GPU renders to device buffer, memcpy to host, glDrawPixels to screen.
 *
 * On AGX Thor with Unified Memory, this is nearly free.
 *
 * Compile: nvcc -O3 -arch=sm_89 visor_fallback.cu -o visor_fb -lglfw -lGL -lGLEW
 */

#include <stdio.h>
#include <stdlib.h>
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

/* ═══════════════════════════════════════════════════════════════════════════ */

__device__ inline int idx(int x, int y, int z) {
    return z * DIM * DIM + y * DIM + x;
}

__global__ void liquid_update(LiquidLattice in, LiquidLattice out, float injection) {
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

    /* Temporal smoothing: blend with previous state (rounds diamond → sphere) */
    signal = (signal * 0.8f) + (in.output[i] * 0.2f);

    /* Center injection */
    if (x == DIM/2 && y == DIM/2 && z == DIM/2) {
        signal += injection;
    }

    /* Soma */
    float local_state[NODE_COUNT];
    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) local_state[n] = in.neurons[n][i];

    float prediction = alpha_step(signal, local_state);

    /* Axon — Stable Liquid Physics */
    float delta = fabsf(signal - prediction);

    /* If Panic: Amplify but cap at 10.0 to prevent Infinity
       If Calm: Decay faster (0.90) to simulate viscous fluid */
    float energy = (delta > 0.112f) ? fminf(delta * 2.0f, 10.0f) : (signal * 0.90f);
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
                    sc = make_float4(0.1f, 0.2f, 0.5f, val * 0.5f);
                } else if (val < 1.0f) {
                    sc = make_float4(0.8f, 0.4f, 0.2f, val * 0.5f);
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

    /* Background gradient */
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
    printf("║  VISOR — CPU Bridge Mode                                     ║\n");
    printf("║  \"The MRI of an Artificial Mind\"                             ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* 1. Init GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "VISOR (CPU Bridge)", NULL, NULL);
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
    printf(">> Physics: Viscous fluid (0.90 decay, 10.0 cap)\n");
    printf(">> IGNITION\n\n");

    /* 3. Main loop */
    dim3 phys_blocks(DIM/8, DIM/8, DIM/8);
    dim3 phys_threads(8, 8, 8);
    dim3 rend_blocks(WIN_W/16, WIN_H/16);
    dim3 rend_threads(16, 16);

    int t = 0;
    int frame_count = 0;
    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {

        /* A. PHYSICS */
        for (int s = 0; s < STEPS_PER_FRAME; s++) {
            LiquidLattice* in  = (t % 2 == 0) ? &gridA : &gridB;
            LiquidLattice* out = (t % 2 == 0) ? &gridB : &gridA;

            float stim = (t % 100 < 5) ? 10.0f : 0.0f;
            if (stim > 0 && t < 20) {
                printf("Step %d: Injecting stimulus %.1f\n", t, stim);
            }
            liquid_update<<<phys_blocks, phys_threads>>>(*in, *out, stim);

            /* Check for kernel errors */
            cudaError_t err = cudaGetLastError();
            if (err != cudaSuccess && t < 10) {
                printf("CUDA Error in liquid_update: %s\n", cudaGetErrorString(err));
            }
            t++;
        }
        cudaDeviceSynchronize();

        /* B. RENDER */
        float theta = t * 0.005f;
        float3 cam_pos = make_float3(
            DIM/2.0f + sinf(theta) * 300.0f,
            DIM/2.0f + 50.0f,
            DIM/2.0f + cosf(theta) * 300.0f
        );
        float3 cam_target = make_float3(DIM/2.0f, DIM/2.0f, DIM/2.0f);

        LiquidLattice* current = (t % 2 == 0) ? &gridA : &gridB;
        raymarch_kernel<<<rend_blocks, rend_threads>>>(d_screen, current->output,
                                                        WIN_W, WIN_H, cam_pos, cam_target, 1.0f);
        cudaDeviceSynchronize();

        /* C. THE BRIDGE */
        cudaMemcpy(h_screen, d_screen, WIN_W * WIN_H * sizeof(uchar4), cudaMemcpyDeviceToHost);

        /* D. DISPLAY */
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawPixels(WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, h_screen);
        glfwSwapBuffers(window);
        glfwPollEvents();

        /* FPS */
        frame_count++;
        double now = glfwGetTime();
        if (now - last_time >= 1.0) {
            float center_val = 0.0f;
            int ci = (DIM/2)*DIM*DIM + (DIM/2)*DIM + (DIM/2);
            cudaMemcpy(&center_val, &current->output[ci], sizeof(float), cudaMemcpyDeviceToHost);

            char title[128];
            snprintf(title, sizeof(title), "VISOR | %d FPS | Step %d | Center: %.2f",
                     frame_count, t, center_val);
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
