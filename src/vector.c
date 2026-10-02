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
    printf(
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
        printf("    <polyline \n");
        printf("        points=\"");
        for (int p = 0; p < s.numPoints; p++) {
            Position point = *(Position*)atIndex(&v.points, s.startingPointIndex + p);
            printf("%d,%d ", point.x, point.y);
        }
        printf(
            "\" \n"
            "        stroke=\"red\" \n"
            "        stroke-width=\"4\" \n"
            "        fill=\"none\" />\n"
        );
    }
    printf("</svg>\n");
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
