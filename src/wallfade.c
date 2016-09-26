#include <GL/gl.h>                  // for glColor4f, glTexCoord2f, glVertex2i
#include <GL/glx.h>                 // for glXChooseVisual, glXCreateContext
#include <X11/X.h>                  // for None, Window
#include <X11/Xlib.h>               // for Screen, (anonymous), XOpenDisplay
#include <X11/Xutil.h>              // for XVisualInfo
#include <X11/extensions/Xrandr.h>  // for XRRMonitorInfo, XRRFreeMonitors
#include <bsd/string.h>             // for strlcpy, strlcat
#include <getopt.h>                 // for optarg, getopt
#include <glob.h>                   // for glob_t, glob, globfree, GLOB_BRACE
#include <limits.h>                 // for PATH_MAX
#include <signal.h>                 // for signal, SIGINT, SIGKILL, SIGQUIT
#include <stdint.h>                 // for uint32_t
#include <stdio.h>                  // for fprintf, NULL, printf, stderr
#include <stdlib.h>                 // for exit, free, malloc, rand, realpath
#include <string.h>                 // for __s1_len, __s2_len, strcmp, strlen
#include <sys/time.h>               // for CLOCK_MONOTONIC
#include <time.h>                   // for timespec, clock_gettime, time
#include <unistd.h>                 // for usleep
#include <wand/MagickWand.h>        // for MagickWand, DestroyMagickWand
#include "magick/constitute.h"      // for ::CharPixel
#include "magick/exception.h"       // for ExceptionType
#include "magick/geometry.h"        // for ::CenterGravity
#include "magick/log.h"             // for GetMagickModule
#include "magick/magick-type.h"     // for ::MagickFalse
#include "magick/resample.h"        // for ::LanczosFilter
#include "wand/magick-image.h"      // for MagickExportImagePixels, MagickGe...
#include "wand/magick-property.h"   // for MagickSetGravity

#define DEFAULT_IDLE_TIME 3
#define DEFAULT_FADE_SPEED 3

struct _opengl {
    GLXContext ctx;
} opengl;

struct _settings {
    Display *dpy;
    Screen *scr;
    XVisualInfo *vi;
    Window root;

    int running;
    int fading;

    float seconds;

    uint32_t screen;
    uint32_t wid;

    int nmon;
    int nfiles;
    int total_files;

    int fade;
    int idle;

    char path[PATH_MAX];
} settings;

struct Plane {
    int width;
    int height;
    int x;
    int y;

    uint32_t front;
    uint32_t back;

    char front_path[PATH_MAX];
    char back_path[PATH_MAX];
};

struct Plane *planes = NULL;

float getTime();
void getMonitors();
void initOpengl();
void init();
void gotsig(int signum);
void shutdown();
void drawplane(struct Plane *plane, uint32_t texture, float alpha);
void drawplanes();
void update();
int checkfile(char *file);
char **getfiles();
void cleanFiles(char **files);
void ThrowWandException(MagickWand *wand);
MagickWand *doMagick(const char *current, int width, int height);
void loadTexture(const char *current, uint32_t *id, int width, int height);
void randomImage(uint32_t *side, struct Plane *plane, const char *not);
void randomImages();

float getTime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return 1000 * ts.tv_sec + (double)ts.tv_nsec / 1e6;
}

void getMonitors()
{
    if (planes) {
        free(planes);
    }

    XRRMonitorInfo *monitors = XRRGetMonitors(
                                   settings.dpy,
                                   settings.root,
                                   0,
                                   &settings.nmon
                               );

    if (settings.nmon == -1) {
        fprintf(stderr, "Unable to find monitors\n");
        exit(-1);
    }

    planes = malloc(settings.nmon * sizeof(struct Plane));

    for (int i = 0; i < settings.nmon; i++) {
        planes[i].width = monitors[i].width;
        planes[i].height = monitors[i].height;
        planes[i].x = monitors[i].x;
        planes[i].y = monitors[i].y;

        planes[i].front = 0;
        planes[i].back = 0;

        printf(
            "monitor: %d %dx%d+%d+%d\n",
            i,
            monitors[i].width,
            monitors[i].height,
            monitors[i].x,
            monitors[i].y
        );
    }

    XRRFreeMonitors(monitors);
}

void initOpengl()
{
    opengl.ctx = glXCreateContext(settings.dpy, settings.vi, NULL, GL_TRUE);

    if (!opengl.ctx) {
        fprintf(stderr, "Error: glXCreateContext failed\n");
        exit(-1);
    }

    glXMakeCurrent(settings.dpy, settings.wid, opengl.ctx);

    printf("GL_RENDERER = %s\n", (char *) glGetString(GL_RENDERER));

    //glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(0, settings.scr->width, settings.scr->height, 0, -100.0f, 100.0f);
    glViewport(0, 0, settings.scr->width, settings.scr->height);

    glClearColor(0, 0, 0, 1);
}

void init()
{
    MagickWandGenesis();

    settings.dpy = XOpenDisplay(NULL);

    if (settings.dpy == NULL) {
        fprintf(stderr, "Cannot connect to X server\n");
        exit(-1);
    }

    settings.scr = DefaultScreenOfDisplay(settings.dpy);

    if (settings.scr == NULL) {
        fprintf(stderr, "No screen found\n");
        exit(-1);
    }

    printf("ScreenSize: %dx%d\n", settings.scr->width, settings.scr->height);

    GLint vi_att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
    settings.vi = glXChooseVisual(settings.dpy, settings.screen, vi_att);

    if (settings.vi == NULL) {
        fprintf(stderr, "No appropriate visual found\n");
        exit(-1);
    }

    settings.root = RootWindow(settings.dpy, settings.screen);

    getMonitors();
    initOpengl();
}

void gotsig(int signum)
{
    settings.running = 0;
}

void shutdown()
{
    if (planes) {
        free(planes);
    }

    glXDestroyContext(settings.dpy, opengl.ctx);

    MagickWandTerminus();
}

void drawPlane(struct Plane *plane, uint32_t texture, float alpha)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glColor4f(1, 1, 1, alpha);
    glVertex2i(
        plane->x,
        plane->y
    );
    glTexCoord2f(0, 1);
    glColor4f(1, 1, 1, alpha);
    glVertex2i(
        plane->x,
        plane->y + plane->height
    );
    glTexCoord2f(1, 1);
    glColor4f(1, 1, 1, alpha);
    glVertex2i(
        plane->x + plane->width,
        plane->y + plane->height
    );
    glTexCoord2f(1, 0);
    glColor4f(1, 1, 1, alpha);
    glVertex2i(
        plane->x + plane->width,
        plane->y
    );
    glEnd();
}

void drawPlanes()
{
    if (settings.nfiles) {
        if (settings.nfiles > 1) {
            static float alpha = 0.0f;

            alpha += settings.fade * settings.seconds;

            if (alpha > 1.0f) {
                settings.fading = 0;

                for (int i = 0; i < settings.nmon; i++) {
                    uint32_t tmp = planes[i].front;
                    planes[i].front = planes[i].back;
                    strlcpy(
                        planes[i].front_path,
                        planes[i].back_path,
                        sizeof(planes[i].front_path)
                    );
                    planes[i].back = tmp;

                    randomImage(
                        &planes[i].back,
                        &planes[i],
                        planes[i].front_path
                    );
                }

                alpha = 0.0f;
            }

            for (int i = 0; i < settings.nmon; i++) {
                drawPlane(&planes[i], planes[i].back, alpha);
                drawPlane(&planes[i], planes[i].front, 1.0f - alpha);
            }
        } else {
            for (int i = 0; i < settings.nmon; i++) {
                drawPlane(&planes[i], planes[i].front, 1.0f);
                randomImage(
                    &planes[i].back,
                    &planes[i],
                    planes[i].front_path
                );
            }
        }
    }  else {
        randomImages();
    }
}

void update()
{
    static float last_time = 0;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawPlanes();

    glXSwapBuffers(settings.dpy, settings.wid);

    if (settings.fading) {
        float current_time = getTime();
        settings.seconds = (current_time - last_time) * 0.0001f;
        last_time = current_time;

        usleep(50000);
    } else {
        usleep(settings.idle * 1000000);

        settings.fading = 1;
        last_time = getTime();
    }
}

char **getFiles()
{
    char **files = 0;

    glob_t globbuf;

    int err = glob(settings.path, GLOB_BRACE | GLOB_TILDE, NULL, &globbuf);

    settings.nfiles = 0;

    if (err == 0) {
        files = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
        settings.total_files = globbuf.gl_pathc;

        size_t i;
        int nfiles = 0;

        #pragma omp parallel for private(i) reduction(+:nfiles)

        for (i = 0; i < globbuf.gl_pathc; i++) {
            char *file = realpath(globbuf.gl_pathv[i], NULL);

            if (file == NULL) {
                fprintf(
                    stderr,
                    "Unable to resolve realpath for %s",
                    globbuf.gl_pathv[i]
                );
            } else {
                files[i] = malloc(PATH_MAX);
                strlcpy(files[i], file, PATH_MAX);
                nfiles++;

                free(file);
            }
        }

        settings.nfiles = nfiles;
        globfree(&globbuf);
    }

    return files;
}

void cleanFiles(char **files)
{
    if (settings.total_files) {
        for (int i = 0; i < settings.total_files; i++) {
            free(files[i]);
        }

        free(files);
    }
}

void ThrowWandException(MagickWand *wand)
{
    char *description;

    ExceptionType severity;

    description = MagickGetException(wand, &severity);
    fprintf(stderr, "%s %s %lu %s\n", GetMagickModule(), description);
    description = (char *) MagickRelinquishMemory(description);
    exit(-1);
}

MagickWand *doMagick(const char *current, int width, int height)
{
    MagickWand *iwand = NewMagickWand();

    int status = MagickReadImage(iwand, current);

    if (status == MagickFalse) {
        ThrowWandException(iwand);
    }

    int orig_height = MagickGetImageHeight(iwand);
    int orig_width = MagickGetImageWidth(iwand);

    status = MagickSetGravity(iwand, CenterGravity);

    if (status == MagickFalse) {
        ThrowWandException(iwand);
    }

    char crop[256];

    if (orig_width < orig_height) {
        double aspect = (double)height / (double)width;
        sprintf(crop, "%dx%d+0+0", orig_width, (int)(orig_width * aspect));
    } else  {
        double aspect = (double)width / (double)height;
        sprintf(crop, "%dx%d+0+0", (int)(orig_height * aspect), orig_height);
    }

    MagickWand *wand = MagickTransformImage(iwand, crop, "");

    MagickResizeImage(wand, width, height, LanczosFilter, 1.0);

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    //printf("%s %dx%d\n", current, orig_width, orig_height);

    DestroyMagickWand(iwand);

    return wand;
}

void loadTexture(const char *current, uint32_t *id, int width, int height)
{
    MagickWand *wand = doMagick(current, width, height);

    unsigned char *data = malloc((width * height) * 3);
    int status = MagickExportImagePixels(
                     wand,
                     0,
                     0,
                     width,
                     height,
                     "RGB",
                     CharPixel,
                     data
                 );

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    if (*id != 0) {
        glDeleteTextures(1, id);
    }

    glGenTextures(1, id);

    glBindTexture(GL_TEXTURE_2D, *id);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        width,
        height,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        data
    );

    DestroyMagickWand(wand);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);

    MagickRelinquishMemory(data);
}

void randomImage(uint32_t *side, struct Plane *plane, const char *not)
{
    char **files = getFiles();

    if (settings.nfiles) {
        int bkrand = 0;

        do {
            bkrand = rand() % settings.nfiles;
        } while (
            strcmp(files[bkrand], not) == 0 &&
            settings.nfiles != 1
        );

        strlcpy(
            plane->back_path,
            files[bkrand],
            sizeof(plane->back_path)
        );

        loadTexture(
            plane->back_path,
            side,
            plane->width,
            plane->height
        );

        cleanFiles(files);
    }
}

void randomImages()
{
    char **files = getFiles();

    if (settings.nfiles) {
        for (int i = 0; i < settings.nmon; i++) {
            strlcpy(
                planes[i].front_path,
                files[rand() % settings.nfiles],
                sizeof(planes[i].front_path)
            );

            loadTexture(
                planes[i].front_path,
                &planes[i].front,
                planes[i].width,
                planes[i].height
            );

            if (settings.nfiles > 1) {
                int bkrand = 0;

                do {
                    bkrand = rand() % settings.nfiles;
                } while (strcmp(files[bkrand], planes[i].front_path) == 0);

                strlcpy(
                    planes[i].back_path,
                    files[bkrand],
                    sizeof(planes[i].back_path)
                );

                loadTexture(
                    planes[i].back_path,
                    &planes[i].back,
                    planes[i].width,
                    planes[i].height
                );
            }
        }

        cleanFiles(files);
    } else {
        printf("No files found!\n");
    }
}

int main(int argc, char *argv[])
{
    srand(time(0));

    int c;

    signal(SIGINT, gotsig);
    signal(SIGKILL, gotsig);
    signal(SIGTERM, gotsig);
    signal(SIGQUIT, gotsig);

    settings.running = 1;
    settings.path[0] = 0;
    settings.fading = 0;
    settings.fade = DEFAULT_FADE_SPEED;
    settings.idle = DEFAULT_IDLE_TIME;

    static const struct option longOpts[] = {
        { "wid", required_argument, 0, 'w' },
        { "path", required_argument, 0, 'p' },
        { "fade", required_argument, 0, 'f' },
        { "idle", no_argument, 0, 'i' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    int longIndex = 0;

    while (
        (c = getopt_long(argc, argv, "w:p:f:ih", longOpts, &longIndex)) != -1
    ) {

        switch (c) {
            case 'w':
                sscanf(optarg, "%x", &settings.wid);
                break;

            case 'f':
                sscanf(optarg, "%d", &settings.fade);
                break;

            case 'i':
                sscanf(optarg, "%d", &settings.idle);
                break;

            case 'p':
                strlcpy(settings.path, optarg, sizeof(settings.path));
                break;

            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("\t-w, wid \t: window id\n");
                printf("\t-p, path\t: wallpapers path\n");
                printf("\t-f, fade\t: fade speed (default 3)\n");
                printf("\t-i, idle\t: idle time (default 3)\n");
                printf("\t-h, help\t: help\n");
                printf("\n");
                return (0);

            default:
                break;
        }
    }

    if (settings.path[strlen(settings.path)] == '/') {
        strlcat(settings.path, "*.{jpg,png}", sizeof(settings.path));
    } else {
        strlcat(settings.path, "/*.{jpg,png}", sizeof(settings.path));
    }

    init();

    randomImages();

    while (settings.running) {
        update();
    }

    shutdown();

    return 0;
}
