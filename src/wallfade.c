#include <GL/gl.h>                  // for glColor4f, glTexCoord2f, glVertex2i
#include <GL/glx.h>                 // for glXChooseVisual, glXCreateContext
#include <X11/Xatom.h>              // for XA_ATOM
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

struct Path {
    char path[PATH_MAX];
};

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

struct _settings {
    Display *dpy;
    Screen *scr;
    XVisualInfo *vi;
    Window root;
    Window desktop;
    Window win;

    int running;
    int fading;

    float seconds;

    uint32_t screen;

    int nmon;
    int *nfiles;

    int fade;
    int idle;

    struct Plane *planes;
    struct Path *paths;
} settings;

struct _opengl {
    GLXContext ctx;
} opengl;

Window *findDesktop();
float getTime();
void getMonitors();
void initOpengl();
int init();
void gotsig(int signum);
void shutdown();
void drawplane(struct Plane *plane, uint32_t texture, float alpha);
void drawplanes();
void update();
int checkfile(char *file);
char **getfiles(int monitor);
void cleanFiles(char **files, int total_files);
void ThrowWandException(MagickWand *wand);
MagickWand *doMagick(const char *current, int width, int height);
void loadTexture(const char *current, uint32_t *id, int width, int height);
void randomImage(uint32_t *side, struct Plane *plane, const char *not,
                 int monitor);
void randomImages(int montior);
int parsePaths(char *paths);

Window *findDesktop()
{
    unsigned int n;
    Window troot, parent, *children;
    char *name;
    int status;
    XWindowAttributes attrs;

    XQueryTree(settings.dpy, settings.root, &troot, &parent, &children, &n);

    for (unsigned int i = 0; i < n; i++) {
        status = XFetchName(settings.dpy, children[i], &name);
        status |= XGetWindowAttributes(settings.dpy, children[i], &attrs);

        if ((status != 0) && (NULL != name)) {
            if (
                (attrs.map_state != 0) &&
                (attrs.width == settings.scr->width) &&
                (attrs.height == settings.scr->height) &&
                (!strcmp(name, "Desktop"))
            ) {
                settings.win = children[i];
                XFree(children);
                XFree(name);

                return &children[i];
            }

            if (name) {
                XFree(name);
            }
        }
    }

    return &settings.root;
}

float getTime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return 1000 * ts.tv_sec + (double)ts.tv_nsec / 1e6;
}

void getMonitors()
{
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

    settings.planes = malloc(settings.nmon * sizeof(struct Plane));
    settings.nfiles = malloc(settings.nmon * sizeof(int));

    for (int i = 0; i < settings.nmon; i++) {
        settings.planes[i].width = monitors[i].width;
        settings.planes[i].height = monitors[i].height;
        settings.planes[i].x = monitors[i].x;
        settings.planes[i].y = monitors[i].y;

        settings.planes[i].front = 0;
        settings.planes[i].back = 0;

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

    glXMakeCurrent(settings.dpy, settings.win, opengl.ctx);

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

int init(int argc, char **argv)
{
    MagickWandGenesis();

    settings.dpy = XOpenDisplay(NULL);

    if (settings.dpy == NULL) {
        fprintf(stderr, "Cannot connect to X server\n");
        return 0;
    }

    settings.scr = DefaultScreenOfDisplay(settings.dpy);

    if (settings.scr == NULL) {
        fprintf(stderr, "No screen found\n");
        return 0;
    }

    printf("ScreenSize: %dx%d\n", settings.scr->width, settings.scr->height);

    GLint vi_att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
    settings.vi = glXChooseVisual(settings.dpy, settings.screen, vi_att);

    if (settings.vi == NULL) {
        fprintf(stderr, "No appropriate visual found\n");
        return 0;
    }

    settings.root = RootWindow(settings.dpy, settings.screen);

    XSetWindowAttributes attr;
    attr.override_redirect = 1;

    Window *w = findDesktop();
    settings.win = XCreateWindow(
                       settings.dpy,
                       *w,
                       0,
                       0,
                       settings.scr->width,
                       settings.scr->height,
                       0,
                       CopyFromParent,
                       InputOutput,
                       CopyFromParent,
                       CWOverrideRedirect,
                       &attr
                   );

    XSizeHints xsh;
    xsh.flags  = PSize | PPosition;
    xsh.width  = settings.scr->width;
    xsh.height = settings.scr->height;

    XWMHints xwmh;
    xwmh.flags = InputHint;
    xwmh.input = 0;

    XSetWMProperties(
        settings.dpy,
        settings.win,
        NULL,
        NULL,
        argv,
        argc,
        &xsh,
        &xwmh,
        NULL
    );
    Atom state[4];

    state[0] = XInternAtom(settings.dpy, "_NET_WM_STATE_BELOW", 0);
    state[1] = XInternAtom(settings.dpy, "_NET_WM_STATE_FULLSCREEN", 0);
    state[2] = XInternAtom(settings.dpy, "_NET_WM_STATE_SKIP_PAGER", 0);
    state[3] = XInternAtom(settings.dpy, "_NET_WM_STATE_SKIP_TASKBAR", 0);

    XChangeProperty(
        settings.dpy,
        settings.win,
        XInternAtom(settings.dpy, "_NET_WM_STATE", 0),
        XA_ATOM,
        32,
        PropModeReplace,
        (unsigned char *) state,
        4
    );

    Atom type = XInternAtom(settings.dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", 0);

    XChangeProperty(
        settings.dpy,
        settings.win,
        XInternAtom(settings.dpy, "_NET_WM_WINDOW_TYPE", 1),
        XA_ATOM,
        32,
        PropModeReplace,
        (unsigned char *) &type,
        1
    );

    XMapWindow(settings.dpy, settings.win);
    XLowerWindow(settings.dpy, settings.win);
    XSync(settings.dpy, settings.win);

    getMonitors();
    initOpengl();

    return 1;
}

void gotsig(int signum)
{
    settings.running = 0;
}

void shutdown()
{
    if (settings.planes) {
        free(settings.planes);
    }

    if (settings.paths) {
        free(settings.paths);
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
    static float alpha = 0.0f;

    alpha += settings.fade * settings.seconds;

    if (alpha > 1.0f) {
        settings.fading = 0;

        for (int i = 0; i < settings.nmon; i++) {
            uint32_t tmp = settings.planes[i].front;
            settings.planes[i].front = settings.planes[i].back;
            strlcpy(
                settings.planes[i].front_path,
                settings.planes[i].back_path,
                sizeof(settings.planes[i].front_path)
            );
            settings.planes[i].back = tmp;

            randomImage(
                &settings.planes[i].back,
                &settings.planes[i],
                settings.planes[i].front_path,
                i
            );
        }

        alpha = 0.0f;
    }

    for (int i = 0; i < settings.nmon; i++) {
        if (settings.nfiles[i]) {
            if (settings.nfiles[i] > 1) {
                drawPlane(
                    &settings.planes[i],
                    settings.planes[i].back,
                    alpha
                );
                drawPlane(
                    &settings.planes[i],
                    settings.planes[i].front,
                    1.0f - alpha
                );
            } else {
                drawPlane(
                    &settings.planes[i],
                    settings.planes[i].front,
                    1.0f
                );
                randomImage(
                    &settings.planes[i].back,
                    &settings.planes[i],
                    settings.planes[i].front_path,
                    i
                );
            }
        }  else {
            randomImages(i);
        }
    }
}

void update()
{
    static float last_time = 0;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawPlanes();

    glXSwapBuffers(settings.dpy, settings.win);

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

char **getFiles(int monitor, int *total_files)
{
    char **files = 0;

    glob_t globbuf;

    int err = glob(
                  settings.paths[monitor].path,
                  GLOB_BRACE | GLOB_TILDE,
                  NULL,
                  &globbuf
              );

    settings.nfiles[monitor] = 0;

    if (err == 0) {
        files = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
        *total_files = globbuf.gl_pathc;

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

        settings.nfiles[monitor] = nfiles;
        globfree(&globbuf);
    }

    return files;
}

void cleanFiles(char **files, int total_files)
{
    if (total_files) {
        for (int i = 0; i < total_files; i++) {
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
    MagickRelinquishMemory(description);
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

    MagickResizeImage(wand, width, height, GaussianFilter, 1.0);

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

void randomImage(uint32_t *side, struct Plane *plane, const char *not,
                 int monitor)
{
    int total_files = 0;
    char **files = getFiles(monitor, &total_files);

    if (settings.nfiles[monitor] > 0) {
        int bkrand = 0;

        do {
            bkrand = rand() % settings.nfiles[monitor];
        } while (
            strcmp(files[bkrand], not) == 0 &&
            settings.nfiles[monitor] != 1
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

    }

    cleanFiles(files, total_files);
}

void randomImages(int monitor)
{
    randomImage(
        &settings.planes[monitor].front,
        &settings.planes[monitor],
        "",
        monitor
    );

    if (settings.nfiles[monitor] > 1) {
        randomImage(
            &settings.planes[monitor].back,
            &settings.planes[monitor],
            settings.planes[monitor].front_path,
            monitor
        );
    }
}

int parsePaths(char *paths)
{
    settings.paths = malloc(settings.nmon * sizeof(struct Path));

    for (int i = 0; i < settings.nmon; i++) {
        settings.paths[i].path[0] = 0;
    }

    char *p = 0;
    char *m = 0;

    char default_path[PATH_MAX] = {0};

    while ((p = strsep(&paths, ",")) != NULL) {
        if ((m = strsep(&p, ":")) != NULL && p != NULL) {
            int monitor = atoi(m);

            if (monitor >= 0 && monitor < settings.nmon) {
                strlcpy(
                    settings.paths[monitor].path,
                    p,
                    sizeof(settings.paths[monitor].path)
                );
            } else {
                fprintf(
                    stderr,
                    "Monitor %d not found, using default\n",
                    monitor
                );
            }
        }

        if (m != NULL) {
            strlcpy(default_path, m, sizeof(default_path));
        }
    }

    if (strlen(default_path)) {
        printf("Default path: %s\n", default_path);
    }

    for (int i = 0; i < settings.nmon; i++) {
        int len = strlen(settings.paths[i].path);

        if (len == 0) {
            if (strlen(default_path)) {
                strlcpy(
                    settings.paths[i].path,
                    default_path,
                    sizeof(settings.paths[i].path)
                );

                len = strlen(default_path);
            } else {
                fprintf(
                    stderr,
                    "Tried to set default path but none was specified!\n"
                );
                settings.running = 0;
                return 0;
            }
        }

        printf("Monitor %d path: %s\n", i, settings.paths[i].path);

        if (settings.paths[i].path[len] == '/') {
            strlcat(
                settings.paths[i].path,
                "*.{jpg,png}",
                sizeof(settings.paths[i].path)
            );
        } else {
            strlcat(
                settings.paths[i].path,
                "/*.{jpg,png}",
                sizeof(settings.paths[i].path)
            );
        }
    }

    return 1;
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
    settings.fading = 0;
    settings.fade = DEFAULT_FADE_SPEED;
    settings.idle = DEFAULT_IDLE_TIME;
    settings.planes = NULL;
    settings.paths = NULL;

    static const struct option longOpts[] = {
        { "paths", required_argument, 0, 'p' },
        { "fade", required_argument, 0, 'f' },
        { "idle", required_argument, 0, 'i' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    int longIndex = 0;
    char paths[PATH_MAX * 10];

    while (
        (c = getopt_long(argc, argv, "p:f:ih", longOpts, &longIndex)) != -1
    ) {

        switch (c) {

            case 'f':
                sscanf(optarg, "%d", &settings.fade);
                break;

            case 'i':
                sscanf(optarg, "%d", &settings.idle);
                break;

            case 'p':
                strlcpy(paths, optarg, PATH_MAX * 10);
                break;

            case 'h':
                printf("Usage: %s -p path [options]\n", argv[0]);
                printf("\t-f, fade \t: fade speed (default 3)\n");
                printf("\t-i, idle \t: idle time (default 3)\n");
                printf("\t-p, paths\t: wallpapers paths.\n");
                printf("\t         \t  use monitor:path, if no monitor is\n");
                printf("\t         \t  specified the path will be used as\n");
                printf("\t         \t  the default path\n\n");
                printf("\t         \t  example: -p 0:path,1:path,path\n\n");
                printf("\t-h, help \t: help\n");
                printf("\n");
                return (0);

            default:
                break;
        }
    }

    if (strlen(paths) == 0) {
        fprintf(stderr, "No paths specified!\n");
    } else {
        if (init(argc, argv)) {
            if (parsePaths(paths)) {
                for (int i = 0; i < settings.nmon; i++) {
                    randomImages(i);
                }

                while (settings.running) {
                    update();
                }
            }
        }
    }

    shutdown();

    return 0;
}
