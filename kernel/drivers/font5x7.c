#include <bnux/types.h>

/* Каждый символ — 7 строк по 5 бит (бит4 = левый пиксель, бит0 = правый).
 * Специально только ЗАГЛАВНЫЕ буквы — под ретро-DOS стилистику BNUX.
 * Символы вне таблицы рисуются как пустой прямоугольник (см. console.c). */

#define R(a,b,c,d,e) ((a<<4)|(b<<3)|(c<<2)|(d<<1)|e)

static const uint8_t font_space[7] = { R(0,0,0,0,0),R(0,0,0,0,0),R(0,0,0,0,0),R(0,0,0,0,0),R(0,0,0,0,0),R(0,0,0,0,0),R(0,0,0,0,0) };
static const uint8_t font_0[7] = { R(0,1,1,1,0),R(1,0,0,1,1),R(1,0,1,0,1),R(1,1,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_1[7] = { R(0,0,1,0,0),R(0,1,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,1,1,1,0) };
static const uint8_t font_2[7] = { R(0,1,1,1,0),R(1,0,0,0,1),R(0,0,0,0,1),R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0),R(1,1,1,1,1) };
static const uint8_t font_3[7] = { R(1,1,1,1,0),R(0,0,0,0,1),R(0,0,0,1,0),R(0,0,1,1,0),R(0,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_4[7] = { R(0,0,0,1,0),R(0,0,1,1,0),R(0,1,0,1,0),R(1,0,0,1,0),R(1,1,1,1,1),R(0,0,0,1,0),R(0,0,0,1,0) };
static const uint8_t font_5[7] = { R(1,1,1,1,1),R(1,0,0,0,0),R(1,1,1,1,0),R(0,0,0,0,1),R(0,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_6[7] = { R(0,0,1,1,0),R(0,1,0,0,0),R(1,0,0,0,0),R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_7[7] = { R(1,1,1,1,1),R(0,0,0,0,1),R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0),R(0,1,0,0,0),R(0,1,0,0,0) };
static const uint8_t font_8[7] = { R(0,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_9[7] = { R(0,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,1),R(0,0,0,0,1),R(0,0,0,1,0),R(0,1,1,0,0) };

static const uint8_t font_A[7] = { R(0,0,1,0,0),R(0,1,0,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,1),R(1,0,0,0,1),R(1,0,0,0,1) };
static const uint8_t font_B[7] = { R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,0) };
static const uint8_t font_C[7] = { R(0,1,1,1,1),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(0,1,1,1,1) };
static const uint8_t font_D[7] = { R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,0) };
static const uint8_t font_E[7] = { R(1,1,1,1,1),R(1,0,0,0,0),R(1,0,0,0,0),R(1,1,1,1,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,1,1,1,1) };
static const uint8_t font_F[7] = { R(1,1,1,1,1),R(1,0,0,0,0),R(1,0,0,0,0),R(1,1,1,1,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0) };
static const uint8_t font_G[7] = { R(0,1,1,1,1),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,1,1,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,1) };
static const uint8_t font_H[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1) };
static const uint8_t font_I[7] = { R(0,1,1,1,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,1,1,1,0) };
static const uint8_t font_J[7] = { R(0,0,0,1,1),R(0,0,0,0,1),R(0,0,0,0,1),R(0,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_K[7] = { R(1,0,0,0,1),R(1,0,0,1,0),R(1,0,1,0,0),R(1,1,0,0,0),R(1,0,1,0,0),R(1,0,0,1,0),R(1,0,0,0,1) };
static const uint8_t font_L[7] = { R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,1,1,1,1) };
static const uint8_t font_M[7] = { R(1,0,0,0,1),R(1,1,0,1,1),R(1,0,1,0,1),R(1,0,1,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1) };
static const uint8_t font_N[7] = { R(1,0,0,0,1),R(1,1,0,0,1),R(1,0,1,0,1),R(1,0,0,1,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1) };
static const uint8_t font_O[7] = { R(0,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_P[7] = { R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,0),R(1,0,0,0,0),R(1,0,0,0,0),R(1,0,0,0,0) };
static const uint8_t font_Q[7] = { R(0,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,1,0,1),R(1,0,0,1,0),R(0,1,1,0,1) };
static const uint8_t font_R[7] = { R(1,1,1,1,0),R(1,0,0,0,1),R(1,0,0,0,1),R(1,1,1,1,0),R(1,0,1,0,0),R(1,0,0,1,0),R(1,0,0,0,1) };
static const uint8_t font_S[7] = { R(0,1,1,1,1),R(1,0,0,0,0),R(1,0,0,0,0),R(0,1,1,1,0),R(0,0,0,0,1),R(0,0,0,0,1),R(1,1,1,1,0) };
static const uint8_t font_T[7] = { R(1,1,1,1,1),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0) };
static const uint8_t font_U[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,1,1,0) };
static const uint8_t font_V[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,0,1,0),R(0,0,1,0,0) };
static const uint8_t font_W[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,0,0,1),R(1,0,1,0,1),R(1,0,1,0,1),R(1,0,1,0,1),R(0,1,0,1,0) };
static const uint8_t font_X[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,0,1,0),R(0,0,1,0,0),R(0,1,0,1,0),R(1,0,0,0,1),R(1,0,0,0,1) };
static const uint8_t font_Y[7] = { R(1,0,0,0,1),R(1,0,0,0,1),R(0,1,0,1,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,0,1,0,0) };
static const uint8_t font_Z[7] = { R(1,1,1,1,1),R(0,0,0,0,1),R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0),R(1,0,0,0,0),R(1,1,1,1,1) };

static const uint8_t font_dot[7]   = { 0,0,0,0,0,0, R(0,1,1,0,0) };
static const uint8_t font_slash[7] = { R(0,0,0,0,1),R(0,0,0,1,0),R(0,0,1,0,0),R(0,0,1,0,0),R(0,1,0,0,0),R(1,0,0,0,0),0 };
static const uint8_t font_dash[7]  = { 0,0,0, R(1,1,1,1,1), 0,0,0 };
static const uint8_t font_colon[7] = { 0, R(0,1,1,0,0), R(0,1,1,0,0), 0, R(0,1,1,0,0), R(0,1,1,0,0), 0 };
static const uint8_t font_gt[7]    = { R(1,0,0,0,0),R(0,1,0,0,0),R(0,0,1,0,0),R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0),R(1,0,0,0,0) };
static const uint8_t font_lparen[7] = { R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0),R(0,1,0,0,0),R(0,1,0,0,0),R(0,0,1,0,0),R(0,0,0,1,0) };
static const uint8_t font_rparen[7] = { R(0,1,0,0,0),R(0,0,1,0,0),R(0,0,0,1,0),R(0,0,0,1,0),R(0,0,0,1,0),R(0,0,1,0,0),R(0,1,0,0,0) };
static const uint8_t font_underscore[7] = { 0,0,0,0,0,0, R(1,1,1,1,1) };

const uint8_t *font_get_glyph(char c) {
    if (c >= 'a' && c <= 'z') c -= 32; // приводим к верхнему регистру
    switch (c) {
        case ' ': return font_space;
        case '0': return font_0; case '1': return font_1; case '2': return font_2;
        case '3': return font_3; case '4': return font_4; case '5': return font_5;
        case '6': return font_6; case '7': return font_7; case '8': return font_8;
        case '9': return font_9;
        case 'A': return font_A; case 'B': return font_B; case 'C': return font_C;
        case 'D': return font_D; case 'E': return font_E; case 'F': return font_F;
        case 'G': return font_G; case 'H': return font_H; case 'I': return font_I;
        case 'J': return font_J; case 'K': return font_K; case 'L': return font_L;
        case 'M': return font_M; case 'N': return font_N; case 'O': return font_O;
        case 'P': return font_P; case 'Q': return font_Q; case 'R': return font_R;
        case 'S': return font_S; case 'T': return font_T; case 'U': return font_U;
        case 'V': return font_V; case 'W': return font_W; case 'X': return font_X;
        case 'Y': return font_Y; case 'Z': return font_Z;
        case '.': return font_dot; case '/': return font_slash;
        case '-': return font_dash; case ':': return font_colon;
        case '>': return font_gt;  case '_': return font_underscore;
        case '(': return font_lparen; case ')': return font_rparen;
        default: return NULL; // неизвестный символ — console.c нарисует пустое место
    }
}
