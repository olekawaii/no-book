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

int min(int x, int y) {
    if (x < y) return x;
    return y;
}

int max(int x, int y) {
    if (x > y) return x;
    return y;
}

#include "themes.c"

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
    uint8_t        thickness;
    enum ColorT    color;
} BrushOptions;

typedef struct {
    uint16_t       x;
    uint16_t       y;
} Position;

typedef struct {
    uint16_t       numPoints;
    uint16_t       startingPointIndex;
    bool           hidden;               // expensive
    enum ColorT    color;
} Stroke;

typedef struct {
    void         * data;
    int            length;
    int            elementSize;
    int            capacity;
} DynamicArray;

DynamicArray 
newDynamicArray(int elementSize) 
{
    return (DynamicArray) {
        .data        = calloc(4, elementSize),
        .length      = 0,
        .elementSize = elementSize,
        .capacity    = 4,
    };
}

struct History {
    DynamicArray   points;
    DynamicArray   highlighterStrokes;
    DynamicArray   penStrokes;
};

struct History 
newhistory() 
{
    return (struct History) {
        .points             = newDynamicArray(sizeof(Position)),
        .highlighterStrokes = newDynamicArray(sizeof(Stroke)),
        .penStrokes         = newDynamicArray(sizeof(Stroke))
    };
}

void 
push(DynamicArray *d, void *value) 
{
    if (d->length == d->capacity) {
        d->data = realloc(d->data, 2 * d->elementSize * d->capacity);
        d->capacity *= 2;
    }
    memcpy(d->data + d->elementSize * d->length, value, d->elementSize);
    d->length++;
}

void *
atIndex(DynamicArray *d, int index) 
{
    return d->data + index * d->elementSize;
}

void 
gridToSvg(FILE* file, struct History v) 
{
    fprintf(file,
        "<svg \n"
        "    width=\"%d\" \n"
        "    height=\"%d\" \n"
        "    viewBox=\"0 0 %d %d\" \n"
        "    xmlns=\"http://www.w3.org/2000/svg\" \n"
        "    xmlns:xlink=\"http://www.w3.org/1999/xlink\" >\n",
        CANVAS_WIDTH,
        CANVAS_HEIGHT,
        CANVAS_WIDTH,
        CANVAS_HEIGHT
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

typedef struct {
    int                width;
    int                height;
    BrushOptions       penOptions;
    BrushOptions       highlighterOptions;
    enum Tool          tool;
    Color            * theme;
    bool               needsRedraw;
    enum Markings      markings;
    struct History     history;
    uint16_t         * gridOfIndecies;
    Color            * colorGrid;
} State;

void 
applyToolToPixel2(int x, int y, State *state, uint16_t strokeIndex) {
    enum ColorT col;
    float scalex = (float)state->width / (float)CANVAS_WIDTH;
    float scaley = (float)state->height / (float)CANVAS_HEIGHT;

    int index = y * state->width + x;
    switch (state->tool) {
    case Eraser:
        int i = state->gridOfIndecies[index];
        if (i == 0) return;
        printf("erased\n");
        i--;
        Stroke * stroke;
        if (i & 1 << 15) {
            i &= ~(1 << 15);
            stroke = atIndex(&state->history.highlighterStrokes, i);
        }
        else {
            stroke = atIndex(&state->history.penStrokes, i);
        }
        stroke->hidden = true;
        break;
    case Pen:
        col = state->penOptions.color;
        for (int dy = -1; dy < 2; dy++)
            for (int dx = -1; dx < 2; dx++) {
                if (
                    x + dx >= state->width  || 
                    x + dx < 0              || 
                    y + dy >= state->height ||
                    y + dy < 0
                ) continue;
                int i = index + dy * state->width + dx;
                state->gridOfIndecies[i] = strokeIndex + 1;
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
            state->gridOfIndecies[i] = (strokeIndex + 1) | (1 << 15);
        }
        break;
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
plotLine2(int x0, int y0, int x1, int y1, State *s, uint16_t index) {
    float scalex = (float)s->width / CANVAS_WIDTH;
    float scaley = (float)s->height / CANVAS_HEIGHT;
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
drawCanvas2(State *state, Color *pixels) {
    enum ColorT hc, pc;
    int gridSize = state->width / 30;
    Color bg = state->theme[Black];
    Color w = state->theme[White];
    memset(
        state->gridOfIndecies, 
        0, 
        state->width * state->height * sizeof(uint16_t)
    );
    float opacity = HIGHLIGHTER_OPACITY;
    bg.r = (bg.r * (1.0 - opacity) + w.r * opacity);
    bg.g = (bg.g * (1.0 - opacity) + w.g * opacity);
    bg.b = (bg.b * (1.0 - opacity) + w.b * opacity);
    for (int y = 0; y < state->height - 1; y += gridSize) {
        for (int x = 0; x < state->width; x++) {
            pixels[y       * state->width + x] = bg;
            pixels[(y + 1) * state->width + x] = bg;
        }
    }
    for (int x = 0; x < state->width - 1; x += gridSize) {
        for (int y = 0; y < state->height; y++) {
            pixels[y * state->width + x    ] = bg;
            pixels[y * state->width + x + 1] = bg;
        }
    }
    enum Tool t = state->tool;
    hc = state->highlighterOptions.color;
    pc = state->penOptions        .color;
    int numStrokes = state->history.highlighterStrokes.length;           
    state->tool = Highlighter;
    for (int i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->history.highlighterStrokes, i);
        if (s.hidden) continue;
        if (s.numPoints == 0) continue;
        state->highlighterOptions.color = s.color;
        for (int j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->history.points, j);
            Position p1 = *(Position *)atIndex(&state->history.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }
    state->tool = Pen;
    numStrokes = state->history.penStrokes.length;           
    for (int i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->history.penStrokes, i);
        if (s.hidden) continue;
        if (s.numPoints == 0) continue;
        state->penOptions.color = s.color;
        for (int j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->history.points, j);
            Position p1 = *(Position *)atIndex(&state->history.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }
    state->tool = t;
    state->highlighterOptions.color = hc;
    state->penOptions.color = pc;
}

void updatePenStrokes(State *state, Color *pixels) 
{
    int numStrokes, i, j;
    enum ColorT pc;
    enum Tool t;

    t = state->tool;
    pc = state->penOptions.color;

    state->tool = Pen;
    numStrokes = state->history.penStrokes.length;           
    for (i = 0; i < numStrokes; i++) {
        Stroke s = *(Stroke *) atIndex(&state->history.penStrokes, i);
        if (s.numPoints == 0) continue;
        state->penOptions.color = s.color;
        for (j = s.startingPointIndex; j < s.startingPointIndex + s.numPoints - 1; j++) {
            Position p0 = *(Position *)atIndex(&state->history.points, j);
            Position p1 = *(Position *)atIndex(&state->history.points, j + 1);
            plotLine2(p0.x, p0.y, p1.x, p1.y, state, i);
        }
    }

    state->penOptions.color = pc;
    state->tool = t;
}

int 
main() {
    Color *colorGrid;
    uint16_t *gridOfIndecies;
    State state;
    bool drawingStroke, needsFullRedraw;
    Stroke *currentStroke;
    Image img;
    Texture2D texture;

    drawingStroke = needsFullRedraw = false;
    currentStroke = NULL;
    colorGrid = calloc(CANVAS_WIDTH * CANVAS_HEIGHT, sizeof(Color));
    gridOfIndecies = calloc(CANVAS_WIDTH * CANVAS_WIDTH, sizeof(uint16_t));

    state = (State) {
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
        .colorGrid          = colorGrid
    };

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(state.width, state.height, "game");
    SetTargetFPS(30);

    //Vector2   previousMouse = GetMousePosition();

    img       = GenImageColor(CANVAS_WIDTH, CANVAS_HEIGHT, BLANK);
    texture   = LoadTextureFromImage(img);
    UnloadImage(img);

    while (!WindowShouldClose()) {
        BeginDrawing();
        //printf("fps: %d\n", GetFPS());
        ClearBackground(state.theme[White]);

        state.needsRedraw = false;
        needsFullRedraw = false;

        if (IsWindowResized()) {
            needsFullRedraw = true;
            state.width = GetRenderWidth();
            state.height = GetRenderHeight();
            printf("dimensions: %dx%d\n", state.width, state.height);
            int size = state.width * state.height;
            state.gridOfIndecies = realloc( state.gridOfIndecies, 
                                            size * sizeof(uint16_t));
            state.colorGrid = realloc( state.colorGrid, 
                                       size * sizeof(Color));
            img       = GenImageColor(state.width, state.height, BLANK);
            UnloadTexture(texture);
            texture   = LoadTextureFromImage(img);
            UnloadImage(img);
        }

        float scalex = (float)state.width / CANVAS_WIDTH;
        float scaley = (float)state.height / CANVAS_HEIGHT;

        Vector2 mouse = GetMousePosition();

        mouse.x /= scalex;
        mouse.y /= scaley;

        if (mouse.x < 0) mouse.x = 0;
        if (mouse.x >= CANVAS_WIDTH) mouse.x = CANVAS_WIDTH - 1;
        if (mouse.y < 0) mouse.y = 0;
        if (mouse.y >= CANVAS_HEIGHT) mouse.y = CANVAS_HEIGHT - 1;

        DynamicArray *strokeList = &state.history.penStrokes;
        if (state.tool == Highlighter) {
            strokeList  = &state.history.highlighterStrokes;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !drawingStroke) {
            // begin new stroke
            drawingStroke            = true;
            enum ColorT strokeColor  = state.penOptions.color;
            if (state.tool == Highlighter) {
                strokeColor = state.highlighterOptions.color;
            }
            Position p = (Position) {mouse.x, mouse.y};
            push(&state.history.points, &p);
            Stroke s = (Stroke) {
                .numPoints                = 1,
                .startingPointIndex       = state.history.points.length - 1,
                .hidden                   = false,
                .color                    = strokeColor
            };
            push(strokeList, &s);
            currentStroke = (Stroke *) strokeList->data + strokeList->length - 1;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && drawingStroke) {
            Position previous = ((Position *) state
                .history
                .points
                .data) [state.history.points.length - 1];
            int x0 = previous.x;
            int y0 = previous.y;
            int x1 = mouse.x;
            int y1 = mouse.y;
            int threshold = CANVAS_WIDTH / 500;
            if (abs(x0 - x1) > threshold  || abs(y0 - y1) > threshold) {
                // if (((Stroke *) atIndex(strokeList, strokeList-> length -1))->numPoints == 0) {
                //     Position p = (Position) { mouse.x, mouse.y };
                //     push(&state.history.points, &p);
                //     previous = p;
                //     x0 = previous.x;
                //     y0 = previous.y;
                // }
                Position p = (Position) { x1, y1 };
                if (state.tool == Eraser) {
                    plotLine2(x0, y0, x1, y1, &state, strokeList->length - 1);
                    needsFullRedraw = true;
                } 
                else {
                    push(&state.history.points, &p);
                    currentStroke->numPoints++;
                    if (state.tool == Pen) {
                        plotLine2(x0, y0, x1, y1, &state, strokeList->length - 1);
                    } else needsFullRedraw = true;
                }
                state.needsRedraw = true;
            }
        } else {
            drawingStroke = false;
            currentStroke = NULL;
        }

        enum ColorT *colorptr = &state.penOptions.color;
        if (state.tool == Highlighter) colorptr = &state.highlighterOptions.color;

        int key = GetKeyPressed();
        if (key != 0) {
            needsFullRedraw = true;
            switch (key) {
            case KEY_ZERO:     *colorptr = White;                   break;
            case KEY_ONE:      *colorptr = Red;                     break;
            case KEY_TWO:      *colorptr = Green;                   break;
            case KEY_THREE:    *colorptr = Yellow;                  break;
            case KEY_FOUR:     *colorptr = Blue;                    break;
            case KEY_FIVE:     *colorptr = Magenta;                 break;
            case KEY_SIX:      *colorptr = Cyan;                    break;
            case KEY_SEVEN:    *colorptr = Black;                   break;

            case KEY_H:        state.tool = Highlighter;            break;
            case KEY_P:        state.tool = Pen;                    break;
            case KEY_E:        state.tool = Eraser;                 break;
                            
            case KEY_T:        state.markings = None;               break;
                            
            case KEY_J:        state.theme = kanagawaLotus;         break;
            case KEY_M:        state.theme = gruvboxMaterialLight;  break;
            case KEY_L:        state.theme = rosePineDawn;          break;
            case KEY_K:        state.theme = everforestLight;       break;
            case KEY_N:        state.theme = flexokiLight;          break;
            }
        }

        if (state.needsRedraw || needsFullRedraw) {
            ClearBackground(state.theme[White]);
            if (needsFullRedraw) {
                printf("drew entire frame\n");
                memset( state.colorGrid, 
                        0, 
                        state.width * state.height * sizeof(Color));
                drawCanvas2(&state, state.colorGrid);
            }
            UpdateTexture(texture, state.colorGrid);
            printf("updated frame\n");
            state.needsRedraw = false;
        }
        // previousMouse = mouse;
        DrawTexturePro(
            texture,
            (Rectangle){0, 0, state.width, state.height},
            (Rectangle){0, 0, state.width, state.height},
            (Vector2){0, 0}, 
            0.0, 
            WHITE
        );
        EndDrawing();
    }
    FILE* f = fopen("uwu.svg", "w");
    gridToSvg(f, state.history);
    CloseWindow();
    free(state.colorGrid);
    return 0;
}
