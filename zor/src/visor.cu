/*
 * visor.cu — EntroMorphic Volumetric Raymarcher
 *
 * "The MRI of an Artificial Mind"
 *
 * This visualizer renders the 16M voxel cortex using volumetric raymarching.
 * No polygons - we shoot rays through the density field and accumulate light.
 *
 * Architecture:
 *   - CUDA-OpenGL interop (zero-copy to screen)
 *   - Front-to-back alpha blending
 *   - Transfer function: Blue (calm) → Orange (active) → White (panic)
 *
 * Prerequisites:
 *   - GLFW3, GLEW, OpenGL
 *   - CUDA with GL interop support
 *
 * Compile:
 *   nvcc -O3 -arch=sm_89 visor.cu -o visor -lglfw -lGL -lGLEW -lX11
 *
 * Controls:
 *   ESC     - Quit
 *   SPACE   - Manual stimulus injection
 *   W/S     - Zoom in/out
 *   A/D     - Rotate camera
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include "../include/alpha_device.cuh"

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define DIM 256                           /* Voxel grid dimension */
#define VOXEL_COUNT (DIM * DIM * DIM)     /* 16,777,216 voxels */

#define WIN_W 1024                        /* Window width */
#define WIN_H 1024                        /* Window height */

#define STEPS_PER_FRAME 4                 /* Physics steps per render frame */
                                          /* 4 steps @ 60fps = 240Hz physics */

#define RAY_STEPS 300                     /* Max raymarching steps */
#define RAY_STEP_SIZE 1.0f                /* Step size (quality vs speed) */

/* Transfer function thresholds */
#define THRESH_CALM   0.01f               /* Below: transparent */
#define THRESH_ACTIVE 0.1f                /* Below: blue mist */
#define THRESH_PANIC  1.0f                /* Below: orange/purple */
                                          /* Above: white plasma */

/* ═══════════════════════════════════════════════════════════════════════════
 * VECTOR HELPERS (operators not built-in to CUDA)
 * ═══════════════════════════════════════════════════════════════════════════ */

__device__ __host__ inline float3 operator+(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __host__ inline float3 operator*(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __host__ inline float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ __host__ inline float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __host__ inline float3 normalize(float3 v) {
    float len = sqrtf(dot(v, v));
    if (len > 0.0001f) {
        return make_float3(v.x / len, v.y / len, v.z / len);
    }
    return v;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * STRUCTURE OF ARRAYS (Same as cortex.cu)
 * ═══════════════════════════════════════════════════════════════════════════ */

struct LiquidLattice {
    float* neurons[NODE_COUNT];
    float* output;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PHYSICS KERNEL (Copied from cortex.cu for self-containment)
 * ═══════════════════════════════════════════════════════════════════════════ */

__device__ __forceinline__ int idx(int x, int y, int z) {
    return z * DIM * DIM + y * DIM + x;
}

__global__ void liquid_update(LiquidLattice in, LiquidLattice out, float injection) {
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

    /* Center injection */
    if (x == DIM/2 && y == DIM/2 && z == DIM/2) {
        signal += injection;
    }

    /* Soma: liquid physics */
    float local_state[NODE_COUNT];
    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) {
        local_state[n] = in.neurons[n][i];
    }

    float prediction = alpha_step(signal, local_state);

    /* Axon: Zit detection */
    float delta = fabsf(signal - prediction);
    if (delta > 0.1f) {
        out.output[i] = delta * 5.0f;
    } else {
        out.output[i] = signal * 0.95f;
    }

    #pragma unroll
    for (int n = 0; n < NODE_COUNT; n++) {
        out.neurons[n][i] = local_state[n];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * THE RAYMARCHER KERNEL
 *
 * For each pixel:
 *   1. Cast a ray from camera through pixel
 *   2. March through the volume, sampling density
 *   3. Accumulate color using transfer function
 *   4. Write pixel to screen buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

__global__ void raymarch_kernel(
    uchar4* screen,
    float* volume,
    int width,
    int height,
    float3 cam_pos,
    float3 cam_target,
    float cam_zoom
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;
    int pixel_idx = y * width + x;

    /* ─────────────────────────────────────────────────────────────────────
     * 1. RAY SETUP
     *
     * Simple perspective camera with orbital rotation
     * ───────────────────────────────────────────────────────────────────── */
    float u = (x / (float)width) * 2.0f - 1.0f;
    float v = (y / (float)height) * 2.0f - 1.0f;

    /* Camera basis vectors */
    float3 forward = normalize(cam_target - cam_pos);
    float3 right = normalize(make_float3(-forward.z, 0.0f, forward.x));
    float3 up = make_float3(0.0f, 1.0f, 0.0f);

    /* Ray direction with FOV */
    float fov = 1.5f / cam_zoom;
    float3 ray_dir = normalize(forward + right * (u * fov) + up * (v * fov));
    float3 ray_origin = cam_pos;

    /* ─────────────────────────────────────────────────────────────────────
     * 2. THE MARCH
     *
     * Step through the volume, sampling at each point
     * ───────────────────────────────────────────────────────────────────── */
    float4 color = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    float t = 0.0f;

    /* Find entry point into volume */
    float3 box_min = make_float3(0.0f, 0.0f, 0.0f);
    float3 box_max = make_float3((float)DIM, (float)DIM, (float)DIM);

    for (int i = 0; i < RAY_STEPS; i++) {
        float3 p = ray_origin + ray_dir * t;

        /* Bounds check */
        if (p.x < 0 || p.x >= DIM || p.y < 0 || p.y >= DIM || p.z < 0 || p.z >= DIM) {
            t += RAY_STEP_SIZE;
            continue;
        }

        /* ─────────────────────────────────────────────────────────────────
         * 3. SAMPLE THE BRAIN
         *
         * Nearest-neighbor sampling for speed
         * ───────────────────────────────────────────────────────────────── */
        int voxel_idx = (int)p.z * DIM * DIM + (int)p.y * DIM + (int)p.x;
        float val = volume[voxel_idx];

        /* ─────────────────────────────────────────────────────────────────
         * 4. TRANSFER FUNCTION: "The Palette of Pain"
         *
         * Maps voltage to color:
         *   < 0.01  : Transparent (nothing)
         *   < 0.1   : Blue mist (calm)
         *   < 1.0   : Orange/Purple (active)
         *   >= 1.0  : White plasma (panic/shock)
         * ───────────────────────────────────────────────────────────────── */
        if (val > THRESH_CALM) {
            float4 sample_col;

            if (val < THRESH_ACTIVE) {
                /* Calm: Faint blue mist */
                float intensity = (val - THRESH_CALM) / (THRESH_ACTIVE - THRESH_CALM);
                sample_col = make_float4(
                    0.1f * intensity,
                    0.2f * intensity,
                    0.5f * intensity,
                    val * 0.1f
                );
            } else if (val < THRESH_PANIC) {
                /* Active: Orange/purple gradient */
                float intensity = (val - THRESH_ACTIVE) / (THRESH_PANIC - THRESH_ACTIVE);
                sample_col = make_float4(
                    0.8f * intensity + 0.3f,
                    0.4f * intensity,
                    0.6f * (1.0f - intensity) + 0.1f,
                    val * 0.3f
                );
            } else {
                /* Panic: White-hot plasma */
                float intensity = fminf(val / 5.0f, 1.0f);
                sample_col = make_float4(
                    1.0f,
                    0.9f + 0.1f * intensity,
                    0.7f + 0.3f * intensity,
                    0.8f
                );
            }

            /* Front-to-back alpha compositing */
            float alpha = sample_col.w * 0.5f;
            float one_minus_alpha = 1.0f - color.w;

            color.x += sample_col.x * alpha * one_minus_alpha;
            color.y += sample_col.y * alpha * one_minus_alpha;
            color.z += sample_col.z * alpha * one_minus_alpha;
            color.w += alpha * one_minus_alpha;

            /* Early termination if opaque */
            if (color.w > 0.95f) break;
        }

        t += RAY_STEP_SIZE;
    }

    /* Background: Deep space blue-black */
    float bg_alpha = 1.0f - color.w;
    color.x += 0.02f * bg_alpha;
    color.y += 0.02f * bg_alpha;
    color.z += 0.05f * bg_alpha;

    /* ─────────────────────────────────────────────────────────────────────
     * 5. WRITE PIXEL
     * ───────────────────────────────────────────────────────────────────── */
    screen[pixel_idx] = make_uchar4(
        (unsigned char)(fminf(color.x, 1.0f) * 255.0f),
        (unsigned char)(fminf(color.y, 1.0f) * 255.0f),
        (unsigned char)(fminf(color.z, 1.0f) * 255.0f),
        255
    );
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MEMORY HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

void alloc_lattice(LiquidLattice* l) {
    cudaMalloc(&l->output, VOXEL_COUNT * sizeof(float));
    for (int i = 0; i < NODE_COUNT; i++) {
        cudaMalloc(&l->neurons[i], VOXEL_COUNT * sizeof(float));
    }
}

void free_lattice(LiquidLattice* l) {
    cudaFree(l->output);
    for (int i = 0; i < NODE_COUNT; i++) {
        cudaFree(l->neurons[i]);
    }
}

void zero_lattice(LiquidLattice* l) {
    cudaMemset(l->output, 0, VOXEL_COUNT * sizeof(float));
    for (int i = 0; i < NODE_COUNT; i++) {
        cudaMemset(l->neurons[i], 0, VOXEL_COUNT * sizeof(float));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GLFW CALLBACKS
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool manual_stimulus = false;
static float camera_angle = 0.0f;
static float camera_zoom = 1.0f;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
            case GLFW_KEY_SPACE:
                manual_stimulus = true;
                break;
            case GLFW_KEY_A:
                camera_angle -= 0.1f;
                break;
            case GLFW_KEY_D:
                camera_angle += 0.1f;
                break;
            case GLFW_KEY_W:
                camera_zoom *= 1.1f;
                break;
            case GLFW_KEY_S:
                camera_zoom *= 0.9f;
                break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ERROR CHECKING
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CUDA_CHECK(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA Error: %s at %s:%d\n", \
                cudaGetErrorString(err), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN: THE VISOR
 * ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ENTROMORPHIC VISOR — Volumetric Brain Renderer              ║\n");
    printf("║  \"The MRI of an Artificial Mind\"                             ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* ─────────────────────────────────────────────────────────────────────
     * 1. INITIALIZE GLFW & OPENGL
     * ───────────────────────────────────────────────────────────────────── */
    printf(">> Initializing graphics...\n");

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "ENTROMORPHIC VISOR", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);

    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW: %s\n", glewGetErrorString(glew_err));
        return 1;
    }

    printf("   OpenGL: %s\n", glGetString(GL_VERSION));
    printf("   Window: %dx%d\n\n", WIN_W, WIN_H);

    /* ─────────────────────────────────────────────────────────────────────
     * 2. INITIALIZE CUDA & PHYSICS GRIDS
     * ───────────────────────────────────────────────────────────────────── */
    printf(">> Initializing CUDA...\n");

    int device;
    cudaDeviceProp props;
    CUDA_CHECK(cudaGetDevice(&device));
    CUDA_CHECK(cudaGetDeviceProperties(&props, device));
    printf("   Device: %s\n", props.name);

    LiquidLattice gridA, gridB;
    alloc_lattice(&gridA);
    alloc_lattice(&gridB);
    zero_lattice(&gridA);
    zero_lattice(&gridB);

    printf("   Voxels: %d (%.1f MB)\n", VOXEL_COUNT,
           VOXEL_COUNT * sizeof(float) * (NODE_COUNT + 1) * 2 / 1e6);

    /* ─────────────────────────────────────────────────────────────────────
     * 3. CREATE PIXEL BUFFER OBJECT (PBO) FOR CUDA-GL INTEROP
     * ───────────────────────────────────────────────────────────────────── */
    printf(">> Setting up CUDA-GL interop...\n");

    GLuint pbo;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, WIN_W * WIN_H * 4, NULL, GL_DYNAMIC_DRAW);

    struct cudaGraphicsResource* cuda_pbo;
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&cuda_pbo, pbo, cudaGraphicsMapFlagsWriteDiscard));

    printf("   PBO registered for zero-copy rendering\n\n");

    /* ─────────────────────────────────────────────────────────────────────
     * 4. LAUNCH CONFIGURATION
     * ───────────────────────────────────────────────────────────────────── */
    dim3 physics_blocks(DIM / 8, DIM / 8, DIM / 8);
    dim3 physics_threads(8, 8, 8);

    dim3 render_blocks(WIN_W / 16, WIN_H / 16);
    dim3 render_threads(16, 16);

    printf(">> Controls:\n");
    printf("   ESC   - Quit\n");
    printf("   SPACE - Inject stimulus\n");
    printf("   A/D   - Rotate camera\n");
    printf("   W/S   - Zoom in/out\n\n");

    printf(">> IGNITION\n\n");

    /* ─────────────────────────────────────────────────────────────────────
     * 5. MAIN LOOP
     * ───────────────────────────────────────────────────────────────────── */
    int frame = 0;
    int physics_step = 0;
    double last_time = glfwGetTime();
    int frame_count = 0;

    while (!glfwWindowShouldClose(window)) {

        /* ── PHYSICS PHASE ─────────────────────────────────────────────── */
        for (int s = 0; s < STEPS_PER_FRAME; s++) {
            LiquidLattice* in  = (physics_step % 2 == 0) ? &gridA : &gridB;
            LiquidLattice* out = (physics_step % 2 == 0) ? &gridB : &gridA;

            /* Stimulus: Pulse every 200 steps, or manual */
            float stimulus = 0.0f;
            if (physics_step % 200 < 5) stimulus = 10.0f;
            if (manual_stimulus) {
                stimulus = 20.0f;
                manual_stimulus = false;
            }

            liquid_update<<<physics_blocks, physics_threads>>>(*in, *out, stimulus);
            physics_step++;
        }

        CUDA_CHECK(cudaDeviceSynchronize());

        /* ── RENDER PHASE ──────────────────────────────────────────────── */

        /* Map PBO for CUDA access */
        uchar4* dev_screen;
        size_t size;
        CUDA_CHECK(cudaGraphicsMapResources(1, &cuda_pbo, 0));
        CUDA_CHECK(cudaGraphicsResourceGetMappedPointer((void**)&dev_screen, &size, cuda_pbo));

        /* Camera orbit */
        float angle = camera_angle + frame * 0.002f;
        float radius = 350.0f / camera_zoom;
        float3 cam_pos = make_float3(
            DIM / 2.0f + sinf(angle) * radius,
            DIM / 2.0f + 50.0f,
            DIM / 2.0f + cosf(angle) * radius
        );
        float3 cam_target = make_float3(DIM / 2.0f, DIM / 2.0f, DIM / 2.0f);

        /* Get current lattice (the one just written to) */
        LiquidLattice* current = (physics_step % 2 == 0) ? &gridA : &gridB;

        /* Raymarch! */
        raymarch_kernel<<<render_blocks, render_threads>>>(
            dev_screen,
            current->output,
            WIN_W,
            WIN_H,
            cam_pos,
            cam_target,
            camera_zoom
        );

        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaGraphicsUnmapResources(1, &cuda_pbo, 0));

        /* ── DISPLAY PHASE ─────────────────────────────────────────────── */
        glClear(GL_COLOR_BUFFER_BIT);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glDrawPixels(WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        /* FPS counter */
        frame++;
        frame_count++;
        double current_time = glfwGetTime();
        if (current_time - last_time >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title),
                     "ENTROMORPHIC VISOR | %d FPS | Physics: %d Hz",
                     frame_count, frame_count * STEPS_PER_FRAME);
            glfwSetWindowTitle(window, title);
            frame_count = 0;
            last_time = current_time;
        }
    }

    /* ─────────────────────────────────────────────────────────────────────
     * 6. CLEANUP
     * ───────────────────────────────────────────────────────────────────── */
    printf("\n>> Shutdown\n");

    CUDA_CHECK(cudaGraphicsUnregisterResource(cuda_pbo));
    glDeleteBuffers(1, &pbo);

    free_lattice(&gridA);
    free_lattice(&gridB);

    glfwDestroyWindow(window);
    glfwTerminate();

    printf("\n\"It's all in the reflexes.\"\n");
    return 0;
}
