#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>

PSP_MODULE_INFO("3D Cube Player", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272

static unsigned int __attribute__((aligned(16))) list[262144];
static int running = 1;

// CONFIG: flip these if an axis behaves inverted on your device

static const int INVERT_ANALOG_X = 1; // left/right invert (1 = flip)
static const int INVERT_ANALOG_Z = 1; // up/down invert (1 = flip)

// VERTEX DATA

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

static const Vertex __attribute__((aligned(16))) vertices[] = {
    { 0xFF000000, -0.5f, -0.5f, -0.5f },
    { 0xFF0000FF,  0.5f, -0.5f, -0.5f },
    { 0xFF00FF00,  0.5f,  0.5f, -0.5f },
    { 0xFF00FFFF, -0.5f,  0.5f, -0.5f },
    { 0xFFFF0000, -0.5f, -0.5f,  0.5f },
    { 0xFFFF00FF,  0.5f, -0.5f,  0.5f },
    { 0xFFFFFF00,  0.5f,  0.5f,  0.5f },
    { 0xFFFFFFFF, -0.5f,  0.5f,  0.5f },
};

static const unsigned short __attribute__((aligned(16))) indices[] = {
    0,1,2,  2,3,0,
    4,5,6,  6,7,4,
    0,4,7,  7,3,0,
    5,2,1,  5,6,2,
    0,1,5,  0,5,4,
    3,2,6,  3,6,7,
};

// EXIT CALLBACKS

static int exit_callback(int arg1, int arg2, void *common) {
    running = 0;
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("Callback Thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}


// GLOBAL STATE

static float playerX = 0.0f, playerY = 0.0f, playerZ = -3.0f;
static float viewRotX = 0.0f, viewRotY = 0.0f;
static float cubeRotX = 0.0f, cubeRotY = 0.0f;
static const float moveSpeed = 0.05f;
static const float viewSpeed = 1.5f;


// INIT GU

static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);

    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);

    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);

    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_CLIP_PLANES);

    sceGuFinish();
    sceGuSync(0,0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}


// INPUT + GAME UPDATE (modified: relative movement)

static void update_input(void) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);

    // Read analog (center = 128)
    float lx = (pad.Lx - 128) / 128.0f; // left/right
    float ly = (pad.Ly - 128) / 128.0f; // up/down

    // Apply inversion flags if needed
    if (INVERT_ANALOG_X) lx = -lx;
    if (INVERT_ANALOG_Z) ly = -ly;

    // Convert view rotation Y to radians for movement calculation
    float angleY = viewRotY * (M_PI / 180.0f);
    float sinY = sinf(angleY);
    float cosY = cosf(angleY);

    // Calculate movement relative to view direction
    // Forward/backward (ly) moves along the viewing direction
    // Left/right (lx) moves perpendicular to viewing direction
    float moveX = (lx * cosY - ly * sinY) * moveSpeed;
    float moveZ = (lx * sinY + ly * cosY) * moveSpeed;

    // Apply movement to player position
    playerX += moveX;
    playerZ += moveZ;

    // Rotate view with buttons (visual only)
    if (pad.Buttons & PSP_CTRL_TRIANGLE) viewRotX -= viewSpeed;
    if (pad.Buttons & PSP_CTRL_CROSS)    viewRotX += viewSpeed;
    if (pad.Buttons & PSP_CTRL_SQUARE)   viewRotY -= viewSpeed;
    if (pad.Buttons & PSP_CTRL_CIRCLE)   viewRotY += viewSpeed;

    // Auto spin the cube slowly (visual; buttons don't change this)
    cubeRotX += 0.3f;
    cubeRotY += 0.2f;
    if (cubeRotX >= 360.0f) cubeRotX -= 360.0f;
    if (cubeRotY >= 360.0f) cubeRotY -= 360.0f;
}


// DRAW

static void draw_scene(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xFF202020);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    // Projection
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(75.0f, (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.5f, 100.0f);

    // View (camera) - apply viewRotX/Y
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 camRot = { viewRotX * GU_PI/180.0f, viewRotY * GU_PI/180.0f, 0.0f };
    sceGumRotateXYZ(&camRot);

    // Model (cube/player)
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 pos = { playerX, playerY, playerZ };
    sceGumTranslate(&pos);
    ScePspFVector3 rot = { cubeRotX * GU_PI/180.0f, cubeRotY * GU_PI/180.0f, 0.0f };
    sceGumRotateXYZ(&rot);

    sceGumDrawArray(GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
        sizeof(indices)/sizeof(indices[0]), indices, vertices);

    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}


// MAIN

int main(void) {
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pspDebugScreenInit();

    init_gu();
    pspDebugScreenPrintf("3D Cube Player\nAnalog = Move (Relative)\n□ ○ △ × = Rotate View\nHOME to exit.\n");

    while (running) {
        update_input();
        draw_scene();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}