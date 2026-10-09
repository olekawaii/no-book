#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <inttypes.h>

#include <raylib.h>

#define CANVAS_WIDTH          1280
#define CANVAS_HEIGHT         1024
#define HIGHLIGHTER_OPACITY   0.9

typedef enum {
    White      = 0, /* background */
    Red        = 1,
    Green      = 2,
    Yellow     = 3,
    Blue       = 4,
    Magenta    = 5,
    Cyan       = 6,
    Black      = 7,
} ColorT;

int min(int x, int y) {
    if (x < y) return x;
    return y;
}

int max(int x, int y) {
    if (x > y) return x;
    return y;
}

#include "themes.c"

/*
 *  bits:
 *       1-3   fg color 
 *       4-6   bg color 
 *       7     // unused
 *       8     selected
 */

typedef uint8_t Pixel;

typedef enum {
    Lines,
    Grid,
    Dots,
    None,
} Markings;

typedef enum {
    Pen,
    Highlighter,
    Eraser,
} Tool;

typedef struct {
    uint8_t         thickness;
    ColorT          color;
} BrushOptions;

typedef struct {
    uint16_t       x, y;
} Position;

typedef struct {
    uint16_t       numPoints;
    uint16_t       startingPointIndex;
    bool           hidden;               // expensive
    ColorT         color;
} Stroke;

typedef struct {
    void         * data;
    int            length;
    int            elementSize;
    int            capacity;
} DynamicArray;

DynamicArray 
newDynamicArray(int elementSize) {
    return (DynamicArray) {
        .data        = calloc(4, elementSize),
        .length      = 0,
        .elementSize = elementSize,
        .capacity    = 4,
    };
}

typedef struct {
    DynamicArray   points;
    DynamicArray   highlighterStrokes;
    DynamicArray   penStrokes;
} StrokeHistory;

StrokeHistory 
newStrokeHistory() {
    return (StrokeHistory) {
        .points             = newDynamicArray(sizeof(Position)),
        .highlighterStrokes = newDynamicArray(sizeof(Stroke)),
        .penStrokes         = newDynamicArray(sizeof(Stroke))
    };
}

void 
push(DynamicArray *d, void *value) {
    if (d->length == d->capacity) {
        d->data = realloc(d->data, 2 * d->elementSize * d->capacity);
        d->capacity *= 2;
    }
    memcpy(d->data + d->elementSize * d->length, value, d->elementSize);
    d->length++;
}

void *
atIndex(DynamicArray *d, int index) {
    return d->data + index * d->elementSize;
}

// void drawStrokes(
//     StrokeHistory   *strokes, 
//     Color           *colorGrid, 
//     uint16_t        *indexGrid
// ) {
//     
// }

void 
gridToSvg(FILE* file, StrokeHistory v) {
    fprintf(file,
        "<svg \n"
        "    width=\"391\" \n"
        "    height=\"391\" \n"
        "    viewBox=\"-10 -10 500 500\" \n"
        "    xmlns=\"http://www.w3.org/2000/svg\" \n"
        "    xmlns:xlink=\"http://www.w3.org/1999/xlink\" >\n"
    );
    // printf("    <rect \n");
    // printf("        fill=\"#fff\" \n");
    // printf("        stroke=\"#000\" \n");
    // printf("        x=\"-70\" \n");
    // printf("        y=\"-70\" \n");
    // printf("        width=\"390\" \n");
    // printf("        height=\"390\" />\n");
    for (int i = 0; i < v.penStrokes.length; i++) {
        Stroke s = *(Stroke*)atIndex(&v.penStrokes, i);
        if (s.hidden) continue;
        fprintf(file, "    <polyline \n");
        fprintf(file, "        points=\"");
        for (int p = 0; p < s.numPoints; p++) {
            Position point = *(Position*)atIndex(&v.points, s.startingPointIndex + p);
            fprintf(file, "%d,%d ", point.x, point.y);
        }
        fprintf(file,
            "\" \n"
            "        stroke=\"red\" \n"
            "        stroke-width=\"4\" \n"
            "        fill=\"none\" />\n"
        );
    }
    fprintf(file, "</svg>\n");
}

void test() {
    Position points[2] = { { 0, 0 }, { 250, 250 } };
    DynamicArray dyn = newDynamicArray(sizeof(Position));
    for (int i = 0; i < 2; i++) {
        push(&dyn, points + i);
    }
    Stroke strokes[1] = { { 2, 0, false, Red } };
    DynamicArray dyn2 = newDynamicArray(sizeof(Stroke));
    for (int i = 0; i < 1; i++) {
        push(&dyn2, strokes + i);
    }
    StrokeHistory testgrid = {
        .points = dyn,
        .highlighterStrokes = newDynamicArray(sizeof(Stroke)),
        .penStrokes = dyn2,
    };
    gridToSvg(stdout, testgrid);
}

typedef struct {
    int             width;
    int             height;
    Pixel         * grid;
    BrushOptions    penOptions;
    BrushOptions    highlighterOptions;
    Tool            tool;
    Color         * theme;
    bool            needsRedraw;
    Markings        markings;
    StrokeHistory   strokeHistory;
    uint16_t      * gridOfIndecies;
    Color         * colorGrid;
} State;

ColorT
getFgColor(Pixel p) {
    return p >> 5; 
}

ColorT
getBgColor(Pixel p) {
    return p >> 2 & ~(7 << 3); 
}

void
setFgColor(Pixel *p, ColorT c) {
    *p = (~(7 << 5) & *p) | (c << 5);
}

void
setBgColor(Pixel *p, ColorT c) {
    *p = (~(7 << 2) & *p) | (c << 2);
}

Color 
pixelColor(Pixel p, Color *theme) {
    ColorT fg = getFgColor(p);
    if (fg == White) {
        Color bg = theme[getBgColor(p)];
        float opacity = HIGHLIGHTER_OPACITY;
        bg.r = (bg.r * (1.0 - opacity) + theme[White].r * opacity);
        bg.g = (bg.g * (1.0 - opacity) + theme[White].g * opacity);
        bg.b = (bg.b * (1.0 - opacity) + theme[White].b * opacity);
        return bg;
    }
    else return theme[fg];
}

void 
drawCanvas(State *state, Color *pixels) {
    for (int y = 0; y < state->height; y++) {
        for (int x = 0; x < state->width; x++) {
            Pixel p = state->grid[(y * state->width) + x];
            Color col = pixelColor(p, state->theme);
            if (getFgColor(p) == White) {
                int gridSize = state->width / 30;
                int border   = state->width / 60;
                if (
                        state->markings == Grid &&
                        (((y % gridSize == 0 || x % gridSize == 0) ||
                        ((y + 1) % gridSize == 0 || (x + 1) % gridSize == 0)) &&
                        x > border && x < state->width  - border && 
                        y > border && y < state->height - border)
                ) {
                    Color c       = state->theme[White];
                    Color b       = state->theme[Black];
                    float opacity = HIGHLIGHTER_OPACITY;
                    if (getBgColor(p) != White) {
                        opacity -= 0.2;
                        b        = state->theme[getBgColor(p)];
                    }
                    b.r = (b.r * (1.0 - opacity) + state->theme[White].r * opacity);
                    b.g = (b.g * (1.0 - opacity) + state->theme[White].g * opacity);
                    b.b = (b.b * (1.0 - opacity) + state->theme[White].b * opacity);
                    col = b;
                }
            }
            pixels[y * state->width + x] = col;
        }
    }
}

void 
drawRect(int x, int y, int width, int height, Color *pixels, Color col) {
    for (int ny = y; ny < min(y + height, CANVAS_HEIGHT); ny++) {
        for (int nx = x; nx < min(x + width, CANVAS_WIDTH); nx++) {
            pixels[ny * CANVAS_WIDTH + nx] = col;
        }
    }
}

void 
eraseLine(State *state, int x, int y) {
    int index = y * state->width + x;
    Pixel pixel = state->grid[index];
    bool isDeletingForeground;
    if      (getFgColor(pixel) != White)  isDeletingForeground = true;
    else if (getBgColor(pixel) != White)  isDeletingForeground = false;
    else                                  return;
    int bufferCapacity = (state->width * state->height);
    uint32_t *buffer = malloc(bufferCapacity * sizeof(uint32_t));
    buffer[0] = index;
    int bufferSize = 1;
    while (true) {
        if (bufferSize == 0) break;
        bufferSize--;
        int index = buffer[bufferSize];
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int index2 = index + dy * state->width + dx;
                Pixel p = state->grid[index2];
                if (isDeletingForeground) {
                    ColorT col = getFgColor(p);
                    if (col == White) continue;
                    setFgColor(state->grid + index, White);
                }
                else {
                    ColorT col = getBgColor(p);
                    if (col == White) continue;
                    setBgColor(state->grid + index, White);
                }
                buffer[bufferSize]   = index2;
                bufferSize++;
            }
        }
    }
    free(buffer);
}

void 
applyToolToPixel(int x, int y, State *state) {
    int index = y * state->width + x;
    switch (state->tool) {
    case Eraser:
        eraseLine(state, x, y);
        break;
    case Pen:
        for (int dy = -1; dy < 2; dy++)
            for (int dx = -1; dx < 2; dx++) {
                if (
                    x + dx >= state->width  || 
                    x + dx < 0              || 
                    y + dy >= state->height ||
                    y + dy < 0
                ) continue;
                setFgColor(
                    &state->grid[index + dy * state->width + dx], 
                    state->penOptions.color
                );
            }
        break;
    case Highlighter:
        for (int i = 0; i < state->width / 30; i++) {
            if (y + i > state->height) break;
            setBgColor(
                &state->grid[index + i * state->width], 
                state->highlighterOptions.color
            );
        }
        break;
    }
}

void 
applyToolToPixel2(int x, int y, State *state, uint16_t strokeIndex) {
    int index = y * state->width + x;
    switch (state->tool) {
    // case Eraser:
    //     eraseLine(state, x, y);
    //     break;
    case Pen:
        ColorT col = state->penOptions.color;
        for (int dy = -1; dy < 2; dy++)
            for (int dx = -1; dx < 2; dx++) {
                if (
                    x + dx >= state->width  || 
                    x + dx < 0              || 
                    y + dy >= state->height ||
                    y + dy < 0
                ) continue;
                int i = index + dy * state->width + dx;
                state->colorGrid[i] = state->theme[col];
            }
        break;
    case Highlighter:
        Color bg = state->theme[state->highlighterOptions.color];
        Color w = state->theme[White];
        float opacity = HIGHLIGHTER_OPACITY;
        bg.r = (bg.r * (1.0 - opacity) + w.r * opacity);
        bg.g = (bg.g * (1.0 - opacity) + w.g * opacity);
        bg.b = (bg.b * (1.0 - opacity) + w.b * opacity);
        Color gridbg = state->theme[state->highlighterOptions.color];
        opacity -= 0.2;
        gridbg.r = (gridbg.r * (1.0 - opacity) + w.r * opacity);
        gridbg.g = (gridbg.g * (1.0 - opacity) + w.g * opacity);
        gridbg.b = (gridbg.b * (1.0 - opacity) + w.b * opacity);
        for (int dy = 0; dy < state->width / 30; dy++) {
            int newX = x;
            int newY = y + dy;
            if (
                newX >= state->width  || 
                newY >= state->height
            ) continue;
            int gridSize = state->width / 30;
            Color b = bg;
            if (
                    state->markings == Grid &&
                    (((newY % gridSize == 0 || newX % gridSize == 0) ||
                    ((newY - 1) % gridSize == 0 || (newX - 1) % gridSize == 0)))
            ) {
                b = gridbg;
            }
            int i = index + dy * state->width ;
            state->colorGrid[i] = b;
        }
        break;
    // case Highlighter:
    //     for (int i = 0; i < state->width / 30; i++) {
    //         if (y + i > state->height) break;
    //         setBgColor(
    //             &state->grid[index + i * state->width], 
    //             state->highlighterOptions.color
    //         );
    //     }
    //     break;
    }
}

void 
plotLineLow(int x0, int y0, int x1, int y1, State *s) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int yi = 1;
    if (dy < 0) {
        yi *= -1;
        dy *= -1;
    }
    int D = (2 * dy) - dx;
    int y = y0;
    for (int x = x0; x <= x1; x++) {
        applyToolToPixel(x, y, s);
        if (D > 0) {
            y += yi;
            D += 2 * (dy - dx);
        }
        else D += 2 * dy;
    }
}

void
plotLineLow2(int x0, int y0, int x1, int y1, State *s, uint16_t index) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int yi = 1;
    if (dy < 0) {
        yi *= -1;
        dy *= -1;
    }
    int D = (2 * dy) - dx;
    int y = y0;
    for (int x = x0; x <= x1; x++) {
        applyToolToPixel2(x, y, s, index);
        if (D > 0) {
            y += yi;
            D += 2 * (dy - dx);
        }
        else D += 2 * dy;
    }
}

void 
plotLineHigh(int x0, int y0, int x1, int y1, State *s) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int xi = 1;
    if (dx < 0) {
        xi *= -1;
        dx *= -1;
    }
    int D = (2 * dx) - dy;
    int x = x0;
    for (int y = y0; y <= y1; y++) {
        applyToolToPixel(x, y, s);
        if (D > 0) {
            x += xi;
            D += 2 * (dx - dy);
        }
        else D += 2 * dx;
    }
}

void 
plotLineHigh2(int x0, int y0, int x1, int y1, State *s, uint16_t index) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int xi = 1;
    if (dx < 0) {
        xi *= -1;
        dx *= -1;
    }
    int D = (2 * dx) - dy;
    int x = x0;
    for (int y = y0; y <= y1; y++) {
        applyToolToPixel2(x, y, s, index);
        if (D > 0) {
            x += xi;
            D += 2 * (dx - dy);
        }
        else D += 2 * dx;
    }
}

void 
plotLine(int x0, int y0, int x1, int y1, State *s) {
    if (abs(y1 - y0) < abs(x1 - x0)) {
        if (x0 > x1) plotLineLow(x1, y1, x0, y0, s);
        else         plotLineLow(x0, y0, x1, y1, s);
    }
    else {
        if (y0 > y1) plotLineHigh(x1, y1, x0, y0, s);
        else         plotLineHigh(x0, y0, x1, y1, s);
    }
}

void 
plotLine2(int x0, int y0, int x1, int y1, State *s, uint16_t index) {
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
drawCanvas2(State *state, Color *pixels) {
    int gridSize = state->width / 30;
    Color bg = state->theme[Black];
    Color w = state->theme[White];
    float opacity = HIGHLIGHTER_OPACITY;
    bg.r = (bg.r * (1.0 - opacity) + w.r * opacity);
    bg.g = (bg.g * (1.0 - opacity) + w.g * opacity);
    bg.b = (bg.b * (1.0 - opacity) + w.b * opacity);
    for (int y = 0; y < state->height + 1; y += gridSize) {
        for (int x = 0; x < state->width; x++) {
            pixels[y       * state->width + x] = bg;
            pixels[(y + 1) * state->width + x] = bg;
        }
    }
    for (int x = 0; x < state->width + 1; x += gridSize) {
        for (int y = 0; y < state->height; y++) {
            pixels[y * state->width + x    ] = bg;
            pixels[y * state->width + x + 1] = bg;
        }
    }
    Tool t = state->tool;
    ColorT hc = state->highlighterOptions.color;
    ColorT pc = state->penOptions        .color;
    int numStrokes = state->strokeHistory.highlighterStrokes.length;           
    state->tool = Highlighter;
    for (int i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->strokeHistory.highlighterStrokes, i);
        if (s.numPoints == 0) continue;
        state->highlighterOptions.color = s.color;
        for (int j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->strokeHistory.points, j);
            Position p1 = *(Position *)atIndex(&state->strokeHistory.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }
    state->tool = Pen;
    numStrokes = state->strokeHistory.penStrokes.length;           
    for (int i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->strokeHistory.penStrokes, i);
        if (s.numPoints == 0) continue;
        state->penOptions.color = s.color;
        for (int j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->strokeHistory.points, j);
            Position p1 = *(Position *)atIndex(&state->strokeHistory.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }
    state->tool = t;
    state->highlighterOptions.color = hc;
    state->penOptions.color = pc;
}

void updatePenStrokes(State *state, Color *pixels) {
    Tool t = state->tool;
    ColorT pc = state->penOptions.color;

    state->tool = Pen;
    int numStrokes = state->strokeHistory.penStrokes.length;           
    for (int i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->strokeHistory.penStrokes, i);
        if (s.numPoints == 0) continue;
        state->penOptions.color = s.color;
        for (int j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->strokeHistory.points, j);
            Position p1 = *(Position *)atIndex(&state->strokeHistory.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }

    state->penOptions.color = pc;
    state->tool = t;
}

// void updateHighlighterStrokes(State *state, Color *pixels) {
//     Tool t = state->tool;
//     ColorT pc = state->highlighterOptions.color;
// 
//     state->penOptions.color = pc;
//     state->tool = t;
// }


int 
main() {
    test();
    Color    *colorGrid = malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(Color));
    State state = {
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
        .grid = calloc(CANVAS_WIDTH * CANVAS_HEIGHT, sizeof(Pixel)),
        .penOptions = {
            .thickness = 1,
            .color = Black,
        },
        .highlighterOptions = {
            .thickness = 1,
            .color = Yellow,
        },
        .tool = Pen,
        .theme = rosePineDawn,
        .needsRedraw = true,
        .markings = Grid,
        .strokeHistory = newStrokeHistory(),
        .gridOfIndecies = calloc(CANVAS_WIDTH * CANVAS_WIDTH, sizeof(uint16_t)),
        .colorGrid = colorGrid
    };
    float scale = 1;
    InitWindow(state.width * scale, state.height * scale, "game");
    SetTargetFPS(120);

    Vector2   previousMouse = GetMousePosition();

    Image     img       = GenImageColor(CANVAS_WIDTH, CANVAS_HEIGHT, BLANK);
    Texture2D texture   = LoadTextureFromImage(img);
    UnloadImage(img);
    bool drawingStroke = false;
    Stroke* currentStroke = NULL;
    bool needsFullRedraw = false;

    while (!WindowShouldClose()) {
        BeginDrawing();
        printf("fps: %d\n", GetFPS());
        ClearBackground(state.theme[White]);

        state.needsRedraw = true;
        needsFullRedraw = false;

        Vector2 mouse = GetMousePosition();
        mouse.x /= scale;
        mouse.y /= scale;
        if (mouse.x < 0) mouse.x = 0;
        if (mouse.x >= state.width) mouse.x = state.width - 1;
        if (mouse.y < 0) mouse.y = 0;
        if (mouse.y >= state.width) mouse.y = state.height - 1;

        DynamicArray *strokeList = &state.strokeHistory.penStrokes;
        if (state.tool == Highlighter) {
            strokeList  = &state.strokeHistory.highlighterStrokes;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !drawingStroke) {
            // begin new stroke
            drawingStroke            = true;
            ColorT strokeColor       = state.penOptions.color;
            if (state.tool == Highlighter) {
                strokeColor = state.highlighterOptions.color;
            }
            Stroke s = (Stroke) {
                .numPoints                = 0,
                .startingPointIndex       = state.strokeHistory.points.length,
                .hidden                   = false,
                .color                    = strokeColor
            };
            push(strokeList, &s);
            currentStroke = (Stroke *) strokeList->data + strokeList->length - 1;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && drawingStroke) {
            Position previous = ((Position *) state
                .strokeHistory
                .points
                .data) [state.strokeHistory.points.length - 1];
            int x0 = previous.x;
            int y0 = previous.y;
            int x1 = mouse.x;
            int y1 = mouse.y;
            int threshold = CANVAS_WIDTH / 500;
            if (abs(x0 - x1) > threshold  || abs(y0 - y1) > threshold) {
                if (((Stroke *) atIndex(strokeList, strokeList-> length -1))->numPoints == 0) {
                    Position p = (Position) { mouse.x, mouse.y };
                    push(&state.strokeHistory.points, &p);
                    previous = p;
                    x0 = previous.x;
                    y0 = previous.y;
                }
                Position p = (Position) { x1, y1 };
                push(&state.strokeHistory.points, &p);
                currentStroke->numPoints++;
                if (state.tool == Pen) {
                    plotLine2(x0, y0, x1, y1, &state, strokeList->length - 1);
                } else needsFullRedraw = true;
                state.needsRedraw = true;
            }
        } else {
            drawingStroke = false;
            currentStroke = NULL;
        }

        ColorT *colorptr = &state.penOptions.color;
        if (state.tool == Highlighter) colorptr = &state.highlighterOptions.color;

        if       (IsKeyPressed(KEY_ZERO  ))   *colorptr = White;
        else if  (IsKeyPressed(KEY_ONE   ))   *colorptr = Red;
        else if  (IsKeyPressed(KEY_TWO   ))   *colorptr = Green;
        else if  (IsKeyPressed(KEY_THREE ))   *colorptr = Yellow;
        else if  (IsKeyPressed(KEY_FOUR  ))   *colorptr = Blue;
        else if  (IsKeyPressed(KEY_FIVE  ))   *colorptr = Magenta;
        else if  (IsKeyPressed(KEY_SIX   ))   *colorptr = Cyan;
        else if  (IsKeyPressed(KEY_SEVEN ))   *colorptr = Black;
        else if  (IsKeyPressed(KEY_H     ))   state.tool = Highlighter;
        else if  (IsKeyPressed(KEY_P     ))   state.tool = Pen;
        else if  (IsKeyPressed(KEY_E     ))   state.tool = Eraser;
        else if  (IsKeyPressed(KEY_T     ))   state.markings = None;
        else if  (IsKeyPressed(KEY_J     ))   state.theme = kanagawaLotus;
        else if  (IsKeyPressed(KEY_M     ))   state.theme = gruvboxMaterialLight;
        else if  (IsKeyPressed(KEY_L     ))   state.theme = rosePineDawn;
        else if  (IsKeyPressed(KEY_K     ))   state.theme = everforestLight;
        else if  (IsKeyPressed(KEY_N     ))   state.theme = flexokiLight;
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { }
        //     int x0 = previousMouse.x;
        //     int y0 = previousMouse.y;
        //     int x1 = mouse.x;
        //     int y1 = mouse.y;
        //     plotLine(x0, y0, x1, y1, &state);
        //     state.needsRedraw = true;
        // }
        else state.needsRedraw = false;

        if (state.needsRedraw || needsFullRedraw) {
            ClearBackground(state.theme[White]);
            if (needsFullRedraw) {
                printf("drew entire frame\n");
                memset(colorGrid, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(Color));
                drawCanvas2(&state, colorGrid);
            }
            UpdateTexture(texture, colorGrid);
            printf("updated frame\n");
            state.needsRedraw = false;
        }
        previousMouse = mouse;
        DrawTexturePro(
            texture,
            (Rectangle){0, 0, CANVAS_WIDTH, CANVAS_HEIGHT},
            (Rectangle){0, 0, CANVAS_WIDTH * scale, CANVAS_HEIGHT * scale},
            (Vector2){0, 0}, 
            0.0, 
            WHITE
        );
        EndDrawing();
    }
    FILE* f = fopen("uwu.svg", "w");
    gridToSvg(f, state.strokeHistory);
    CloseWindow();
    free(state.grid);
    free(colorGrid);
    return 0;
}
