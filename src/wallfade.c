#include <GL/gl.h>                  // for glColor4f, glTexCoord2f, glVertex2i
#include <GL/glx.h>                 // for glXChooseVisual, glXCreateContext
#include <X11/X.h>                  // for None, Window
#include <X11/Xatom.h>              // for XA_ATOM
#include <X11/Xlib.h>               // for Screen, (anonymous), XOpenDisplay
#include <X11/Xutil.h>              // for XVisualInfo
#include <X11/extensions/Xrandr.h>  // for XRRMonitorInfo, XRRFreeMonitors
#include <dirent.h>                 // for DIR, opendir, closedir, readdir
#include <getopt.h>                 // for optarg, getopt
#include <glob.h>                   // for glob_t, glob, globfree, GLOB_BRACE
#include <limits.h>                 // for PATH_MAX
#include <signal.h>                 // for signal, SIGINT, SIGKILL, SIGQUIT
#include <stdbool.h>                // for bool
#include <stdint.h>                 // for uint32_t
#include <stdio.h>                  // for fprintf, NULL, printf, stderr
#include <stdlib.h>                 // for exit, free, malloc, rand, realpath
#include <string.h>                 // for __s1_len, __s2_len, strcmp, strlen
#include <sys/time.h>               // for CLOCK_MONOTONIC
#include <tgmath.h>                 // for fmaxf, fminf
#include <time.h>                   // for timespec, clock_gettime, time
#include <unistd.h>                 // for usleep
#include <ctype.h>                  // for isdigit

#include <sys/shm.h>
#include <sys/stat.h>

#include "magick.h"

#define MEM_SIZE 1024

#define DEFAULT_IDLE_TIME 3
#define DEFAULT_FADE_TIME 1

#define S_(x) #x
#define S(x) S_(x)

#define MESSAGE(y,x) !strcmp(y, x)

#define MSG_NONE '\0'
#define MSG_PARENT '\1'

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

struct OpenGL {
    GLXContext ctx;
};

struct _settings {
    Display *dpy;
    Screen *scr;
    XVisualInfo *vi;
    Window root;
    Window desktop;
    Window win;

    int base;
    int parent;

    float seconds;
    float timer;

    uint32_t screen;

    int nmon;
    int *nfiles;

    float fade;
    int idle;
    int smoothfunction;

    bool running;
    bool fading;
    bool center;

    char lower[PATH_MAX];
    char *shmem;

    struct Plane *planes;
    struct Path *paths;
    struct OpenGL opengl;
} settings;

pthread_t thread;

Window findByClass(const char *classname);
Window findDesktop();
float getDeltaTime();
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
void randomImages(int monitor);
int parsePaths(char *paths);
int handler(Display *dpy, XErrorEvent *e);
int getProcIdByName(const char *proc_name);
char *create_shared_memory(size_t size, int parent);

int handler(Display *dpy, XErrorEvent *e)
{
    fprintf(stderr, "X Error code: %d\n", e->error_code);
    return 0;
}

Window findByClass(const char *classname)
{
    unsigned int n;
    Window troot, parent, *children;
    char *name;
    XWindowAttributes attrs;

    XQueryTree(settings.dpy, settings.root, &troot, &parent, &children, &n);

    for (unsigned int i = 0; i < n; i++) {
        int status = XFetchName(settings.dpy, children[i], &name);
        status |= XGetWindowAttributes(settings.dpy, children[i], &attrs);

        if ((status != 0) && (NULL != name)) {
            if ((!strncmp(name, classname, strlen(classname)))) {
                XFree(children);
                XFree(name);

                return children[i];
            }

            if (name) {
                XFree(name);
            }
        }
    }

    return 0;
}

Window findDesktop()
{
    unsigned int n;
    Window troot, parent, *children;
    char *name;
    XWindowAttributes attrs;

    XQueryTree(settings.dpy, settings.root, &troot, &parent, &children, &n);

    for (unsigned int i = 0; i < n; i++) {
        int status = XFetchName(settings.dpy, children[i], &name);
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

                return children[i];
            }

            if (name) {
                XFree(name);
            }
        }
    }

    return settings.root;
}

float getDeltaTime()
{
    static struct timespec last_ts;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    float difftime = (ts.tv_sec * 1000 + ts.tv_nsec / 1000000) -
                     (last_ts.tv_sec * 1000 + last_ts.tv_nsec / 1000000);

    last_ts = ts;
    return difftime * 0.001f;
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
    settings.opengl.ctx = glXCreateContext(
                              settings.dpy,
                              settings.vi,
                              NULL,
                              GL_TRUE
                          );

    if (!settings.opengl.ctx) {
        fprintf(stderr, "Error: glXCreateContext failed\n");
        exit(-1);
    }

    glXMakeCurrent(settings.dpy, settings.win, settings.opengl.ctx);

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
    #ifdef GraphicsMagick
    InitializeMagick(*argv);
    #else
    MagickWandGenesis();
    #endif


    settings.dpy = XOpenDisplay(NULL);

    if (settings.dpy == NULL) {
        fprintf(stderr, "Cannot connect to X server\n");
        return 0;
    }

    XSetErrorHandler(handler);

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

    if (strlen(settings.lower)) {
        printf("Waiting for %s...\n", settings.lower);

        Window wid = 0;

        do {
            wid = findByClass(settings.lower);

            if (wid != 0) {
                break;
            }

            usleep(100000);
        } while (1);

        printf("Found %s (%lu)\n", settings.lower, wid);
        XLowerWindow(settings.dpy, wid);
    }

    XSetWindowAttributes attr = {0};
    attr.override_redirect = 1;

    Window desktop = findDesktop();
    settings.win = XCreateWindow(
                       settings.dpy,
                       desktop,
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

    Atom state[5];

    state[0] = XInternAtom(settings.dpy, "_NET_WM_STATE_BELOW", 0);
    state[1] = XInternAtom(settings.dpy, "_NET_WM_STATE_FULLSCREEN", 0);
    state[2] = XInternAtom(settings.dpy, "_NET_WM_STATE_SKIP_PAGER", 0);
    state[3] = XInternAtom(settings.dpy, "_NET_WM_STATE_SKIP_TASKBAR", 0);
    state[4] = XInternAtom(settings.dpy, "_NET_WM_STATE_STICKY", 0);

    Atom type = XInternAtom(settings.dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", 0);

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
    settings.running = false;
}

void shutdown()
{
    shmdt(&settings.shmem);

    if (settings.planes) {
        free(settings.planes);
    }

    if (settings.paths) {
        free(settings.paths);
    }

    for (int i = 0; i < settings.nmon; i++) {
        if (settings.planes[i].front != 0) {
            glDeleteTextures(1, &settings.planes[i].front);
        }
        if (settings.planes[i].back != 0) {
            glDeleteTextures(1, &settings.planes[i].back);
        }
    }

    glXDestroyContext(settings.dpy, settings.opengl.ctx);

    #ifdef GraphicsMagick
    DestroyMagick();
    #else
    MagickWandTerminus();
    #endif
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

float smooth(float min, float max, float val)
{
    float out = 0;

    if (settings.smoothfunction == 1) {
        out = val; // linear
    } else {
        float s = fmaxf(0, fminf(1, (val - min) / (max - min)));

        switch (settings.smoothfunction) {
            default:
            case 2: // smoothstep
                out = s * s * (3 - 2 * s);
                break;

            case 3: // smootherstep
                out = s * s * (s * (s * 6 - 15) + 10);
                break;
        }
    }

    return out;
}

void drawPlanes()
{
    float alpha = 0.0f;

    if (settings.fading) {
        static float linear = 0.0f;

        linear += settings.fade * settings.seconds;
        alpha = smooth(0.0f, 1.0f, linear);

        if (linear > 1.0f) {
            settings.fading = false;

            for (int i = 0; i < settings.nmon; i++) {
                uint32_t tmp = settings.planes[i].front;
                settings.planes[i].front = settings.planes[i].back;

                sprintf(
                    settings.planes[i].front_path,
                    "%.*s",
                    (int)sizeof(settings.planes[i].front_path),
                    settings.planes[i].back_path
                );

                settings.planes[i].back = tmp;

                randomImage(
                    &settings.planes[i].back,
                    &settings.planes[i],
                    settings.planes[i].front_path,
                    i
                );
            }

            linear = 0.0f;
            alpha = 0.0f;
        }
    }

    for (int i = 0; i < settings.nmon; i++) {
        if (settings.nfiles[i]) {
            if (settings.nfiles[i] > 1) {
                drawPlane(
                    &settings.planes[i],
                    settings.planes[i].back,
                    1.0f
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

void checkMessages()
{
    if (settings.shmem[0] != MSG_NONE && settings.shmem[0] != MSG_PARENT) {
        char *tmpstr = strdup(settings.shmem);
        char separator[4]=" ,\0";
        char *token = strtok(tmpstr,separator);
        while(token!=0)
        {
            if (MESSAGE(token,"next")) {
                settings.fading = true;
                settings.timer = 0;
            } else if (MESSAGE(token,"current")) {
                char output[MEM_SIZE] = {0};

                for (int i = 0; i < settings.nmon; i++) {
                    char line[128] = {0};

                    sprintf(
                        line,
                        "Monitor %d: %.*s\n",
                        i,
                        100,
                        settings.planes[i].front_path
                    );

                    int len = strlen(output);
                    sprintf(output + len, "%.*s", MEM_SIZE - len, line);
                }

                sprintf(settings.shmem, "%c%.*s", MSG_PARENT, MEM_SIZE, output);
                usleep(100000);
            } else if (MESSAGE(token,"fade")) {
                token = strtok(0,separator);
                if (token == 0) {
                    printf("fade needs a number after\n");
                    break;
                }
                if (isdigit(token[0])) {
                    settings.fade=1.0f/strtof(token,0);
                } else {
                    printf("idle needs a number after\n");
                    break;
                }
            } else if (MESSAGE(token,"idle")) {
                token = strtok(0,separator);
                if (token == 0) {
                    printf("idle needs a number after\n");
                    break;
                }
                if (isdigit(token[0])) {
                    settings.idle=strtol(token,0,10);
                } else {
                    printf("idle needs a number after\n");
                    break;
                }
            } else if (MESSAGE(token,"smooth")) {
                token = strtok(0,separator);
                if (token == 0) {
                    printf("smooth needs a number after\n");
                    break;
                }
                if (isdigit(token[0])) {
                    settings.smoothfunction = strtol(token, 0, 10);
                } else {
                    printf("smooth needs a number after\n");
                    break;
                }
            } else {
                fprintf(stderr, "Unknown command \"%s\"\n", token);
                settings.shmem[0] = MSG_NONE;
                return;
            }
            token = strtok(0,separator);
        }
        if(tmpstr) free(tmpstr);
        settings.shmem[0] = MSG_NONE;
    }
}

void update()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawPlanes();

    glXSwapBuffers(settings.dpy, settings.win);

    XFlush(settings.dpy);

    settings.seconds = getDeltaTime();

    if (settings.timer >= settings.idle) {
        settings.fading = true;
        settings.timer = 0;
    }

    if (!settings.fading) {
        settings.timer += settings.seconds;
        usleep(50000);
    }

    usleep(50000);

    checkMessages();
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
                sprintf(files[i], "%.*s", PATH_MAX - 1, file);
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
    #ifdef GraphicsMagick
    fprintf(stderr, "%s %s %d %s\n", GetMagickModule(), description);
    #else
    fprintf(stderr, "%s %s %lu %s\n", GetMagickModule(), description);
    #endif
    MagickRelinquishMemory(description);
    exit(-1);
}

MagickWand *doMagick(const char *current, int width, int height)
{
    MagickWand *wand = NewMagickWand();

    int status = MagickReadImage(wand, current);

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    status = MagickSetImageGravity(wand, CenterGravity);

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    int orig_height = MagickGetImageHeight(wand);
    int orig_width = MagickGetImageWidth(wand);

    int newheight = orig_height;
    int newwidth = orig_width;
    double screen_aspect = (double)width / (double)height;
    double image_aspect = (double)orig_width / (double)orig_height;

    if (screen_aspect < image_aspect) {
        newwidth = (int)((double)orig_height * screen_aspect);
    } else {
        newheight = (int)((double)orig_width / screen_aspect);
    }

    if (settings.center) {
        status = MagickCropImage(
                     wand,
                     newwidth,
                     newheight,
                     (orig_width - newwidth) / 2,
                     (orig_height - newheight) / 2
                 );
    } else {
        status = MagickCropImage(wand, newwidth, newheight, 0, 0);
    }

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    #if ImageMagick_MajorVersion < 7 || GraphicsMagick
    MagickResizeImage(wand, width, height, GaussianFilter, 1.0);
    #else
    MagickResizeImage(wand, width, height, GaussianFilter);
    #endif

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    return wand;
}

void loadTexture(const char *current, uint32_t *id, int width, int height)
{
    MagickWand *wand = doMagick(current, width, height);

    unsigned char *data = malloc((width * height) * 3);
    #ifdef GraphicsMagick
    int status = MagickGetImagePixels(
                     wand,
                     0,
                     0,
                     width,
                     height,
                     "RGB",
                     CharPixel,
                     data
                 );
    #else
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
    #endif

    if (status == MagickFalse) {
        ThrowWandException(wand);
    }

    if (*id != 0) {
        glBindTexture(GL_TEXTURE_2D, *id);

        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            width,
            height,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            data
        );
    } else {
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
    }

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
            bkrand = random() % settings.nfiles[monitor];
        } while (
            strcmp(files[bkrand], not) == 0 &&
            settings.nfiles[monitor] != 1
        );

        sprintf(
            plane->back_path,
            "%.*s",
            (int)sizeof(plane->back_path),
            files[bkrand]
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

    char default_path[PATH_MAX] = {0};

    while ((p = strsep(&paths, ",")) != NULL) {
        char *m = 0;

        if ((m = strsep(&p, ":")) != NULL && p != NULL) {
            int monitor = atoi(m);

            if (monitor >= 0 && monitor < settings.nmon) {
                sprintf(
                    settings.paths[monitor].path,
                    "%.*s",
                    (int)sizeof(settings.paths[monitor].path),
                    p
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
            sprintf(default_path, "%.*s", (int)sizeof(default_path) - 1, m);
        }
    }

    if (strlen(default_path)) {
        printf("Default path: %s\n", default_path);
    }

    for (int i = 0; i < settings.nmon; i++) {
        int len = strlen(settings.paths[i].path);

        if (len == 0) {
            if (strlen(default_path)) {
                sprintf(
                    settings.paths[i].path,
                    "%.*s",
                    (int)sizeof(settings.paths[i].path),
                    default_path
                );

                len = strlen(default_path);
            } else {
                fprintf(
                    stderr,
                    "Tried to set default path but none was specified!\n"
                );
                settings.running = false;
                return 0;
            }
        }

        printf("Monitor %d path: %s\n", i, settings.paths[i].path);

        if (settings.paths[i].path[len] != '/') {
            sprintf(
                settings.paths[i].path + len,
                "%.*s",
                (int)sizeof(settings.paths[i].path) - len,
                "/"
            );

            len = strlen(settings.paths[i].path);
        }

        sprintf(
            settings.paths[i].path + len,
            "%.*s",
            (int)sizeof(settings.paths[i].path) - len,
            "*.{jpg,png}"
        );
    }

    return 1;
}

void help(const char *filename)
{
    printf("Usage: %s -p path [options]\n", filename);
    printf("    -f, fade    : fade time (default 1.0s)\n");
    printf("    -i, idle    : idle time (default 3s)\n");
    printf("    -s, smooth  : smoothing function to use when fading.\n");
    printf("                    1: linear\n");
    printf("                    2: smoothstep (default)\n");
    printf("                    3: smootherstep\n");
    printf("\n");
    printf("    -p, paths   : wallpapers paths.\n");
    printf("                    use monitor:path, if no monitor is\n");
    printf("                    specified the path will be used as\n");
    printf("                    the default path\n");
    printf("\n");
    printf("                    example: -p 0:path,1:path,path\n");
    printf("\n");
    printf("    -l, lower   : finds and lowers window by classname (e.g. Conky)\n");
    printf("    -c, center  : center wallpapers\n");
    printf("    -m, message : send message to running process\n");
    printf("    -h, help    : help\n");
    printf("\n");
}

int getProcIdByName(const char *proc_name)
{
    int pid = -1;
    int current = getpid();

    DIR *proc = opendir("/proc");

    if (proc != NULL) {
        struct dirent *ent;

        while (pid < 0 && (ent = readdir(proc))) {
            int id = atoi(ent->d_name);

            if (id > 0 && id != current) {
                char path[PATH_MAX] = {0};
                sprintf(path, "/proc/%s/stat", ent->d_name);

                FILE *f = fopen(path, "r");

                if (f == NULL) {
                    fprintf(stderr, "Unable to open %s for reading!\n", path);
                    continue;
                }

                char name[PATH_MAX];

                int ret = fscanf(f, "%*d (%" S(PATH_MAX) "[^)]", name);

                if (ret > 0 && !strcmp(name, proc_name)) {
                    pid = id;
                    break;
                }

                fclose(f);
            }
        }
    }

    closedir(proc);

    return pid;
}

char *create_shared_memory(size_t size, int parent)
{
    int shmid = shmget(parent, size, IPC_CREAT | S_IRUSR | S_IWUSR);
    return shmat(shmid, 0, 0);
}

int main(int argc, char *argv[])
{
    struct timespec ts;

    if (timespec_get(&ts, TIME_UTC) == 0) {
        fprintf(stderr, "Unable to get time for random!\n");
        return EXIT_FAILURE;
    }

    srandom(ts.tv_nsec ^ ts.tv_sec);

    int c;

    signal(SIGINT, gotsig);
    signal(SIGKILL, gotsig);
    signal(SIGTERM, gotsig);
    signal(SIGQUIT, gotsig);

    memset(settings.lower, 0, PATH_MAX);

    settings.smoothfunction = 2;
    settings.timer = 0;
    settings.center = false;
    settings.running = true;
    settings.fading = false;
    settings.fade = DEFAULT_FADE_TIME;
    settings.idle = DEFAULT_IDLE_TIME;
    settings.planes = NULL;
    settings.paths = NULL;
    settings.parent = getProcIdByName("wallfade");

    settings.shmem = create_shared_memory(MEM_SIZE, getpid());
    memset(settings.shmem, 0, MEM_SIZE);

    static const struct option longOpts[] = {
        { "lower", required_argument, 0, 'l' },
        { "center", required_argument, 0, 'c' },
        { "paths", required_argument, 0, 'p' },
        { "smooth", required_argument, 0, 's' },
        { "fade", required_argument, 0, 'f' },
        { "idle", required_argument, 0, 'i' },
        { "message", required_argument, 0, 'm' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    int longIndex = 0;
    char paths[PATH_MAX * 10] = {0};

    while ((c = getopt_long(
                    argc,
                    argv,
                    "o:p:f:i:hcs:l:m:",
                    longOpts,
                    &longIndex
                )) != -1) {
        switch (c) {

            case 'f':
                settings.fade = 1.0f / strtof(optarg, NULL);
                break;

            case 'i':
                settings.idle = strtol(optarg, NULL, 10);
                break;

            case 'l':
                sprintf(settings.lower, "%.*s", PATH_MAX - 1, optarg);
                break;

            case 'c':
                settings.center = true;
                break;

            case 's':
                settings.smoothfunction = strtol(optarg, NULL, 10);
                break;

            case 'p':
                sprintf(paths, "%.*s", (PATH_MAX * 10) - 1, optarg);
                break;

            case 'm':
                if (settings.parent != -1) {
                    settings.shmem = create_shared_memory(
                                         MEM_SIZE,
                                         settings.parent
                                     );

                    while (settings.shmem[0] != MSG_NONE) {
                        usleep(10000);
                    }

                    sprintf(settings.shmem, "%.*s", MEM_SIZE, optarg);

                    while (settings.shmem[0] != MSG_NONE) {
                        if (settings.shmem[0] == MSG_PARENT) {
                            printf("%s", &settings.shmem[1]);
                            settings.shmem[0] = MSG_NONE;
                            break;
                        }

                        usleep(10000);
                    }

                    shmdt(&settings.shmem);
                } else {
                    fprintf(stderr, "No wallfade process found!\n");
                    return EXIT_FAILURE;
                }

                return EXIT_SUCCESS;

            case 'h':
                help(argv[0]);
                return EXIT_SUCCESS;

            default:
                break;
        }
    }

    if (settings.parent != -1) {
        fprintf(stderr, "Another wallfade processes is already running!\n");
        return EXIT_FAILURE;
    }

    if (strlen(paths) == 0) {
        fprintf(stderr, "No paths specified!\n");
        help(argv[0]);
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

            shutdown();
        }
    }

    return EXIT_SUCCESS;
}
