#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <inttypes.h>

#include <raylib.h>

#define CANVAS_WIDTH          1200
#define CANVAS_HEIGHT         900
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
    int             thickness;
    ColorT          color;
} BrushOptions;

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

int min(int x, int y) {
    if (x < y) return x;
    return y;
}

int max(int x, int y) {
    if (x > y) return x;
    return y;
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
                    setFgColor(&state->grid[index], White);
                }
                else {
                    ColorT col = getBgColor(p);
                    if (col == White) continue;
                    setBgColor(&state->grid[index], White);
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

int 
main() {
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
    };
    float scale = 1;
    InitWindow(state.width * scale, state.height * scale, "game");
    SetTargetFPS(60);
    Color    *colorGrid = malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(Color));

    Vector2   previousMouse = GetMousePosition();

    Image     img       = GenImageColor(CANVAS_WIDTH, CANVAS_HEIGHT, BLANK);
    Texture2D texture   = LoadTextureFromImage(img);
    UnloadImage(img);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(state.theme[White]);

        state.needsRedraw = true;

        Vector2 mouse = GetMousePosition();
        mouse.x /= scale;
        mouse.y /= scale;

        ColorT *colorptr = &state.penOptions.color;
        if (state.tool == Highlighter) colorptr = &state.highlighterOptions.color;

        if       (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) printf("67\n");
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
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            int x0 = previousMouse.x;
            int y0 = previousMouse.y;
            int x1 = mouse.x;
            int y1 = mouse.y;
            plotLine(x0, y0, x1, y1, &state);
            state.needsRedraw = true;
        }
        else state.needsRedraw = false;

        if (state.needsRedraw) {
            printf("drew frame\n");
            ClearBackground(state.theme[White]);
            drawCanvas(&state, colorGrid);
            UpdateTexture(texture, colorGrid);
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
    CloseWindow();
    free(state.grid);
    free(colorGrid);
    return 0;
}
