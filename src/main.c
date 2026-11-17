#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>

#include <raylib.h>

#define CANVAS_WIDTH          1280
#define CANVAS_HEIGHT         1024
#define HIGHLIGHTER_OPACITY   0.9

#define DEBUG 0

bool needsToEnterMenu   = 0;
int previousNum         = 0;
int penStart            = 0;
int highlighterStart    = 0;
int menuStrokesStart    = 0;
bool inMenu             = false;
struct History other;

enum ColorT {
    White      = 0, /* background */
    Red        = 1,
    Green      = 2,
    Yellow     = 3,
    Blue       = 4,
    Magenta    = 5,
    Cyan       = 6,
    Black      = 7,
};

/*
 * To create a menu file, draw the strokes in 
 * the following order. Anything drawn after 
 * these has no special behavior and is just 
 * for decoration.
 */

enum Action {
    Action_UseBlack         = 0, // do not change order
    Action_UseRed           = 1, //
    Action_UseGreen         = 2, //
    Action_UseYellow        = 3, //
    Action_UseBlue          = 4, //
    Action_UseMagenta       = 5, //
    Action_UseCyan          = 6, //

    Action_ExitMenu         = 7,
    Action_Save             = 8,
    Action_ChangeTheme      = 9,

    Action_Eraser           = 10,
};

enum Message {
    Message_ChangeColor     = 1 << 0,  // save color to state.colorChange
    Message_ChangeTool      = 1 << 1,  // save tool to state.toolChange
    Message_ExitMenu        = 1 << 2,
    Message_EnterMenu       = 1 << 3,
    Message_Save            = 1 << 4,
    Message_ChangeTheme     = 1 << 5,
    Message_RedrawFrame     = 1 << 6,
};

#include "themes.c"

Color *themes[] = {
    rosePineDawn,
    everforestLight,
    kanagawaLotus
};

int themeIndex = 0;
int themeCount = 3;

enum Markings {
    Lines,
    Grid,
    Dots,
    None,
};

enum Tool {
    Pen,
    Highlighter,
    Eraser,
};

typedef struct {
    uint8_t             thickness;
    enum ColorT         color;
} BrushOptions;

typedef struct {
    uint16_t            x;
    uint16_t            y;
} Position;

struct Stroke {
    uint16_t            numPoints;
    uint16_t            pointIndex;
    bool                hidden;               /* expensive */
    enum ColorT         color;
};

typedef struct {
    void              * data;
    int                 length;
    int                 elementSize;
    int                 capacity;
} DynArr;

struct History {
    DynArr              points;
    DynArr              highlighterStrokes;
    DynArr              penStrokes;
};

typedef struct State {
    int                 width;
    int                 height;
    BrushOptions        penOptions;
    BrushOptions        highlighterOptions;
    enum Tool           tool;
    Color             * theme;
    bool                needsRedraw;
    enum Markings       markings;
    struct History      history;
    uint16_t          * gridOfIndecies;
    Color             * colorGrid;
    enum ColorT         colorChange;    // used by Message_ChangeColor
    enum Tool           toolChange;     // used by Message_ChangeTool
    int                 messages;
} State;

DynArr            newArr(int);
struct History    newhistory();
void              push(DynArr*, void*);
void *            atIndex(DynArr*, int);
Color             highligtherColor(Color*, enum ColorT, bool);
void              exportSVG(FILE*, State*);
void              exportPPM(FILE*, State*);
void              applyToolToPixel(int, int, State*, uint16_t);
void              plotLineLow2(int, int, int, int, State*, uint16_t);
void              plotLineHigh2(int, int, int, int, State*, uint16_t);
void              plotLine2(int, int, int, int, State*, uint16_t);
void              drawPenStrokes(State*, Color*);
void              drawHighlighterStrokes(State*, Color*);
void              drawGrid(State*, Color*);
void              drawCanvas2(State*, Color*);
State             newState(int, int);
void              exitWithHelp();
int               main(int, char**);

DynArr 
newArr(int elementSize) 
{
    return (DynArr) {
        .data        = calloc(4, elementSize),
        .length      = 0,
        .elementSize = elementSize,
        .capacity    = 4,
    };
}

struct History 
newhistory() 
{
    return (struct History) {
        .points             = newArr(sizeof(Position)),
        .highlighterStrokes = newArr(sizeof(struct Stroke)),
        .penStrokes         = newArr(sizeof(struct Stroke))
    };
}

void 
push(DynArr *d, void *value) 
{
    if (d->length == d->capacity) {
        d->data = realloc(d->data, 2 * d->elementSize * d->capacity);
        assert(d->data);
        d->capacity *= 2;
    }
    memcpy(d->data + d->elementSize * d->length, value, d->elementSize);
    d->length++;
}

void *
atIndex(DynArr *d, int index) 
{
    return d->data + index * d->elementSize;
}

Color 
highligtherColor(Color *theme, enum ColorT col, bool grid) 
{
    float opacity; 
    Color w, bg;

    opacity = HIGHLIGHTER_OPACITY;
    if (grid) opacity -= 0.2;
    w  = theme[White];
    bg = theme[col];
    bg.r = (bg.r * (1.0 - opacity) + w.r * opacity);
    bg.g = (bg.g * (1.0 - opacity) + w.g * opacity);
    bg.b = (bg.b * (1.0 - opacity) + w.b * opacity);
    return bg;
}

void 
exportSVG(FILE* file, State *state) 
{
    Color c;
    struct Stroke *s;
    int i, p, width, height;

    float scale = 0.75;
    struct History v = state->history;
    width = CANVAS_WIDTH  * scale;
    height = CANVAS_HEIGHT * scale;
    fprintf(file,
            "<svg \n"
            "    width=\"%d\" \n"
            "    height=\"%d\" \n"
            "    viewBox=\"0 0 %d %d\" \n"
            "    xmlns=\"http://www.w3.org/2000/svg\" \n"
            "    xmlns:xlink=\"http://www.w3.org/1999/xlink\" >\n",
            width, height,
            width, height);
    c = state->theme[White];
    fprintf(file,
            "    <rect \n"
            "        fill=\"rgb(%d,%d,%d,255)\" \n"
            "        stroke=\"rgb(0,0,0,0)\" \n"
            "        x=\"0\" \n"
            "        y=\"0\" \n"
            "        width=\"%d\" \n"
            "        height=\"%d\" />\n",
            c.r, c.g, c.b,
            width,
            height);

    for (i = 0; i < v.highlighterStrokes.length; i++) {
        s = atIndex(&v.highlighterStrokes, i);
        if (s->hidden) continue;
        fprintf(file, "    <polyline \n");
        fprintf(file, "        points=\"");
        for (p = 0; p < s->numPoints; p++) {
            Position *point = atIndex(&v.points, s->pointIndex + p);
            fprintf(file, 
                    "%d,%d ", 
                    (int)(point->x * scale), 
                    (int)(point->y * scale));
        }
        c = highligtherColor(state->theme, s->color, false);
        fprintf(file,
                "\" \n"
                "        stroke=\"rgb(%d, %d, %d, 255)\" \n"
                "        stroke-width=\"40\" \n"
                "        fill=\"none\" />\n",
                c.r, c.g, c.b);
    }
    for (i = 0; i < v.penStrokes.length; i++) {
        s = atIndex(&v.penStrokes, i);
        if (s->hidden) continue;
        fprintf(file, "    <polyline \n");
        fprintf(file, "        points=\"");
        for (p = 0; p < s->numPoints; p++) {
            Position *point = atIndex(&v.points, s->pointIndex + p);
            fprintf(file, 
                    "%d,%d ", 
                    (int)(point->x * scale), 
                    (int)(point->y * scale));
        }
        c = state->theme[s->color];
        fprintf(file,
                "\" \n"
                "        stroke=\"rgb(%d, %d, %d, 255)\" \n"
                "        stroke-width=\"3\" \n"
                "        fill=\"none\" />\n",
                c.r, c.g, c.b);
    }
    fprintf(file, "</svg>\n");
}

void 
exportPPM(FILE* file, State *state) 
{
    int i;
    Color c;

    // the colorgrid must be drawn by now
    drawCanvas2(state, state->colorGrid);

    fprintf(file, "P3\n");
    fprintf(file, "%d %d\n", state->width, state->height);
    fprintf(file, "255\n");
    for (i = 0; i < state->width * state->height; i++) {
        c = state->colorGrid[i];
        if (c.r + c.b + c.g == 0) {
            c = state->theme[White];
        }
        fprintf(file, "%d %d %d\n", c.r, c.g, c.b);
    }

}

void 
applyToolToPixel(int x, int y, State *state, uint16_t strokeIndex) {
    int               index, i, dy, dx, thickness;
    Color             col;
    struct Stroke     *stroke;

    index = y * state->width + x;

    int num = state->gridOfIndecies[index];
    if (inMenu
            && strokeIndex == state->history.penStrokes.length - 1
            && strokeIndex >= menuStrokesStart 
            && previousNum != strokeIndex
            && num         != 0 
            && num - 1     <  menuStrokesStart)
    {
        if (num != strokeIndex) previousNum = strokeIndex;
        // XXX handle highlighter
        state->messages |= Message_RedrawFrame;
        num = num - 1 - penStart;
        switch (num) {
        case Action_Save        :  
            state->messages |= Message_ExitMenu | Message_Save;
            break; 
        case Action_ChangeTheme :  
            state->messages |= Message_ChangeTheme;   
            break;
        case Action_UseBlack    :  
        case Action_UseRed      :
        case Action_UseGreen    :
        case Action_UseYellow   :
        case Action_UseBlue     :
        case Action_UseMagenta  :
        case Action_UseCyan     :
            enum ColorT col = num - Action_UseBlack;
            if (col == White) col = Black;
            state->colorChange = col;
            state->messages |= Message_ChangeColor;
            break;
        case Action_ExitMenu    :  
            state->messages |= Message_ExitMenu;
            break;
        case Action_Eraser      :  
            state->messages |= Message_ExitMenu | Message_ChangeTool;
            state->toolChange = Eraser;
            break;
        }
    }

    switch (state->tool) {
    case Eraser:
        i = state->gridOfIndecies[index];
        if (i == 0) return;
        i--;
        if (i & 1 << 15) {
            i &= ~(1 << 15);
            stroke = atIndex(&state->history.highlighterStrokes, i);
        }
        else stroke = atIndex(&state->history.penStrokes, i);
        stroke->hidden = true;
        break;
    case Pen:
        thickness = 3 * (float)state->width / CANVAS_WIDTH;
        col = state->theme[state->penOptions.color];
        for (dy = 0; dy < thickness; dy++) {
            for (dx = 0; dx < thickness; dx++) {
                if ( x + dx >= state->width  
                        || x + dx < 0              
                        || y + dy >= state->height 
                        || y + dy < 0
                ) continue;
                i = index + dy * state->width + dx;
                state->gridOfIndecies[i] = strokeIndex + 1;
                state->colorGrid[i] = col;
            }
        }
        break;
    case Highlighter:
        col = highligtherColor(state->theme, 
                               state->highlighterOptions.color, 
                               false);
        for (dy = 0; dy < state->height / 24; dy++) {
            if (y + dy >= state->height) continue;
            i = index + dy * state->width;
            state->colorGrid[i] = col;
            state->gridOfIndecies[i] = (strokeIndex + 1) | (1 << 15);
        }
        break;
    }
}

void
plotLineLow2(int x0, int y0, 
             int x1, int y1, 
             State *s, uint16_t index) 
{
    int dx, dy, yi, D, y, x;

    dx = x1 - x0;
    dy = y1 - y0;
    yi = 1;
    if (dy < 0) {
        yi *= -1;
        dy *= -1;
    }
    D = (2 * dy) - dx;
    y = y0;
    for (x = x0; x <= x1; x++) {
        applyToolToPixel(x, y, s, index);
        if (D > 0) {
            y += yi;
            D += 2 * (dy - dx);
        }
        else D += 2 * dy;
    }
}

void 
plotLineHigh2(int x0, int y0, 
              int x1, int y1, 
              State *s, uint16_t index) 
{
    int dx, dy, xi, D, x, y;

    dx = x1 - x0;
    dy = y1 - y0;
    xi = 1;
    if (dx < 0) {
        xi *= -1;
        dx *= -1;
    }
    D = (2 * dx) - dy;
    x = x0;
    for (y = y0; y <= y1; y++) {
        applyToolToPixel(x, y, s, index);
        if (D > 0) {
            x += xi;
            D += 2 * (dx - dy);
        }
        else D += 2 * dx;
    }
}

void 
plotLine2(int x0, int y0, 
          int x1, int y1, 
          State *s, uint16_t index) 
{
    float scalex, scaley;
    
    scalex = (float)s->width / CANVAS_WIDTH;
    scaley = (float)s->height / CANVAS_HEIGHT;
    x0 *= scalex; 
    x1 *= scalex; 
    y0 *= scaley; 
    y1 *= scaley; 
    if (abs(y1 - y0) < abs(x1 - x0)) {
        if (x0 > x1) plotLineLow2(x1, y1, x0, y0, s, index);
        else         plotLineLow2(x0, y0, x1, y1, s, index);
    }
    else {
        if (y0 > y1) plotLineHigh2(x1, y1, x0, y0, s, index);
        else         plotLineHigh2(x0, y0, x1, y1, s, index);
    }
}

void 
drawPenStrokes(State *state, Color *pixels) 
{
    int             numStrokes, i, j;
    enum ColorT     col;
    enum Tool       t;
    struct Stroke   s;
    Position        p0, p1;

    t = state->tool;
    col = state->penOptions.color;

    state->tool = Pen;
    numStrokes = state->history.penStrokes.length;           
    for (i = penStart; i < numStrokes; i++) {
        s = *(struct Stroke *) atIndex(&state->history.penStrokes, i);
        if (s.hidden || s.numPoints < 2) continue;
        state->penOptions.color = s.color;
        for (j = s.pointIndex; j < s.pointIndex + s.numPoints - 1; j++) {
            p0 = *(Position *)atIndex(&state->history.points, j);
            p1 = *(Position *)atIndex(&state->history.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }

    state->penOptions.color = col;
    state->tool = t;
}

void drawHighlighterStrokes(State *state, Color *pixels) 
{
    int             numStrokes, i, j;
    enum ColorT     col;
    enum Tool       t;
    struct Stroke   s;
    Position        p0, p1;

    t = state->tool;
    col = state->highlighterOptions.color;

    state->tool = Highlighter;
    numStrokes = state->history.highlighterStrokes.length;           
    for (i = 0; i < numStrokes; i++) {
        s = *(struct Stroke *) atIndex(&state->history.highlighterStrokes, i);
        if (s.hidden || s.numPoints < 2) continue;
        if (s.numPoints == 0) continue;
        state->highlighterOptions.color = s.color;
        for (j = s.pointIndex; j < s.pointIndex + s.numPoints - 1; j++) {
            p0 = *(Position *)atIndex(&state->history.points, j);
            p1 = *(Position *)atIndex(&state->history.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }

    state->highlighterOptions.color = col;
    state->tool = t;
}

void
drawGrid(State *state, Color *pixels) 
{
    int g, gridThickness, index, strokeIndex;
    float x, y, gridSizeX, gridSizeY;
    struct Stroke *s;
    Color bg;

    gridThickness = state->width / ((float)CANVAS_WIDTH / 3);
    gridSizeX = (float)state->width / 30;
    gridSizeY = (float)state->height / (6 * 4);

    for (y = 0; y < state->height - 1; y += gridSizeY) {
        for (x = 0; x < state->width; x++) {
            for (g = 0; g < gridThickness; g++) {
                index = ((int)y + g) * state->width + x;
                strokeIndex = state->gridOfIndecies[index];
                if (strokeIndex != 0 && !(strokeIndex & 1 << 15)) continue;
                if (strokeIndex == 0) 
                    bg = highligtherColor(state->theme, Black, false);
                else {
                    strokeIndex &= ~(1 << 15);
                    strokeIndex--;
                    s = atIndex(&state->history.highlighterStrokes, 
                                strokeIndex);
                    bg = highligtherColor(state->theme, s->color, true);
                }
                pixels[index] = bg;
            }
        }
    }

    for (y = 0; y < state->height; y +=1) {
        for (x = 0; x < state->width - 1; x += gridSizeX) {
            for (g = 0; g < gridThickness; g++) {
                index = (int)y * state->width + x + g;
                strokeIndex = state->gridOfIndecies[index];
                if (strokeIndex != 0 && !(strokeIndex & 1 << 15)) continue;
                if (strokeIndex == 0) 
                    bg = highligtherColor(state->theme, Black, false);
                else {
                    strokeIndex &= ~(1 << 15);
                    strokeIndex--;
                    s = atIndex(&state->history.highlighterStrokes, 
                                strokeIndex);
                    bg = highligtherColor(state->theme, s->color, true);
                }
                pixels[index] = bg;
            }
        }
    }
}

void 
drawCanvas2(State *state, Color *pixels) 
{
    memset(state->gridOfIndecies, 
           0, 
           state->width * state->height * sizeof(uint16_t));

    drawHighlighterStrokes(state, pixels);
    drawGrid(state, pixels);
    drawPenStrokes(state, pixels);
}

State
newState(int width, int height) 
{
    int size;
    State state;
    Color *colorGrid;
    uint16_t *gridOfIndecies;

    size           = width * height;
    colorGrid      = calloc(size, sizeof(Color));
    gridOfIndecies = calloc(size, sizeof(uint16_t));
    state          = (State) {
        .width              = CANVAS_WIDTH,
        .height             = CANVAS_HEIGHT,
        .penOptions         = { .thickness = 1, .color = Black,  },
        .highlighterOptions = { .thickness = 1, .color = Yellow, },
        .tool               = Pen,
        .theme              = rosePineDawn,
        .needsRedraw        = true,
        .markings           = Grid,
        .history            = newhistory(),
        .gridOfIndecies     = gridOfIndecies,
        .colorGrid          = colorGrid,
        .colorChange        = 0,
        .toolChange         = 0,
        .messages           = 0,
    };
    return state;
}

void 
readFile(int fd, State *state) 
{
    int i, j, k, result;
    uint16_t len, x, y, numPoints, start;
    struct Stroke s;
    enum ColorT col;
    Position p;

    DynArr *target = &state->history.highlighterStrokes;

    for (k = 0; k < 2; k++) {
        result = read(fd, &len, 2);
        for (i = 0; i < len; i++) {
            result = read(fd, &col, sizeof(enum ColorT));
            //printf("\ncol: %d %d\n\n", col, sizeof(enum ColorT));
            assert(col <= 8 && col >= 0);
            result = read(fd, &numPoints, 2);
            start = state->history.points.length;
            for (j = 0; j < numPoints; j++) {
                result = read(fd, &p, sizeof(Position));
                push(&state->history.points, &p);
            }
            s = (struct Stroke) {
                .color = col,
                .numPoints = numPoints,
                .pointIndex = start
            };
            push(target, &s);
        }
        target = &state->history.penStrokes;
    }
}

void 
writeFile(int fd, State *s) 
{
    int i, j, result, k;
    uint16_t len, x, y, totallen;
    struct Stroke *stroke;
    Position *point;

    DynArr *target = &s->history.highlighterStrokes;
    for (int k = 0; k < 2; k++) {
        len = target->length;
        totallen = len;
        for (i = 0; i < len; i++) {
            stroke = atIndex(target, i);
            if (stroke->hidden) totallen--;
        }
        result = write(fd, &totallen, sizeof(uint16_t));
        for (i = 0; i < len; i++) {
            stroke = atIndex(target, i);
            if (stroke->hidden) continue; 

            result = write(fd, &stroke->color, sizeof(enum ColorT));
            assert(result == sizeof(enum ColorT));

            result = write(fd, &stroke->numPoints, sizeof(uint16_t));
            assert(result == sizeof(uint16_t));

            for (j = 0; j < stroke->numPoints; j++) {
                point = atIndex(&s->history.points, stroke->pointIndex + j);
                result = write(fd, point, sizeof(Position));
                assert(result == sizeof(Position));
            }
        }
        target = &s->history.penStrokes;
    }
}

char* getNobookDirectory() {
    char *home, *path;

    path = calloc(100, 1);
    home = getenv("HOME");
    assert(home != NULL);
    sprintf(path, "%s/.local/share/no-book/", home);
    //free(home);
    return(path);
}

void
exitWithHelp() {
    fprintf(stderr, "Usage: no-book [OPTION]... [SAVEFILE]\n"
           "Options: \n\n"
           "    --help                   see this help message\n"
           "    --export FORMAT          where FORMAT is 'svg' or 'ppm'\n"
           "    --menu-file FILE         use savefile FILE as menu\n");
    exit(1);
}

int 
main(int argc, char **argv) 
{
    State               state;
    bool                drawingStroke;
    struct Stroke       *currentStroke;
    Image               img;
    Texture2D           texture;
    float               scalex, scaley;
    Vector2             mouse;
    char                *path, *dirend;
    enum Tool           previousTool;

    char *savefile = NULL;
    char *exportAs = NULL;
    char *menuFile = NULL;

    char *gotSaveFile = NULL;

    path = getNobookDirectory();
    dirend = path + strlen(path);

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) exitWithHelp(); 
        else if (strcmp(arg, "--export") == 0) {
            if (i + 1 >= argc) exitWithHelp();
            i++;
            arg = argv[i];
            if (strcmp(arg, "svg") != 0 && strcmp(arg, "ppm") != 0)
                exitWithHelp();
            exportAs = arg;
        }
        else if (strcmp(arg, "--menu-file") == 0) {
            if (i + 1 >= argc) exitWithHelp();
            i++;
            menuFile = argv[i];
        }
        else {
            gotSaveFile = arg;
            savefile = arg;
        }
    }

    {
        // tempMenuFile because (path) might be overwritten
        char *tempMenuFile = menuFile;
        if (menuFile == NULL) {
            strcpy(dirend, "overlay.nb");
            tempMenuFile = path;
        }
        int fd = open(tempMenuFile, O_RDONLY);
        assert(fd != -1);
        State temp = newState(0, 0);
        readFile(fd, &temp);
        menuStrokesStart = temp.history.penStrokes.length;
        other = temp.history;
        close(fd);
    }
    drawingStroke = false;
    currentStroke = NULL;
    state         = newState(CANVAS_WIDTH, CANVAS_HEIGHT);

    if (savefile != NULL) {
        int   fd   = open(savefile, O_RDONLY);
        assert(fd != -1);
        readFile(fd, &state);
        close(fd);
#if DEBUG
        fprintf(stderr, "successfully loaded!\n");
#endif
        state.messages |= Message_RedrawFrame;
    }

    if (exportAs != NULL) {
        if (gotSaveFile == NULL) {
            fprintf(stderr, "provide a savefile to export\n\n");
            exitWithHelp();
        }
        if (strcmp(exportAs, "svg") == 0)     exportSVG(stdout, &state);
        else                                  exportPPM(stdout, &state);
        exit(0);
    }

    SetTraceLogLevel(LOG_ERROR);

    InitWindow(state.width, state.height, "game");
    SetTargetFPS(120);

    img       = GenImageColor(CANVAS_WIDTH, CANVAS_HEIGHT, BLANK);
    texture   = LoadTextureFromImage(img);
    UnloadImage(img);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(state.theme[White]);

        state.needsRedraw = false;
        if (IsWindowResized()) {
            state.messages |= Message_RedrawFrame;
            state.width = GetRenderWidth();
            state.height = GetRenderHeight();
            int expectedHeight = (float)state.width * 4/5;
            if (state.height < expectedHeight) {
                int newSize = (float)state.height * 5/4;
                state.width = newSize;
            }
            else state.height = (float) state.width * 4/5;
#if DEBUG
            fprintf(stderr, "dimensions: %dx%d\n", state.width, state.height);
#endif
            int size = state.width * state.height;
            state.gridOfIndecies = realloc(state.gridOfIndecies, 
                                           size * sizeof(uint16_t));
            state.colorGrid = realloc(state.colorGrid, size * sizeof(Color));
            img = GenImageColor(state.width, state.height, BLANK);
            UnloadTexture(texture);
            texture = LoadTextureFromImage(img);
            UnloadImage(img);
        }

        scalex = (float)state.width / CANVAS_WIDTH;
        scaley = (float)state.height / CANVAS_HEIGHT;

        mouse = GetMousePosition();

        mouse.x /= scalex;
        mouse.y /= scaley;

        if (mouse.x < 0) mouse.x = 0;
        if (mouse.x >= CANVAS_WIDTH) mouse.x = CANVAS_WIDTH - 1;
        if (mouse.y < 0) mouse.y = 0;
        if (mouse.y >= CANVAS_HEIGHT) mouse.y = CANVAS_HEIGHT - 1;

        DynArr *strokeList = &state.history.penStrokes;
        if (state.tool == Highlighter) {
            strokeList  = &state.history.highlighterStrokes;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !drawingStroke) {
            drawingStroke            = true;
            enum ColorT strokeColor  = state.penOptions.color;
            if (state.tool == Highlighter) {
                strokeColor = state.highlighterOptions.color;
            }
            Position p = (Position) {mouse.x, mouse.y};
            push(&state.history.points, &p);
            struct Stroke s = (struct Stroke) {
                .numPoints       = 1,
                .pointIndex      = state.history.points.length - 1,
                .hidden          = false,
                .color           = strokeColor
            };
            push(strokeList, &s);
            currentStroke = atIndex(strokeList, strokeList->length - 1);
            state.messages |= Message_RedrawFrame;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && drawingStroke) {
            if (state.history.points.length == 0) {
                state.messages |= Message_RedrawFrame;
                goto skip;
            }
            Position previous = 
                *(Position *) atIndex(&state.history.points, 
                                      state.history.points.length - 1);
            int x0 = previous.x;
            int y0 = previous.y;
            int x1 = mouse.x;
            int y1 = mouse.y;
            int threshold = CANVAS_WIDTH / 500;
            if (abs(x0 - x1) > threshold  || abs(y0 - y1) > threshold) {
                Position p = (Position) { x1, y1 };
                if (state.tool == Eraser) {
                    plotLine2(x0, y0, x1, y1, &state, strokeList->length - 1);
                    state.messages |= Message_RedrawFrame;
                } 
                else {
                    push(&state.history.points, &p);
                    currentStroke->numPoints++;
                    if (state.tool == Pen) {
                        plotLine2(x0, y0, x1, y1, &state, strokeList->length - 1);
                    } 
                    else state.messages |= Message_RedrawFrame;
                }
                state.needsRedraw = true;
            }
        } 
        else if (drawingStroke) {
            /* stop drawing stroke */
            int numPoints = currentStroke->numPoints;
            if (numPoints == 1) {
                strokeList->length--;
                state.history.points.length--;
                state.messages |= Message_RedrawFrame;
            }
            if (state.tool == Eraser) state.tool = previousTool;
            drawingStroke = false;
            currentStroke = NULL;
        }
    skip:


        int key = GetKeyPressed();
        if (key != 0) {
            state.messages |= Message_RedrawFrame;
            switch (key) {
            case KEY_H:  state.tool = Highlighter;              break;
            case KEY_P:  state.tool = Pen;                      break;
            case KEY_T:  state.markings = None;                 break;
            case KEY_Z:  state.messages |= Message_EnterMenu;   break;
            }
        }

        struct History temp;

        if (state.messages & Message_ExitMenu) {
            if (inMenu) {
                state.history.penStrokes.length = menuStrokesStart;
                temp = state.history;
                state.history = other;
                other = temp;
                inMenu = false;
            }
            state.messages &= ~Message_ExitMenu;
            state.messages |= Message_RedrawFrame;
            drawingStroke = false;
        }

        if (state.messages & Message_ChangeColor) {
            state.messages &= ~Message_ChangeColor;
            enum ColorT *colorptr = &state.penOptions.color;
            if (state.tool == Highlighter) colorptr = &state.highlighterOptions.color;
            *colorptr = state.colorChange;
            if (drawingStroke)     currentStroke->color = state.colorChange;
        }

        if (state.messages & Message_ChangeTool) {
            state.messages &= ~Message_ChangeTool;
            previousTool = state.tool;
            state.tool = state.toolChange;
            drawingStroke = false;
        }

        if (state.messages & Message_EnterMenu) {
            if (!inMenu) {
                temp = state.history;
                state.history = other;
                other = temp;
                inMenu = true;
            }
            state.messages &= ~Message_EnterMenu;
            state.messages |= Message_RedrawFrame;
            drawingStroke = false;
        }

        if (state.messages & Message_ChangeTheme) {
            state.messages &= ~Message_ChangeTheme;
            themeIndex = (themeIndex + 1) % themeCount;
            state.theme = themes[themeIndex];
        }

        if (state.messages & Message_Save) {
            state.messages &= ~Message_Save;
            if (savefile == NULL) savefile = "out.nb";
            int fd = open(savefile, O_WRONLY | O_CREAT, 0666);
            assert(fd != -1);
            writeFile(fd, &state);
            close(fd);
            fprintf(stderr, "saved to %s\n", savefile);
        }

        if (state.needsRedraw || state.messages & Message_RedrawFrame) {
            state.needsRedraw = false;
            if (state.messages & Message_RedrawFrame) {
                state.messages &= ~Message_RedrawFrame;
#if DEBUG
                fprintf(stderr, "drew entire frame\n");
#endif
                Color c = state.theme[White];
                for (int i = 0; i < state.width * state.height; i++)
                    state.colorGrid[i] = c;
                drawCanvas2(&state, state.colorGrid);
            }
            UpdateTexture(texture, state.colorGrid);
#if DEBUG
            fprintf(stderr, "updated frame\n");
#endif
            state.needsRedraw = false;
        }

        Rectangle rect = {0, 0, state.width, state.height};
        DrawTexturePro(texture, 
                       rect, rect, 
                       (Vector2){0, 0}, 
                       0.0, WHITE);
        EndDrawing();
    }
    return 0;
}
