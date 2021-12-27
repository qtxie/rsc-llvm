#ifndef RED_SYSTEM_LEXMAP_H
#define RED_SYSTEM_LEXMAP_H

//
// The source code in this file is taken from REBOL: 
// https://github.com/rebol/rebol/blob/master/src/core/l-scan.c
//

typedef unsigned char	REBYTE;     // unsigned byte data

// In UTF8 C0, C1, F5, and FF are invalid.
#ifdef USE_UNICODE
#define LEX_UTFE LEX_DEFAULT
#else
#define LEX_UTFE LEX_WORD
#endif

/*
**  Lexical Table Entry Encoding
*/
#define LEX_SHIFT       5               /* shift for encoding classes */
#define LEX_CLASS       (3<<LEX_SHIFT)  /* class bit field */
#define LEX_VALUE       (0x1F)          /* value bit field */

#define GET_LEX_CLASS(c)  (Lex_Map[(REBYTE)c] >> LEX_SHIFT)
#define GET_LEX_VALUE(c)  (Lex_Map[(REBYTE)c] & LEX_VALUE)


/*
**	Delimiting Chars (encoded in the LEX_VALUE field)
*/
enum LEX_DELIMIT_ENUM {
	LEX_DELIMIT_SPACE,              /* 20 space */
	LEX_DELIMIT_LINEFEED,           /* 0A line-feed */
	LEX_DELIMIT_RETURN,             /* 0D return */
	LEX_DELIMIT_END_FILE,           /* 00 EOF */
	LEX_DELIMIT_SEMICOLON,          /* 3B ; */
	LEX_DELIMIT_LEFT_PAREN,         /* 28 ( */
	LEX_DELIMIT_RIGHT_PAREN,        /* 29 ) */
	LEX_DELIMIT_LEFT_BRACKET,       /* 5B [ */
	LEX_DELIMIT_RIGHT_BRACKET,      /* 5D ] */
	LEX_DELIMIT_LEFT_BRACE,         /* 7B { */
	LEX_DELIMIT_RIGHT_BRACE,        /* 7D } */
	LEX_DELIMIT_QUOTE,              /* 22 " */
	LEX_DELIMIT_SLASH,              /* 2F / - date, path, file */
	LEX_DELIMIT_UTF8_ERROR,
	LEX_DELIMIT_MAX
};


/*
**  General Lexical Classes (encoded in the LEX_CLASS field)
*/
enum LEX_CLASS_ENUM {
    LEX_CLASS_DELIMIT = 0,
    LEX_CLASS_SPECIAL,
    LEX_CLASS_WORD,
    LEX_CLASS_NUMBER
};

#define LEX_DELIMIT     (LEX_CLASS_DELIMIT<<LEX_SHIFT)
#define LEX_SPECIAL     (LEX_CLASS_SPECIAL<<LEX_SHIFT)
#define LEX_WORD        (LEX_CLASS_WORD<<LEX_SHIFT)
#define LEX_NUMBER      (LEX_CLASS_NUMBER<<LEX_SHIFT)

#define LEX_FLAG(n)             (1 << (n))
#define SET_LEX_FLAG(f,l)       (f = f | LEX_FLAG(l))
#define HAS_LEX_FLAGS(f,l)      (f & (l))
#define HAS_LEX_FLAG(f,l)       (f & LEX_FLAG(l))
#define ONLY_LEX_FLAG(f,l)      (f == LEX_FLAG(l))

#define MASK_LEX_CLASS(c)               (Lex_Map[(REBYTE)c] & LEX_CLASS)
#define IS_LEX_SPACE(c)                 (!Lex_Map[(REBYTE)c])
#define IS_LEX_ANY_SPACE(c)				(Lex_Map[(REBYTE)c]<=LEX_DELIMIT_RETURN)
#define IS_LEX_DELIMIT(c)               (MASK_LEX_CLASS(c) == LEX_DELIMIT)
#define IS_LEX_SPECIAL(c)               (MASK_LEX_CLASS(c) == LEX_SPECIAL)
#define IS_LEX_WORD(c)                  (MASK_LEX_CLASS(c) == LEX_WORD)
#define IS_LEX_NUMBER(c)                (MASK_LEX_CLASS(c) == LEX_NUMBER)

#define IS_LEX_AT_LEAST_SPECIAL(c)      (Lex_Map[(REBYTE)c] >= LEX_SPECIAL)
#define IS_LEX_AT_LEAST_WORD(c)         (Lex_Map[(REBYTE)c] >= LEX_WORD)
#define IS_LEX_AT_LEAST_NUMBER(c)       (Lex_Map[(REBYTE)c] >= LEX_NUMBER)

/*
**  Special Chars
*/
enum LEX_SPECIAL_ENUM {             /* The order is important! */
	LEX_SPECIAL_PERCENT,            /* 25 % - file name */
	LEX_SPECIAL_COLON,              /* 3A : - time, get, set */
	LEX_SPECIAL_AT,                 /* 40 @ - email */
	LEX_SPECIAL_BACKSLASH,          /* 5C \  */
	LEX_SPECIAL_TICK,               /* 27 ' - literal */
	LEX_SPECIAL_LESSER,				/* 3C < - compare or tag */
	LEX_SPECIAL_GREATER,			/* 3E > - compare or end tag */
	LEX_SPECIAL_PLUS,               /* 2B + - positive number */
	LEX_SPECIAL_MINUS,              /* 2D - - date, negative number */
	LEX_SPECIAL_TILDE,              /* 7E ~ - complement number */
									/** Any of these can follow - or ~ : */
	LEX_SPECIAL_PERIOD,             /* 2E . - decimal number */
	LEX_SPECIAL_COMMA,              /* 2C , - decimal number */
	LEX_SPECIAL_POUND,              /* 23 # - hex number */
	LEX_SPECIAL_DOLLAR,             /* 24 $ - money */
	LEX_SPECIAL_WORD,               /* SPECIAL - used for word chars (for nums) */
	LEX_SPECIAL_MAX
};

/*
**  Special Encodings
*/
#define LEX_DEFAULT (LEX_DELIMIT|LEX_DELIMIT_SPACE)     /* control chars = spaces */

/*
**  Characters not allowed in Words
*/
#define LEX_WORD_FLAGS (LEX_FLAG(LEX_SPECIAL_AT) |              \
                        LEX_FLAG(LEX_SPECIAL_PERCENT) |         \
                        LEX_FLAG(LEX_SPECIAL_BACKSLASH) |       \
                        LEX_FLAG(LEX_SPECIAL_COMMA) |           \
						LEX_FLAG(LEX_SPECIAL_POUND) |			\
						LEX_FLAG(LEX_SPECIAL_DOLLAR) |			\
                        LEX_FLAG(LEX_SPECIAL_COLON))


/***********************************************************************
**
*/	const REBYTE Lex_Map[256] =
/*
**		Maps each character to its lexical attributes, using
**		a frequency optimized encoding.
**
**		UTF8: The values C0, C1, F5 to FF never appear.
**
***********************************************************************/
{
	/* 00 EOF */    LEX_DELIMIT|LEX_DELIMIT_END_FILE,
	/* 01     */    LEX_DEFAULT,
	/* 02     */    LEX_DEFAULT,
	/* 03     */    LEX_DEFAULT,
	/* 04     */    LEX_DEFAULT,
	/* 05     */    LEX_DEFAULT,
	/* 06     */    LEX_DEFAULT,
	/* 07     */    LEX_DEFAULT,
	/* 08 BS  */    LEX_DEFAULT,
	/* 09 TAB */    LEX_DEFAULT,
	/* 0A LF  */    LEX_DELIMIT|LEX_DELIMIT_LINEFEED,
	/* 0B     */    LEX_DEFAULT,
	/* 0C PG  */    LEX_DEFAULT,
	/* 0D CR  */    LEX_DELIMIT|LEX_DELIMIT_RETURN,
	/* 0E     */    LEX_DEFAULT,
	/* 0F     */    LEX_DEFAULT,

	/* 10     */    LEX_DEFAULT,
	/* 11     */    LEX_DEFAULT,
	/* 12     */    LEX_DEFAULT,
	/* 13     */    LEX_DEFAULT,
	/* 14     */    LEX_DEFAULT,
	/* 15     */    LEX_DEFAULT,
	/* 16     */    LEX_DEFAULT,
	/* 17     */    LEX_DEFAULT,
	/* 18     */    LEX_DEFAULT,
	/* 19     */    LEX_DEFAULT,
	/* 1A     */    LEX_DEFAULT,
	/* 1B     */    LEX_DEFAULT,
	/* 1C     */    LEX_DEFAULT,
	/* 1D     */    LEX_DEFAULT,
	/* 1E     */    LEX_DEFAULT,
	/* 1F     */    LEX_DEFAULT,

	/* 20     */    LEX_DELIMIT|LEX_DELIMIT_SPACE,
	/* 21 !   */    LEX_WORD,
	/* 22 "   */    LEX_DELIMIT|LEX_DELIMIT_QUOTE,
	/* 23 #   */    LEX_SPECIAL|LEX_SPECIAL_POUND,
	/* 24 $   */    LEX_SPECIAL|LEX_SPECIAL_DOLLAR,
	/* 25 %   */    LEX_SPECIAL|LEX_SPECIAL_PERCENT,
	/* 26 &   */    LEX_WORD,
	/* 27 '   */    LEX_WORD|LEX_SPECIAL_TICK,
	/* 28 (   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_PAREN,
	/* 29 )   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_PAREN,
	/* 2A *   */    LEX_WORD,
	/* 2B +   */    LEX_WORD|LEX_SPECIAL_PLUS,
	/* 2C ,   */    LEX_SPECIAL|LEX_SPECIAL_COMMA,
	/* 2D -   */    LEX_WORD|LEX_SPECIAL_MINUS,
	/* 2E .   */    LEX_WORD|LEX_SPECIAL_PERIOD,
	/* 2F /   */    LEX_SPECIAL|LEX_DELIMIT_SLASH,

	/* 30 0   */    LEX_NUMBER|0,
	/* 31 1   */    LEX_NUMBER|1,
	/* 32 2   */    LEX_NUMBER|2,
	/* 33 3   */    LEX_NUMBER|3,
	/* 34 4   */    LEX_NUMBER|4,
	/* 35 5   */    LEX_NUMBER|5,
	/* 36 6   */    LEX_NUMBER|6,
	/* 37 7   */    LEX_NUMBER|7,
	/* 38 8   */    LEX_NUMBER|8,
	/* 39 9   */    LEX_NUMBER|9,
	/* 3A :   */    LEX_SPECIAL|LEX_SPECIAL_COLON,
	/* 3B ;   */    LEX_DELIMIT|LEX_DELIMIT_SEMICOLON,
	/* 3C <   */    LEX_WORD|LEX_SPECIAL_LESSER,
	/* 3D =   */    LEX_WORD,
	/* 3E >   */    LEX_WORD|LEX_SPECIAL_GREATER,
	/* 3F ?   */    LEX_WORD,

	/* 40 @   */    LEX_SPECIAL|LEX_SPECIAL_AT,
	/* 41 A   */    LEX_WORD|10,
	/* 42 B   */    LEX_WORD|11,
	/* 43 C   */    LEX_WORD|12,
	/* 44 D   */    LEX_WORD|13,
	/* 45 E   */    LEX_WORD|14,
	/* 46 F   */    LEX_WORD|15,
	/* 47 G   */    LEX_WORD,
	/* 48 H   */    LEX_WORD,
	/* 49 I   */    LEX_WORD,
	/* 4A J   */    LEX_WORD,
	/* 4B K   */    LEX_WORD,
	/* 4C L   */    LEX_WORD,
	/* 4D M   */    LEX_WORD,
	/* 4E N   */    LEX_WORD,
	/* 4F O   */    LEX_WORD,

	/* 50 P   */    LEX_WORD,
	/* 51 Q   */    LEX_WORD,
	/* 52 R   */    LEX_WORD,
	/* 53 S   */    LEX_WORD,
	/* 54 T   */    LEX_WORD,
	/* 55 U   */    LEX_WORD,
	/* 56 V   */    LEX_WORD,
	/* 57 W   */    LEX_WORD,
	/* 58 X   */    LEX_WORD,
	/* 59 Y   */    LEX_WORD,
	/* 5A Z   */    LEX_WORD,
	/* 5B [   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_BRACKET,
	/* 5C \   */    LEX_SPECIAL|LEX_SPECIAL_BACKSLASH,
	/* 5D ]   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_BRACKET,
	/* 5E ^   */    LEX_WORD,
	/* 5F _   */    LEX_WORD,

	/* 60 `   */    LEX_WORD,
	/* 61 a   */    LEX_WORD|10,
	/* 62 b   */    LEX_WORD|11,
	/* 63 c   */    LEX_WORD|12,
	/* 64 d   */    LEX_WORD|13,
	/* 65 e   */    LEX_WORD|14,
	/* 66 f   */    LEX_WORD|15,
	/* 67 g   */    LEX_WORD,
	/* 68 h   */    LEX_WORD,
	/* 69 i   */    LEX_WORD,
	/* 6A j   */    LEX_WORD,
	/* 6B k   */    LEX_WORD,
	/* 6C l   */    LEX_WORD,
	/* 6D m   */    LEX_WORD,
	/* 6E n   */    LEX_WORD,
	/* 6F o   */    LEX_WORD,

	/* 70 p   */    LEX_WORD,
	/* 71 q   */    LEX_WORD,
	/* 72 r   */    LEX_WORD,
	/* 73 s   */    LEX_WORD,
	/* 74 t   */    LEX_WORD,
	/* 75 u   */    LEX_WORD,
	/* 76 v   */    LEX_WORD,
	/* 77 w   */    LEX_WORD,
	/* 78 x   */    LEX_WORD,
	/* 79 y   */    LEX_WORD,
	/* 7A z   */    LEX_WORD,
	/* 7B {   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_BRACE,
	/* 7C |   */    LEX_WORD,
	/* 7D }   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_BRACE,
	/* 7E ~   */    LEX_WORD,  //LEX_SPECIAL|LEX_SPECIAL_TILDE,
	/* 7F DEL */    LEX_DEFAULT,

	/* Odd Control Chars */
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,    /* 80 */
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	/* Alternate Chars */
#ifdef USE_UNICODE
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
#else
	LEX_DEFAULT,LEX_WORD,LEX_WORD,LEX_WORD, /* A0 (a space) */
#endif
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	// C0, C1
	LEX_UTFE,LEX_UTFE,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_UTFE,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
	LEX_WORD,LEX_WORD,LEX_WORD,LEX_UTFE
};

#endif /* RED_SYSTEM_LEXMAP_H */
