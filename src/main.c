#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>

#include "error.h"
#include "imports.h"
#include "jni_shim.h"
#include "so_util.h"
#include "util.h"

typedef int jint;

#define MEMORY_MB 256
#define SO_NAME "libgl2jni.so"

#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_BUTTON_A 96
#define AKEYCODE_BUTTON_B 97
#define AKEYCODE_BUTTON_X 99
#define AKEYCODE_BUTTON_Y 100
#define AKEYCODE_BUTTON_L1 102
#define AKEYCODE_BUTTON_R1 103
#define AKEYCODE_BUTTON_L2 104
#define AKEYCODE_BUTTON_R2 105
#define AKEYCODE_BUTTON_START 108
#define AKEYCODE_BACK 4

static jint (*JNI_OnLoad)(void *vm, void *reserved);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_assetmanager)(void *env, void *clazz, void *assetManager);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_init)(void *env, void *clazz, int width, int height);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_step)(void *env, void *clazz);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_pause)(void *env, void *clazz);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_resume)(void *env, void *clazz);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton)(void *env, void *clazz, int keycode, unsigned char isDown);
static void (*Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive)(void *env, void *clazz, int joyId, float x, float y);

static SDL_GameController *g_gamepad = NULL;

static int map_sdl_button_to_android(int sdl_button) {
    switch (sdl_button) {
        case SDL_CONTROLLER_BUTTON_A: return AKEYCODE_BUTTON_A;
        case SDL_CONTROLLER_BUTTON_B: return AKEYCODE_BUTTON_B;
        case SDL_CONTROLLER_BUTTON_X: return AKEYCODE_BUTTON_Y;
        case SDL_CONTROLLER_BUTTON_Y: return AKEYCODE_BUTTON_X;
        case SDL_CONTROLLER_BUTTON_START: return AKEYCODE_BUTTON_START;
        case SDL_CONTROLLER_BUTTON_BACK: return AKEYCODE_BACK; 
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return AKEYCODE_BUTTON_L1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return AKEYCODE_BUTTON_R1;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return AKEYCODE_DPAD_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return AKEYCODE_DPAD_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return AKEYCODE_DPAD_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return AKEYCODE_DPAD_RIGHT;
        default: return -1;
    }
}

static void open_gamepad() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_gamepad = SDL_GameControllerOpen(i);
            if (g_gamepad) {
                debugPrintf("Gamepad opened: %s\n", SDL_GameControllerName(g_gamepad));
                break;
            }
        }
    }
}

extern void opensles_shim_pump_callbacks(void) __attribute__((weak));

int main(int argc, char *argv[]) {
    debugPrintf("=== Crazy Taxi Classic AARCH64 ===\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fatal_error("Failed to init SDL2: %s", SDL_GetError());
    }
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
        fatal_error("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow("Crazy Taxi Classic",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          dm.w, dm.h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);

    if (!window) fatal_error("Failed to create SDL window: %s", SDL_GetError());

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) fatal_error("Failed to create GL context: %s", SDL_GetError());

    int draw_w, draw_h;
    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);

    open_gamepad();

    size_t heap_size = MEMORY_MB * 1024 * 1024;
    void *heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (heap == MAP_FAILED) fatal_error("Failed to allocate %d MB heap", MEMORY_MB);

    if (so_load(SO_NAME, heap, heap_size) < 0)
        fatal_error("Failed to load %s", SO_NAME);
    debugPrintf("Loaded %s: text=%p+%zu data=%p+%zu\n", SO_NAME, text_base, text_size, data_base, data_size);

    if (so_relocate() < 0) fatal_error("Failed to relocate %s", SO_NAME);
    if (so_resolve(dynlib_functions, dynlib_functions_count, 0) < 0) fatal_error("Failed to resolve imports");

    so_make_text_writable();

    int *useTouch = (int *)so_find_addr("useTouch");
    if (useTouch) {
        *useTouch = 0;
        debugPrintf("Patched useTouch = 0\n");
    }

    int *isPremiumUser = (int *)so_find_addr("isPremiumUser");
    if (isPremiumUser) {
        *isPremiumUser = 1;
        debugPrintf("Patched isPremiumUser = 1\n");
    }

    so_finalize();
    so_flush_caches();
    so_execute_init_array();

    JNI_OnLoad = (void *)so_find_addr("JNI_OnLoad");
    Java_com_sega_CrazyTaxi_GL2JNILib_assetmanager = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_assetmanager");
    Java_com_sega_CrazyTaxi_GL2JNILib_init = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_init");
    Java_com_sega_CrazyTaxi_GL2JNILib_step = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_step");
    Java_com_sega_CrazyTaxi_GL2JNILib_pause = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_pause");
    Java_com_sega_CrazyTaxi_GL2JNILib_resume = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_resume");
    Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton");
    Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive = (void *)so_find_addr("Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive");

    if (!Java_com_sega_CrazyTaxi_GL2JNILib_init || !Java_com_sega_CrazyTaxi_GL2JNILib_step) {
        fatal_error("Failed to find engine JNI functions in %s", SO_NAME);
    }

    void *fake_vm = NULL, *fake_env = NULL;
    jni_shim_init(&fake_vm, &fake_env);

    debugPrintf("Booting Crazy Taxi Engine...\n");
    if (JNI_OnLoad) JNI_OnLoad(fake_vm, NULL);

    void *dummy_asset_mgr = (void *)0xDEADBEEF;
    Java_com_sega_CrazyTaxi_GL2JNILib_assetmanager(fake_env, NULL, dummy_asset_mgr);

    Java_com_sega_CrazyTaxi_GL2JNILib_init(fake_env, NULL, draw_w, draw_h);

    if (Java_com_sega_CrazyTaxi_GL2JNILib_resume) {
        debugPrintf("Waking up engine (firing resume event)...\n");
        Java_com_sega_CrazyTaxi_GL2JNILib_resume(fake_env, NULL);
    }
    int running = 1;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP: {
                    int android_kc = map_sdl_button_to_android(e.cbutton.button);
                    if (android_kc >= 0 && Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton) {
                        Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton(
                            fake_env, NULL, android_kc, e.type == SDL_CONTROLLERBUTTONDOWN ? 1 : 0);
                    }
                    break;
                }
                case SDL_CONTROLLERAXISMOTION:
                    if (g_gamepad) {
                        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                            float lx = (float)SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                            float ly = (float)SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                            
                            if (lx > -0.2f && lx < 0.2f) lx = 0.0f;
                            if (ly > -0.2f && ly < 0.2f) ly = 0.0f;

                            if (Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive) {
                                Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive(fake_env, NULL, 0, lx, ly);
                            }
                        }
                        else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX || e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                            float rx = (float)SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
                            float ry = (float)SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
                            
                            if (rx > -0.2f && rx < 0.2f) rx = 0.0f;
                            if (ry > -0.2f && ry < 0.2f) ry = 0.0f;

                            if (Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive) {
                                Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive(fake_env, NULL, 1, rx, ry);
                            }
                        }
                        else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                            static int l2_pressed = 0;
                            static int r2_pressed = 0;
                            
                            int is_down = (e.caxis.value > 16384) ? 1 : 0; 
                            
                            if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && is_down != l2_pressed) {
                                l2_pressed = is_down;
                                Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton(fake_env, NULL, AKEYCODE_BUTTON_L1, is_down);
                            }
                            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && is_down != r2_pressed) {
                                r2_pressed = is_down;
                                Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton(fake_env, NULL, AKEYCODE_BUTTON_R1, is_down);
                            }
                        }
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_MINIMIZED && Java_com_sega_CrazyTaxi_GL2JNILib_pause) {
                        Java_com_sega_CrazyTaxi_GL2JNILib_pause(fake_env, NULL);
                    } else if (e.window.event == SDL_WINDOWEVENT_RESTORED && Java_com_sega_CrazyTaxi_GL2JNILib_resume) {
                        Java_com_sega_CrazyTaxi_GL2JNILib_resume(fake_env, NULL);
                    }
                    break;
            }
        }

        if (opensles_shim_pump_callbacks) {
            opensles_shim_pump_callbacks();
        }

        Java_com_sega_CrazyTaxi_GL2JNILib_step(fake_env, NULL);
        SDL_GL_SwapWindow(window);
    }

    debugPrintf("Exiting...\n");
    if (g_gamepad) SDL_GameControllerClose(g_gamepad);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
