typedef struct {
    void         * data;
    int            length;
    int            elementSize;
    int            capacity;
} DynamicArray;

DynamicArray 
newDynamicArray(int elementSize) {
    return (DynamicArray) {
        .data        = malloc(4 * elementSize),
        .length      = 0,
        .elementSize = elementSize,
        .capacity    = 4,
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
    return &d->data + d->length * d->elementSize;
}

typedef struct {
    uint16_t       x, y;
} Position;

typedef struct {
    uint8_t        numPoints;
    uint16_t       startingPointIndex;
    bool           hidden;               // expensive
    ColorT         color;
} Stroke;

typedef struct {
    DynamicArray   points;
    DynamicArray   highlighterStrokes;
    DynamicArray   penStrokes;
    uint16_t     * gridOfStrokeIndecies;
    uint16_t       numStrokes;
} VectorGrid;

void 
gridToSvg(FILE* file, VectorGrid v) {
    printf("<svg \n");
    printf("    width=\"391\" \n");
    printf("    height=\"391\" \n");
    printf("    viewBox=\"-10 -10 500 500\" \n");
    printf("    xmlns=\"http://www.w3.org/2000/svg\" \n");
    printf("    xmlns:xlink=\"http://www.w3.org/1999/xlink\" >\n");
   // printf("    <rect \n");
   // printf("        fill=\"#fff\" \n");
   // printf("        stroke=\"#000\" \n");
   // printf("        x=\"-70\" \n");
   // printf("        y=\"-70\" \n");
   // printf("        width=\"390\" \n");
   // printf("        height=\"390\" />\n");
    for (int i = 0; i < v.numStrokes; i++) {
        Stroke s = v.strokes[i];
        if (s.hidden) continue;
        printf("    <polyline \n");
        printf("        points=\"");
        for (int p = 0; p < s.numPoints; p++) {
            Position point = *atIndex(v, s.startingPointIndex + p);
            printf("%d,%d ", point.x, point.y);
        }
        printf("\" \n");
        printf("        stroke=\"red\" \n");
        printf("        stroke-width=\"4\" \n");
        printf("        fill=\"none\" />\n");
    }
    printf("</svg>\n");

}

void test() {
    Position points[2] = { { 0, 0 }, { 250, 250 } };
    Stroke strokes[1] = { { 2, points, false, Red } };
    VectorGrid testgrid = {
        .gridOfStrokeIndecies = NULL,
        .numStrokes = 1,
        .strokes = strokes,
    };
    gridToSvg(stdout, testgrid);
}
