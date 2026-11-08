/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1





# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_IDENTIFIER = 3,                 /* IDENTIFIER  */
  YYSYMBOL_INTEGER = 4,                    /* INTEGER  */
  YYSYMBOL_STRING = 5,                     /* STRING  */
  YYSYMBOL_CHARACTER = 6,                  /* CHARACTER  */
  YYSYMBOL_DECLARE_INT = 7,                /* DECLARE_INT  */
  YYSYMBOL_DECLARE_BOOLEAN = 8,            /* DECLARE_BOOLEAN  */
  YYSYMBOL_DECLARE_TEXT = 9,               /* DECLARE_TEXT  */
  YYSYMBOL_DECLARE_CHAR = 10,              /* DECLARE_CHAR  */
  YYSYMBOL_DECLARE_TRUE = 11,              /* DECLARE_TRUE  */
  YYSYMBOL_DECLARE_FALSE = 12,             /* DECLARE_FALSE  */
  YYSYMBOL_STAR = 13,                      /* STAR  */
  YYSYMBOL_ASSIGN = 14,                    /* ASSIGN  */
  YYSYMBOL_SEMICOLON = 15,                 /* SEMICOLON  */
  YYSYMBOL_COMMA = 16,                     /* COMMA  */
  YYSYMBOL_GAUSS = 17,                     /* GAUSS  */
  YYSYMBOL_NEUMANN = 18,                   /* NEUMANN  */
  YYSYMBOL_PITAGORAS = 19,                 /* PITAGORAS  */
  YYSYMBOL_EUCLIDES = 20,                  /* EUCLIDES  */
  YYSYMBOL_EULER = 21,                     /* EULER  */
  YYSYMBOL_DESCARTES = 22,                 /* DESCARTES  */
  YYSYMBOL_AND = 23,                       /* AND  */
  YYSYMBOL_OR = 24,                        /* OR  */
  YYSYMBOL_NOT = 25,                       /* NOT  */
  YYSYMBOL_XOR = 26,                       /* XOR  */
  YYSYMBOL_CYCLE = 27,                     /* CYCLE  */
  YYSYMBOL_LET = 28,                       /* LET  */
  YYSYMBOL_UNTIL = 29,                     /* UNTIL  */
  YYSYMBOL_STEP = 30,                      /* STEP  */
  YYSYMBOL_ENDGAME = 31,                   /* ENDGAME  */
  YYSYMBOL_LBRACKET = 32,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 33,                  /* RBRACKET  */
  YYSYMBOL_DOT = 34,                       /* DOT  */
  YYSYMBOL_DECLARE_LIST = 35,              /* DECLARE_LIST  */
  YYSYMBOL_ADD = 36,                       /* ADD  */
  YYSYMBOL_REMOVE = 37,                    /* REMOVE  */
  YYSYMBOL_SIZE = 38,                      /* SIZE  */
  YYSYMBOL_BRING = 39,                     /* BRING  */
  YYSYMBOL_AKA = 40,                       /* AKA  */
  YYSYMBOL_DECLARE_CONST = 41,             /* DECLARE_CONST  */
  YYSYMBOL_SEEK = 42,                      /* SEEK  */
  YYSYMBOL_SEIZE = 43,                     /* SEIZE  */
  YYSYMBOL_LPAREN = 44,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 45,                    /* RPAREN  */
  YYSYMBOL_LBRACE = 46,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 47,                    /* RBRACE  */
  YYSYMBOL_WHETHER = 48,                   /* WHETHER  */
  YYSYMBOL_ALIF = 49,                      /* ALIF  */
  YYSYMBOL_ALSO = 50,                      /* ALSO  */
  YYSYMBOL_WHALE = 51,                     /* WHALE  */
  YYSYMBOL_STOP = 52,                      /* STOP  */
  YYSYMBOL_GIVE = 53,                      /* GIVE  */
  YYSYMBOL_CREATE_FUNK = 54,               /* CREATE_FUNK  */
  YYSYMBOL_HASH = 55,                      /* HASH  */
  YYSYMBOL_MAIN = 56,                      /* MAIN  */
  YYSYMBOL_DECLARE_INFINITE_VOID = 57,     /* DECLARE_INFINITE_VOID  */
  YYSYMBOL_EQUAL = 58,                     /* EQUAL  */
  YYSYMBOL_NOT_EQUAL = 59,                 /* NOT_EQUAL  */
  YYSYMBOL_LESS = 60,                      /* LESS  */
  YYSYMBOL_GREATER = 61,                   /* GREATER  */
  YYSYMBOL_LESS_EQUAL = 62,                /* LESS_EQUAL  */
  YYSYMBOL_GREATER_EQUAL = 63,             /* GREATER_EQUAL  */
  YYSYMBOL_NEGATIVO = 64,                  /* NEGATIVO  */
  YYSYMBOL_YYACCEPT = 65,                  /* $accept  */
  YYSYMBOL_programa = 66,                  /* programa  */
  YYSYMBOL_funcion = 67,                   /* funcion  */
  YYSYMBOL_parametros_opcionales = 68,     /* parametros_opcionales  */
  YYSYMBOL_parametros = 69,                /* parametros  */
  YYSYMBOL_parametro = 70,                 /* parametro  */
  YYSYMBOL_tipo_retorno = 71,              /* tipo_retorno  */
  YYSYMBOL_nombre_funcion = 72,            /* nombre_funcion  */
  YYSYMBOL_sentencias = 73,                /* sentencias  */
  YYSYMBOL_sentencia = 74,                 /* sentencia  */
  YYSYMBOL_retorno = 75,                   /* retorno  */
  YYSYMBOL_llamada = 76,                   /* llamada  */
  YYSYMBOL_argumentos_opcionales = 77,     /* argumentos_opcionales  */
  YYSYMBOL_argumentos = 78,                /* argumentos  */
  YYSYMBOL_bloque = 79,                    /* bloque  */
  YYSYMBOL_contenido_bloque = 80,          /* contenido_bloque  */
  YYSYMBOL_condicional = 81,               /* condicional  */
  YYSYMBOL_alternativa = 82,               /* alternativa  */
  YYSYMBOL_ciclo = 83,                     /* ciclo  */
  YYSYMBOL_declaracion = 84,               /* declaracion  */
  YYSYMBOL_asignacion = 85,                /* asignacion  */
  YYSYMBOL_tipo = 86,                      /* tipo  */
  YYSYMBOL_expresion = 87,                 /* expresion  */
  YYSYMBOL_elemento_superior = 88,         /* elemento_superior  */
  YYSYMBOL_importacion = 89,               /* importacion  */
  YYSYMBOL_alias_opcional = 90,            /* alias_opcional  */
  YYSYMBOL_tipo_parametro = 91,            /* tipo_parametro  */
  YYSYMBOL_tipo_variable = 92,             /* tipo_variable  */
  YYSYMBOL_dimensiones = 93,               /* dimensiones  */
  YYSYMBOL_dimensiones_parametro = 94,     /* dimensiones_parametro  */
  YYSYMBOL_dimension = 95,                 /* dimension  */
  YYSYMBOL_dimension_parametro = 96,       /* dimension_parametro  */
  YYSYMBOL_declarador = 97,                /* declarador  */
  YYSYMBOL_inicializador_opcional = 98,    /* inicializador_opcional  */
  YYSYMBOL_declaradores = 99,              /* declaradores  */
  YYSYMBOL_listas = 100,                   /* listas  */
  YYSYMBOL_constante = 101,                /* constante  */
  YYSYMBOL_declaracion_lista = 102,        /* declaracion_lista  */
  YYSYMBOL_declarador_lista = 103,         /* declarador_lista  */
  YYSYMBOL_recorrido = 104,                /* recorrido  */
  YYSYMBOL_paso_opcional = 105,            /* paso_opcional  */
  YYSYMBOL_intentar = 106,                 /* intentar  */
  YYSYMBOL_capturas = 107,                 /* capturas  */
  YYSYMBOL_captura = 108,                  /* captura  */
  YYSYMBOL_referencia = 109,               /* referencia  */
  YYSYMBOL_destino_llamada = 110,          /* destino_llamada  */
  YYSYMBOL_literal_coleccion = 111,        /* literal_coleccion  */
  YYSYMBOL_elementos_opcionales = 112,     /* elementos_opcionales  */
  YYSYMBOL_elementos = 113                 /* elementos  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
# define YYCOPY_NEEDED 1
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  12
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   695

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  65
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  121
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  236

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   319


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    76,    76,    83,    89,   102,   107,   112,   118,   126,
     136,   137,   142,   143,   148,   154,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   175,   181,   192,
     199,   206,   213,   222,   227,   232,   238,   244,   249,   254,
     263,   275,   277,   285,   294,   305,   313,   323,   324,   325,
     326,   333,   334,   335,   336,   337,   338,   339,   340,   341,
     348,   355,   362,   369,   376,   383,   390,   397,   404,   411,
     418,   425,   432,   439,   446,   452,   458,   465,   467,   469,
     474,   484,   486,   491,   493,   500,   509,   511,   521,   527,
     532,   538,   543,   552,   554,   562,   573,   575,   580,   582,
     596,   598,   612,   623,   628,   639,   652,   657,   662,   672,
     678,   683,   694,   696,   703,   713,   715,   720,   725,   730,
     735,   741
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  static const char *const yy_sname[] =
  {
  "end of file", "error", "invalid token", "IDENTIFIER", "INTEGER",
  "STRING", "CHARACTER", "DECLARE_INT", "DECLARE_BOOLEAN", "DECLARE_TEXT",
  "DECLARE_CHAR", "DECLARE_TRUE", "DECLARE_FALSE", "STAR", "ASSIGN",
  "SEMICOLON", "COMMA", "GAUSS", "NEUMANN", "PITAGORAS", "EUCLIDES",
  "EULER", "DESCARTES", "AND", "OR", "NOT", "XOR", "CYCLE", "LET", "UNTIL",
  "STEP", "ENDGAME", "LBRACKET", "RBRACKET", "DOT", "DECLARE_LIST", "ADD",
  "REMOVE", "SIZE", "BRING", "AKA", "DECLARE_CONST", "SEEK", "SEIZE",
  "LPAREN", "RPAREN", "LBRACE", "RBRACE", "WHETHER", "ALIF", "ALSO",
  "WHALE", "STOP", "GIVE", "CREATE_FUNK", "HASH", "MAIN",
  "DECLARE_INFINITE_VOID", "EQUAL", "NOT_EQUAL", "LESS", "GREATER",
  "LESS_EQUAL", "GREATER_EQUAL", "NEGATIVO", "$accept", "programa",
  "funcion", "parametros_opcionales", "parametros", "parametro",
  "tipo_retorno", "nombre_funcion", "sentencias", "sentencia", "retorno",
  "llamada", "argumentos_opcionales", "argumentos", "bloque",
  "contenido_bloque", "condicional", "alternativa", "ciclo", "declaracion",
  "asignacion", "tipo", "expresion", "elemento_superior", "importacion",
  "alias_opcional", "tipo_parametro", "tipo_variable", "dimensiones",
  "dimensiones_parametro", "dimension", "dimension_parametro",
  "declarador", "inicializador_opcional", "declaradores", "listas",
  "constante", "declaracion_lista", "declarador_lista", "recorrido",
  "paso_opcional", "intentar", "capturas", "captura", "referencia",
  "destino_llamada", "literal_coleccion", "elementos_opcionales",
  "elementos", YY_NULLPTR
  };
  return yy_sname[yysymbol];
}
#endif

#define YYPACT_NINF (-125)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-116)

#define yytable_value_is_error(Yyn) \
  ((Yyn) == YYTABLE_NINF)

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      17,    14,    19,   -27,    13,  -125,  -125,  -125,  -125,   -11,
      21,    11,  -125,  -125,    41,    55,    65,  -125,  -125,  -125,
    -125,  -125,    23,  -125,  -125,  -125,    44,    73,    -1,   194,
      44,  -125,   194,  -125,  -125,    57,  -125,  -125,  -125,  -125,
    -125,  -125,   194,   194,   194,    58,    59,    66,   194,  -125,
    -125,   317,    25,    67,  -125,  -125,    62,   110,    92,    92,
     538,    83,   112,   194,   194,   194,   336,   194,   194,   194,
     194,   194,   194,   194,   194,   194,  -125,   194,   194,   194,
     194,   194,   194,   194,   126,   194,  -125,   124,    95,   123,
    -125,  -125,   194,   259,   307,   365,  -125,   114,   114,    92,
      92,    92,    92,   616,   585,   598,   632,   632,   632,   632,
     632,   632,   383,  -125,    96,   129,   538,    56,   100,   110,
     538,   194,   194,  -125,  -125,  -125,   194,    65,   115,  -125,
     198,  -125,  -125,   412,   432,   538,  -125,    94,   115,  -125,
    -125,   135,   139,   146,   100,   107,   109,   158,   198,  -125,
    -125,   140,  -125,   111,  -125,  -125,  -125,  -125,  -125,    46,
    -125,  -125,  -125,  -125,    -9,  -125,  -125,  -125,  -125,    65,
     128,   144,    80,  -125,   116,   194,   194,  -125,   238,  -125,
    -125,  -125,  -125,   157,   194,   151,   194,    65,  -125,   146,
     122,   116,  -125,   442,   462,  -125,   135,  -125,   248,   194,
    -125,   509,   151,  -125,   165,  -125,   100,   100,  -125,   538,
     194,  -125,   168,   -35,   120,   522,   130,   136,   100,  -125,
     159,   194,   100,   100,   194,  -125,  -125,   538,   148,  -125,
     569,   166,   100,  -125,   -35,  -125
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,    77,     2,    78,    79,    81,
       0,     0,     1,     3,     0,     0,     0,    47,    48,    49,
      50,    11,     0,    10,    82,    80,    86,     0,     0,     0,
      87,    88,     0,    13,    12,     0,   112,    51,    52,    53,
      54,    55,     0,     0,   118,     0,     0,     0,     0,   116,
      58,     0,    56,     0,    57,    89,     0,     5,    75,    74,
     120,     0,   119,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    92,     0,     0,     0,
       0,     0,     0,     0,     0,    33,   102,     0,     0,     6,
       7,   117,     0,     0,     0,     0,    76,    65,    66,    67,
      68,    72,    73,    69,    70,    71,    59,    60,    61,    62,
      63,    64,     0,   113,     0,    34,    35,     0,     0,     0,
     121,     0,     0,    32,   114,    29,     0,     0,    83,     9,
      38,     4,     8,     0,     0,    36,    85,     0,    84,    93,
      90,   112,     0,     0,     0,     0,     0,     0,    39,    14,
      21,     0,    18,     0,    19,    20,    16,    17,    98,     0,
      22,    23,    24,    25,   115,    30,    31,    94,    91,     0,
       0,     0,     0,   100,     0,     0,     0,    28,     0,    15,
      26,    37,    45,     0,     0,    96,     0,     0,   103,     0,
       0,   108,   109,     0,     0,    27,     0,    99,     0,     0,
      95,     0,    96,   101,     0,   110,     0,     0,    46,    97,
       0,   104,     0,    41,     0,   106,     0,     0,     0,    40,
       0,     0,     0,     0,     0,    43,    44,   107,     0,   111,
       0,     0,     0,   105,    41,    42
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -125,  -125,  -125,  -125,  -125,    63,  -125,  -125,  -125,    36,
    -125,  -124,  -125,  -125,  -114,  -125,  -125,   -49,  -125,  -125,
    -125,   -10,   -32,   182,  -125,  -125,  -125,    18,  -125,  -125,
     -23,    50,     8,     1,  -125,  -125,  -122,  -125,     4,  -125,
    -125,  -125,  -125,    16,  -121,  -125,  -125,  -125,  -125
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     4,     5,    88,    89,    90,    22,    35,   148,   149,
     150,    50,   114,   115,   152,   153,   154,   219,   155,   156,
     157,    26,    51,     6,     7,    15,   129,    27,    30,   138,
     139,   140,   158,   200,   159,   172,     8,   161,   173,   162,
     222,   163,   191,   192,    52,    53,    54,    61,    62
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      56,    23,    33,    31,   131,   184,   151,    55,   160,   164,
      58,    59,    60,    12,   217,   218,    66,     9,    17,    18,
      19,    20,    10,    83,   151,    84,   160,   164,    11,    14,
     174,    93,    94,    95,    16,    97,    98,    99,   100,   101,
     102,   103,   104,   105,    24,   106,   107,   108,   109,   110,
     111,   112,     1,   116,     2,    34,     1,    83,     2,    84,
     120,   182,   183,    17,    18,    19,    20,     3,    21,  -115,
      25,     3,    17,    18,    19,    20,    29,    86,    28,    67,
      68,    69,    70,    71,    72,    73,    74,    32,    75,   133,
     134,   127,   213,   214,   135,   188,   189,    36,    37,    38,
      39,    57,    63,    64,   225,    40,    41,   128,   228,   229,
      65,    85,    42,    87,    72,   178,    91,   136,   234,    43,
      77,    78,    79,    80,    81,    82,    44,   167,    92,   113,
      45,    46,    47,    69,    70,    71,    72,   117,    48,   119,
     118,   125,   170,   193,   194,   126,   130,   137,   169,   171,
      49,   175,   198,   176,   201,   180,   186,   187,   181,   190,
     196,    36,    37,    38,    39,   199,   204,   209,   212,    40,
      41,   216,   220,   177,   226,   223,    42,   202,   215,   231,
     224,   233,   132,    43,   179,   235,    13,   185,   168,   227,
      44,   197,   230,   203,    45,    46,    47,    36,    37,    38,
      39,   141,    48,   211,     0,    40,    41,   205,     0,     0,
       0,     0,    42,     0,    49,     0,     0,     0,     0,    43,
       0,     0,     0,     0,     0,   142,    44,     0,     0,     0,
      45,    46,    47,   143,    45,    46,    47,     0,    48,     2,
     144,     0,     0,     0,   130,     0,   145,     0,     0,   146,
      49,   147,     0,   195,    49,    67,    68,    69,    70,    71,
      72,    73,    74,   208,    75,    67,    68,    69,    70,    71,
      72,    73,    74,     0,    75,   121,    67,    68,    69,    70,
      71,    72,    73,    74,     0,    75,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    77,    78,    79,    80,
      81,    82,     0,     0,     0,     0,    77,    78,    79,    80,
      81,    82,     0,     0,     0,     0,     0,    77,    78,    79,
      80,    81,    82,   122,    67,    68,    69,    70,    71,    72,
      73,    74,     0,    75,    67,    68,    69,    70,    71,    72,
      73,    74,     0,    75,     0,     0,     0,     0,     0,     0,
      76,     0,     0,    67,    68,    69,    70,    71,    72,    73,
      74,     0,    75,     0,     0,    77,    78,    79,    80,    81,
      82,     0,     0,     0,     0,    77,    78,    79,    80,    81,
      82,    96,    67,    68,    69,    70,    71,    72,    73,    74,
       0,    75,     0,     0,    77,    78,    79,    80,    81,    82,
      67,    68,    69,    70,    71,    72,    73,    74,     0,    75,
     123,     0,     0,     0,     0,     0,   124,     0,     0,     0,
       0,     0,     0,    77,    78,    79,    80,    81,    82,    67,
      68,    69,    70,    71,    72,    73,    74,     0,    75,     0,
       0,    77,    78,    79,    80,    81,    82,     0,     0,    67,
      68,    69,    70,    71,    72,    73,    74,   165,    75,    67,
      68,    69,    70,    71,    72,    73,    74,     0,    75,     0,
      77,    78,    79,    80,    81,    82,     0,   166,     0,    67,
      68,    69,    70,    71,    72,    73,    74,   206,    75,     0,
      77,    78,    79,    80,    81,    82,     0,     0,     0,     0,
      77,    78,    79,    80,    81,    82,     0,   207,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      77,    78,    79,    80,    81,    82,    67,    68,    69,    70,
      71,    72,    73,    74,     0,    75,     0,     0,   210,    67,
      68,    69,    70,    71,    72,    73,    74,     0,    75,     0,
       0,     0,   221,     0,     0,    67,    68,    69,    70,    71,
      72,    73,    74,     0,    75,     0,     0,    77,    78,    79,
      80,    81,    82,     0,     0,     0,     0,     0,     0,     0,
      77,    78,    79,    80,    81,    82,    67,    68,    69,    70,
      71,    72,    73,    74,     0,    75,    77,    78,    79,    80,
      81,    82,    67,    68,    69,    70,    71,    72,    73,     0,
       0,    75,     0,     0,   232,    67,    68,    69,    70,    71,
      72,    73,     0,     0,     0,     0,     0,    77,    78,    79,
      80,    81,    82,    67,    68,    69,    70,    71,    72,     0,
       0,     0,     0,    77,    78,    79,    80,    81,    82,    67,
      68,    69,    70,    71,    72,     0,    77,    78,    79,    80,
      81,    82,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    77,    78,    79,    80,    81,    82,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    -116,  -116,  -116,  -116,  -116,  -116
};

static const yytype_int16 yycheck[] =
{
      32,    11,     3,    26,   118,    14,   130,    30,   130,   130,
      42,    43,    44,     0,    49,    50,    48,     3,     7,     8,
       9,    10,     3,    32,   148,    34,   148,   148,    55,    40,
     144,    63,    64,    65,    13,    67,    68,    69,    70,    71,
      72,    73,    74,    75,     3,    77,    78,    79,    80,    81,
      82,    83,    39,    85,    41,    56,    39,    32,    41,    34,
      92,    15,    16,     7,     8,     9,    10,    54,    57,    44,
      15,    54,     7,     8,     9,    10,    32,    15,    55,    17,
      18,    19,    20,    21,    22,    23,    24,    14,    26,   121,
     122,    35,   206,   207,   126,    15,    16,     3,     4,     5,
       6,    44,    44,    44,   218,    11,    12,   117,   222,   223,
      44,    44,    18,     3,    22,   147,    33,   127,   232,    25,
      58,    59,    60,    61,    62,    63,    32,    33,    16,     3,
      36,    37,    38,    19,    20,    21,    22,    13,    44,    16,
      45,    45,     3,   175,   176,    16,    46,    32,    13,     3,
      56,    44,   184,    44,   186,    15,    28,    13,    47,    43,
       3,     3,     4,     5,     6,    14,    44,   199,     3,    11,
      12,     3,    52,    15,    15,    45,    18,   187,   210,    31,
      44,    15,   119,    25,   148,   234,     4,   169,   138,   221,
      32,   183,   224,   189,    36,    37,    38,     3,     4,     5,
       6,     3,    44,   202,    -1,    11,    12,   191,    -1,    -1,
      -1,    -1,    18,    -1,    56,    -1,    -1,    -1,    -1,    25,
      -1,    -1,    -1,    -1,    -1,    27,    32,    -1,    -1,    -1,
      36,    37,    38,    35,    36,    37,    38,    -1,    44,    41,
      42,    -1,    -1,    -1,    46,    -1,    48,    -1,    -1,    51,
      56,    53,    -1,    15,    56,    17,    18,    19,    20,    21,
      22,    23,    24,    15,    26,    17,    18,    19,    20,    21,
      22,    23,    24,    -1,    26,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    -1,    26,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    58,    59,    60,    61,
      62,    63,    -1,    -1,    -1,    -1,    58,    59,    60,    61,
      62,    63,    -1,    -1,    -1,    -1,    -1,    58,    59,    60,
      61,    62,    63,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    -1,    26,    17,    18,    19,    20,    21,    22,
      23,    24,    -1,    26,    -1,    -1,    -1,    -1,    -1,    -1,
      33,    -1,    -1,    17,    18,    19,    20,    21,    22,    23,
      24,    -1,    26,    -1,    -1,    58,    59,    60,    61,    62,
      63,    -1,    -1,    -1,    -1,    58,    59,    60,    61,    62,
      63,    45,    17,    18,    19,    20,    21,    22,    23,    24,
      -1,    26,    -1,    -1,    58,    59,    60,    61,    62,    63,
      17,    18,    19,    20,    21,    22,    23,    24,    -1,    26,
      45,    -1,    -1,    -1,    -1,    -1,    33,    -1,    -1,    -1,
      -1,    -1,    -1,    58,    59,    60,    61,    62,    63,    17,
      18,    19,    20,    21,    22,    23,    24,    -1,    26,    -1,
      -1,    58,    59,    60,    61,    62,    63,    -1,    -1,    17,
      18,    19,    20,    21,    22,    23,    24,    45,    26,    17,
      18,    19,    20,    21,    22,    23,    24,    -1,    26,    -1,
      58,    59,    60,    61,    62,    63,    -1,    45,    -1,    17,
      18,    19,    20,    21,    22,    23,    24,    45,    26,    -1,
      58,    59,    60,    61,    62,    63,    -1,    -1,    -1,    -1,
      58,    59,    60,    61,    62,    63,    -1,    45,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      58,    59,    60,    61,    62,    63,    17,    18,    19,    20,
      21,    22,    23,    24,    -1,    26,    -1,    -1,    29,    17,
      18,    19,    20,    21,    22,    23,    24,    -1,    26,    -1,
      -1,    -1,    30,    -1,    -1,    17,    18,    19,    20,    21,
      22,    23,    24,    -1,    26,    -1,    -1,    58,    59,    60,
      61,    62,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      58,    59,    60,    61,    62,    63,    17,    18,    19,    20,
      21,    22,    23,    24,    -1,    26,    58,    59,    60,    61,
      62,    63,    17,    18,    19,    20,    21,    22,    23,    -1,
      -1,    26,    -1,    -1,    45,    17,    18,    19,    20,    21,
      22,    23,    -1,    -1,    -1,    -1,    -1,    58,    59,    60,
      61,    62,    63,    17,    18,    19,    20,    21,    22,    -1,
      -1,    -1,    -1,    58,    59,    60,    61,    62,    63,    17,
      18,    19,    20,    21,    22,    -1,    58,    59,    60,    61,
      62,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    58,    59,    60,    61,    62,    63,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      58,    59,    60,    61,    62,    63
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    39,    41,    54,    66,    67,    88,    89,   101,     3,
       3,    55,     0,    88,    40,    90,    13,     7,     8,     9,
      10,    57,    71,    86,     3,    15,    86,    92,    55,    32,
      93,    95,    14,     3,    56,    72,     3,     4,     5,     6,
      11,    12,    18,    25,    32,    36,    37,    38,    44,    56,
      76,    87,   109,   110,   111,    95,    87,    44,    87,    87,
      87,   112,   113,    44,    44,    44,    87,    17,    18,    19,
      20,    21,    22,    23,    24,    26,    33,    58,    59,    60,
      61,    62,    63,    32,    34,    44,    15,     3,    68,    69,
      70,    33,    16,    87,    87,    87,    45,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,     3,    77,    78,    87,    13,    45,    16,
      87,    16,    16,    45,    33,    45,    16,    35,    86,    91,
      46,    79,    70,    87,    87,    87,    86,    32,    94,    95,
      96,     3,    27,    35,    42,    48,    51,    53,    73,    74,
      75,    76,    79,    80,    81,    83,    84,    85,    97,    99,
     101,   102,   104,   106,   109,    45,    45,    33,    96,    13,
       3,     3,   100,   103,    79,    44,    44,    15,    87,    74,
      15,    47,    15,    16,    14,    92,    28,    13,    15,    16,
      43,   107,   108,    87,    87,    15,     3,    97,    87,    14,
      98,    87,    86,   103,    44,   108,    45,    45,    15,    87,
      29,    98,     3,    79,    79,    87,     3,    49,    50,    82,
      52,    30,   105,    45,    44,    79,    15,    87,    79,    79,
      87,    31,    45,    15,    79,    82
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    65,    66,    66,    67,    68,    68,    69,    69,    70,
      71,    71,    72,    72,    73,    73,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    75,    75,    76,
      76,    76,    76,    77,    77,    78,    78,    79,    80,    80,
      81,    82,    82,    82,    83,    84,    85,    86,    86,    86,
      86,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    88,    88,    88,
      89,    90,    90,    91,    91,    91,    92,    92,    93,    93,
      94,    94,    95,    96,    96,    97,    98,    98,    99,    99,
     100,   100,   101,   102,   103,   104,   105,   105,   106,   107,
     107,   108,   109,   109,   109,   110,   110,   111,   112,   112,
     113,   113
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     9,     0,     1,     1,     3,     3,
       1,     1,     1,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     3,     2,     4,
       6,     6,     4,     0,     1,     1,     3,     3,     0,     1,
       6,     0,     6,     2,     7,     2,     4,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     2,     2,     3,     1,     1,     1,
       4,     0,     2,     1,     2,     2,     1,     2,     1,     2,
       1,     2,     3,     1,     2,     4,     0,     2,     1,     3,
       1,     3,     7,     3,     4,    10,     0,     2,     3,     1,
       2,     6,     1,     3,     4,     1,     1,     3,     0,     1,
       1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        YY_LAC_DISCARD ("YYBACKUP");                              \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, ctx, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location, ctx); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, ContextoSintactico *ctx)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  YY_USE (ctx);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, ContextoSintactico *ctx)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp, ctx);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule, ContextoSintactico *ctx)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]), ctx);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule, ctx); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Given a state stack such that *YYBOTTOM is its bottom, such that
   *YYTOP is either its top or is YYTOP_EMPTY to indicate an empty
   stack, and such that *YYCAPACITY is the maximum number of elements it
   can hold without a reallocation, make sure there is enough room to
   store YYADD more elements.  If not, allocate a new stack using
   YYSTACK_ALLOC, copy the existing elements, and adjust *YYBOTTOM,
   *YYTOP, and *YYCAPACITY to reflect the new capacity and memory
   location.  If *YYBOTTOM != YYBOTTOM_NO_FREE, then free the old stack
   using YYSTACK_FREE.  Return 0 if successful or if no reallocation is
   required.  Return YYENOMEM if memory is exhausted.  */
static int
yy_lac_stack_realloc (YYPTRDIFF_T *yycapacity, YYPTRDIFF_T yyadd,
#if YYDEBUG
                      char const *yydebug_prefix,
                      char const *yydebug_suffix,
#endif
                      yy_state_t **yybottom,
                      yy_state_t *yybottom_no_free,
                      yy_state_t **yytop, yy_state_t *yytop_empty)
{
  YYPTRDIFF_T yysize_old =
    *yytop == yytop_empty ? 0 : *yytop - *yybottom + 1;
  YYPTRDIFF_T yysize_new = yysize_old + yyadd;
  if (*yycapacity < yysize_new)
    {
      YYPTRDIFF_T yyalloc = 2 * yysize_new;
      yy_state_t *yybottom_new;
      /* Use YYMAXDEPTH for maximum stack size given that the stack
         should never need to grow larger than the main state stack
         needs to grow without LAC.  */
      if (YYMAXDEPTH < yysize_new)
        {
          YYDPRINTF ((stderr, "%smax size exceeded%s", yydebug_prefix,
                      yydebug_suffix));
          return YYENOMEM;
        }
      if (YYMAXDEPTH < yyalloc)
        yyalloc = YYMAXDEPTH;
      yybottom_new =
        YY_CAST (yy_state_t *,
                 YYSTACK_ALLOC (YY_CAST (YYSIZE_T,
                                         yyalloc * YYSIZEOF (*yybottom_new))));
      if (!yybottom_new)
        {
          YYDPRINTF ((stderr, "%srealloc failed%s", yydebug_prefix,
                      yydebug_suffix));
          return YYENOMEM;
        }
      if (*yytop != yytop_empty)
        {
          YYCOPY (yybottom_new, *yybottom, yysize_old);
          *yytop = yybottom_new + (yysize_old - 1);
        }
      if (*yybottom != yybottom_no_free)
        YYSTACK_FREE (*yybottom);
      *yybottom = yybottom_new;
      *yycapacity = yyalloc;
    }
  return 0;
}

/* Establish the initial context for the current lookahead if no initial
   context is currently established.

   We define a context as a snapshot of the parser stacks.  We define
   the initial context for a lookahead as the context in which the
   parser initially examines that lookahead in order to select a
   syntactic action.  Thus, if the lookahead eventually proves
   syntactically unacceptable (possibly in a later context reached via a
   series of reductions), the initial context can be used to determine
   the exact set of tokens that would be syntactically acceptable in the
   lookahead's place.  Moreover, it is the context after which any
   further semantic actions would be erroneous because they would be
   determined by a syntactically unacceptable token.

   YY_LAC_ESTABLISH should be invoked when a reduction is about to be
   performed in an inconsistent state (which, for the purposes of LAC,
   includes consistent states that don't know they're consistent because
   their default reductions have been disabled).  Iff there is a
   lookahead token, it should also be invoked before reporting a syntax
   error.  This latter case is for the sake of the debugging output.

   For parse.lac=full, the implementation of YY_LAC_ESTABLISH is as
   follows.  If no initial context is currently established for the
   current lookahead, then check if that lookahead can eventually be
   shifted if syntactic actions continue from the current context.
   Report a syntax error if it cannot.  */
#define YY_LAC_ESTABLISH                                                \
do {                                                                    \
  if (!yy_lac_established)                                              \
    {                                                                   \
      YYDPRINTF ((stderr,                                               \
                  "LAC: initial context established for %s\n",          \
                  yysymbol_name (yytoken)));                            \
      yy_lac_established = 1;                                           \
      switch (yy_lac (yyesa, &yyes, &yyes_capacity, yyssp, yytoken))    \
        {                                                               \
        case YYENOMEM:                                                  \
          YYNOMEM;                                                      \
        case 1:                                                         \
          goto yyerrlab;                                                \
        }                                                               \
    }                                                                   \
} while (0)

/* Discard any previous initial lookahead context because of Event,
   which may be a lookahead change or an invalidation of the currently
   established initial context for the current lookahead.

   The most common example of a lookahead change is a shift.  An example
   of both cases is syntax error recovery.  That is, a syntax error
   occurs when the lookahead is syntactically erroneous for the
   currently established initial context, so error recovery manipulates
   the parser stacks to try to find a new initial context in which the
   current lookahead is syntactically acceptable.  If it fails to find
   such a context, it discards the lookahead.  */
#if YYDEBUG
# define YY_LAC_DISCARD(Event)                                           \
do {                                                                     \
  if (yy_lac_established)                                                \
    {                                                                    \
      YYDPRINTF ((stderr, "LAC: initial context discarded due to "       \
                  Event "\n"));                                          \
      yy_lac_established = 0;                                            \
    }                                                                    \
} while (0)
#else
# define YY_LAC_DISCARD(Event) yy_lac_established = 0
#endif

/* Given the stack whose top is *YYSSP, return 0 iff YYTOKEN can
   eventually (after perhaps some reductions) be shifted, return 1 if
   not, or return YYENOMEM if memory is exhausted.  As preconditions and
   postconditions: *YYES_CAPACITY is the allocated size of the array to
   which *YYES points, and either *YYES = YYESA or *YYES points to an
   array allocated with YYSTACK_ALLOC.  yy_lac may overwrite the
   contents of either array, alter *YYES and *YYES_CAPACITY, and free
   any old *YYES other than YYESA.  */
static int
yy_lac (yy_state_t *yyesa, yy_state_t **yyes,
        YYPTRDIFF_T *yyes_capacity, yy_state_t *yyssp, yysymbol_kind_t yytoken)
{
  yy_state_t *yyes_prev = yyssp;
  yy_state_t *yyesp = yyes_prev;
  /* Reduce until we encounter a shift and thereby accept the token.  */
  YYDPRINTF ((stderr, "LAC: checking lookahead %s:", yysymbol_name (yytoken)));
  if (yytoken == YYSYMBOL_YYUNDEF)
    {
      YYDPRINTF ((stderr, " Always Err\n"));
      return 1;
    }
  while (1)
    {
      int yyrule = yypact[+*yyesp];
      if (yypact_value_is_default (yyrule)
          || (yyrule += yytoken) < 0 || YYLAST < yyrule
          || yycheck[yyrule] != yytoken)
        {
          /* Use the default action.  */
          yyrule = yydefact[+*yyesp];
          if (yyrule == 0)
            {
              YYDPRINTF ((stderr, " Err\n"));
              return 1;
            }
        }
      else
        {
          /* Use the action from yytable.  */
          yyrule = yytable[yyrule];
          if (yytable_value_is_error (yyrule))
            {
              YYDPRINTF ((stderr, " Err\n"));
              return 1;
            }
          if (0 < yyrule)
            {
              YYDPRINTF ((stderr, " S%d\n", yyrule));
              return 0;
            }
          yyrule = -yyrule;
        }
      /* By now we know we have to simulate a reduce.  */
      YYDPRINTF ((stderr, " R%d", yyrule - 1));
      {
        /* Pop the corresponding number of values from the stack.  */
        YYPTRDIFF_T yylen = yyr2[yyrule];
        /* First pop from the LAC stack as many tokens as possible.  */
        if (yyesp != yyes_prev)
          {
            YYPTRDIFF_T yysize = yyesp - *yyes + 1;
            if (yylen < yysize)
              {
                yyesp -= yylen;
                yylen = 0;
              }
            else
              {
                yyesp = yyes_prev;
                yylen -= yysize;
              }
          }
        /* Only afterwards look at the main stack.  */
        if (yylen)
          yyesp = yyes_prev -= yylen;
      }
      /* Push the resulting state of the reduction.  */
      {
        yy_state_fast_t yystate;
        {
          const int yylhs = yyr1[yyrule] - YYNTOKENS;
          const int yyi = yypgoto[yylhs] + *yyesp;
          yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyesp
                     ? yytable[yyi]
                     : yydefgoto[yylhs]);
        }
        if (yyesp == yyes_prev)
          {
            yyesp = *yyes;
            YY_IGNORE_USELESS_CAST_BEGIN
            *yyesp = YY_CAST (yy_state_t, yystate);
            YY_IGNORE_USELESS_CAST_END
          }
        else
          {
            if (yy_lac_stack_realloc (yyes_capacity, 1,
#if YYDEBUG
                                      " (", ")",
#endif
                                      yyes, yyesa, &yyesp, yyes_prev))
              {
                YYDPRINTF ((stderr, "\n"));
                return YYENOMEM;
              }
            YY_IGNORE_USELESS_CAST_BEGIN
            *++yyesp = YY_CAST (yy_state_t, yystate);
            YY_IGNORE_USELESS_CAST_END
          }
        YYDPRINTF ((stderr, " G%d", yystate));
      }
    }
}

/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yy_state_t *yyesa;
  yy_state_t **yyes;
  YYPTRDIFF_T *yyes_capacity;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;

  int yyx;
  for (yyx = 0; yyx < YYNTOKENS; ++yyx)
    {
      yysymbol_kind_t yysym = YY_CAST (yysymbol_kind_t, yyx);
      if (yysym != YYSYMBOL_YYerror && yysym != YYSYMBOL_YYUNDEF)
        switch (yy_lac (yyctx->yyesa, yyctx->yyes, yyctx->yyes_capacity, yyctx->yyssp, yysym))
          {
          case YYENOMEM:
            return YYENOMEM;
          case 1:
            continue;
          default:
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = yysym;
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif



static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
       In the first two cases, it might appear that the current syntax
       error should have been detected in the previous state when yy_lac
       was invoked.  However, at that time, there might have been a
       different syntax error that discarded a different initial context
       during error recovery, leaving behind the current lookahead.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      YYDPRINTF ((stderr, "Constructing syntax error message\n"));
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else if (yyn == 0)
        YYDPRINTF ((stderr, "No expected tokens.\n"));
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.  In order to see if a particular token T is a
   valid looakhead, invoke yy_lac (YYESA, YYES, YYES_CAPACITY, YYSSP, T).

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store or if
   yy_lac returned YYENOMEM.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yystrlen (yysymbol_name (yyarg[yyi]));
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp = yystpcpy (yyp, yysymbol_name (yyarg[yyi++]));
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, ContextoSintactico *ctx)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  YY_USE (ctx);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (ContextoSintactico *ctx)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

    yy_state_t yyesa[20];
    yy_state_t *yyes = yyesa;
    YYPTRDIFF_T yyes_capacity = 20 < YYMAXDEPTH ? 20 : YYMAXDEPTH;

  /* Whether LAC context is established.  A Boolean.  */
  int yy_lac_established = 0;
  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */


/* User initialization code.  */
#line 14 "src/Analisis_Sintactico/gramatica.y"
{ yylloc = (AstUbicacion){1, 1, 1, 1}; }

#line 1802 "src/Analisis_Sintactico/parser.c"

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc, ctx);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    {
      YY_LAC_ESTABLISH;
      goto yydefault;
    }
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      YY_LAC_ESTABLISH;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  YY_LAC_DISCARD ("shift");
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  {
    int yychar_backup = yychar;
    switch (yyn)
      {
  case 2: /* programa: elemento_superior  */
#line 77 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_PROGRAMA, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
        ctx->arbol->raiz = yyval;
      }
#line 2027 "src/Analisis_Sintactico/parser.c"
    break;

  case 3: /* programa: programa elemento_superior  */
#line 84 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2033 "src/Analisis_Sintactico/parser.c"
    break;

  case 4: /* funcion: CREATE_FUNK HASH tipo_retorno HASH nombre_funcion LPAREN parametros_opcionales RPAREN bloque  */
#line 90 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_FUNCION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-4]);
        ast_agregar_hijo(yyval, yyvsp[-6]);
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2046 "src/Analisis_Sintactico/parser.c"
    break;

  case 5: /* parametros_opcionales: %empty  */
#line 103 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_PARAMETROS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2055 "src/Analisis_Sintactico/parser.c"
    break;

  case 6: /* parametros_opcionales: parametros  */
#line 108 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2061 "src/Analisis_Sintactico/parser.c"
    break;

  case 7: /* parametros: parametro  */
#line 113 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_PARAMETROS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2071 "src/Analisis_Sintactico/parser.c"
    break;

  case 8: /* parametros: parametros COMMA parametro  */
#line 119 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-2]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2077 "src/Analisis_Sintactico/parser.c"
    break;

  case 9: /* parametro: IDENTIFIER STAR tipo_parametro  */
#line 127 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_PARAMETRO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2088 "src/Analisis_Sintactico/parser.c"
    break;

  case 14: /* sentencias: sentencia  */
#line 149 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BLOQUE, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2098 "src/Analisis_Sintactico/parser.c"
    break;

  case 15: /* sentencias: sentencias sentencia  */
#line 155 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2104 "src/Analisis_Sintactico/parser.c"
    break;

  case 27: /* retorno: GIVE expresion SEMICOLON  */
#line 176 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_RETORNO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2114 "src/Analisis_Sintactico/parser.c"
    break;

  case 28: /* retorno: GIVE SEMICOLON  */
#line 182 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_RETORNO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2123 "src/Analisis_Sintactico/parser.c"
    break;

  case 29: /* llamada: destino_llamada LPAREN argumentos_opcionales RPAREN  */
#line 193 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_LLAMADA, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2134 "src/Analisis_Sintactico/parser.c"
    break;

  case 30: /* llamada: ADD LPAREN expresion COMMA expresion RPAREN  */
#line 200 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_AGREGAR, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2145 "src/Analisis_Sintactico/parser.c"
    break;

  case 31: /* llamada: REMOVE LPAREN expresion COMMA expresion RPAREN  */
#line 207 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ELIMINAR, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2156 "src/Analisis_Sintactico/parser.c"
    break;

  case 32: /* llamada: SIZE LPAREN expresion RPAREN  */
#line 214 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_TAMANO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2166 "src/Analisis_Sintactico/parser.c"
    break;

  case 33: /* argumentos_opcionales: %empty  */
#line 223 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ARGUMENTOS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2175 "src/Analisis_Sintactico/parser.c"
    break;

  case 34: /* argumentos_opcionales: argumentos  */
#line 228 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2181 "src/Analisis_Sintactico/parser.c"
    break;

  case 35: /* argumentos: expresion  */
#line 233 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ARGUMENTOS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2191 "src/Analisis_Sintactico/parser.c"
    break;

  case 36: /* argumentos: argumentos COMMA expresion  */
#line 239 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-2]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2197 "src/Analisis_Sintactico/parser.c"
    break;

  case 37: /* bloque: LBRACE contenido_bloque RBRACE  */
#line 245 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); }
#line 2203 "src/Analisis_Sintactico/parser.c"
    break;

  case 38: /* contenido_bloque: %empty  */
#line 250 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BLOQUE, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2212 "src/Analisis_Sintactico/parser.c"
    break;

  case 39: /* contenido_bloque: sentencias  */
#line 255 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2218 "src/Analisis_Sintactico/parser.c"
    break;

  case 40: /* condicional: WHETHER LPAREN expresion RPAREN bloque alternativa  */
#line 264 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_SI, "whether", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2230 "src/Analisis_Sintactico/parser.c"
    break;

  case 41: /* alternativa: %empty  */
#line 276 "src/Analisis_Sintactico/gramatica.y"
      { yyval = NULL; }
#line 2236 "src/Analisis_Sintactico/parser.c"
    break;

  case 42: /* alternativa: ALIF LPAREN expresion RPAREN bloque alternativa  */
#line 278 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_SI, "alif", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2248 "src/Analisis_Sintactico/parser.c"
    break;

  case 43: /* alternativa: ALSO bloque  */
#line 286 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2254 "src/Analisis_Sintactico/parser.c"
    break;

  case 44: /* ciclo: WHALE LPAREN expresion RPAREN bloque STOP SEMICOLON  */
#line 295 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_MIENTRAS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-4]);
        ast_agregar_hijo(yyval, yyvsp[-2]);
      }
#line 2265 "src/Analisis_Sintactico/parser.c"
    break;

  case 45: /* declaracion: declaradores SEMICOLON  */
#line 306 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); }
#line 2271 "src/Analisis_Sintactico/parser.c"
    break;

  case 46: /* asignacion: referencia ASSIGN expresion SEMICOLON  */
#line 314 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ASIGNACION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2282 "src/Analisis_Sintactico/parser.c"
    break;

  case 59: /* expresion: expresion EQUAL expresion  */
#line 342 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "==", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2293 "src/Analisis_Sintactico/parser.c"
    break;

  case 60: /* expresion: expresion NOT_EQUAL expresion  */
#line 349 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "=/=", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2304 "src/Analisis_Sintactico/parser.c"
    break;

  case 61: /* expresion: expresion LESS expresion  */
#line 356 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "<", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2315 "src/Analisis_Sintactico/parser.c"
    break;

  case 62: /* expresion: expresion GREATER expresion  */
#line 363 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, ">", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2326 "src/Analisis_Sintactico/parser.c"
    break;

  case 63: /* expresion: expresion LESS_EQUAL expresion  */
#line 370 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "<=", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2337 "src/Analisis_Sintactico/parser.c"
    break;

  case 64: /* expresion: expresion GREATER_EQUAL expresion  */
#line 377 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, ">=", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2348 "src/Analisis_Sintactico/parser.c"
    break;

  case 65: /* expresion: expresion GAUSS expresion  */
#line 384 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "gauss", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2359 "src/Analisis_Sintactico/parser.c"
    break;

  case 66: /* expresion: expresion NEUMANN expresion  */
#line 391 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "neumann", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2370 "src/Analisis_Sintactico/parser.c"
    break;

  case 67: /* expresion: expresion PITAGORAS expresion  */
#line 398 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "pitagoras", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2381 "src/Analisis_Sintactico/parser.c"
    break;

  case 68: /* expresion: expresion EUCLIDES expresion  */
#line 405 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "euclides", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2392 "src/Analisis_Sintactico/parser.c"
    break;

  case 69: /* expresion: expresion AND expresion  */
#line 412 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "&&", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2403 "src/Analisis_Sintactico/parser.c"
    break;

  case 70: /* expresion: expresion OR expresion  */
#line 419 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "||", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2414 "src/Analisis_Sintactico/parser.c"
    break;

  case 71: /* expresion: expresion XOR expresion  */
#line 426 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "^", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2425 "src/Analisis_Sintactico/parser.c"
    break;

  case 72: /* expresion: expresion EULER expresion  */
#line 433 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "euler", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2436 "src/Analisis_Sintactico/parser.c"
    break;

  case 73: /* expresion: expresion DESCARTES expresion  */
#line 440 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_BINARIO, "descartes", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2447 "src/Analisis_Sintactico/parser.c"
    break;

  case 74: /* expresion: NOT expresion  */
#line 447 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_UNARIO, "~", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2457 "src/Analisis_Sintactico/parser.c"
    break;

  case 75: /* expresion: NEUMANN expresion  */
#line 453 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_UNARIO, "neumann", (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2467 "src/Analisis_Sintactico/parser.c"
    break;

  case 76: /* expresion: LPAREN expresion RPAREN  */
#line 459 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; }
#line 2473 "src/Analisis_Sintactico/parser.c"
    break;

  case 77: /* elemento_superior: funcion  */
#line 466 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2479 "src/Analisis_Sintactico/parser.c"
    break;

  case 78: /* elemento_superior: importacion  */
#line 468 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2485 "src/Analisis_Sintactico/parser.c"
    break;

  case 79: /* elemento_superior: constante  */
#line 470 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2491 "src/Analisis_Sintactico/parser.c"
    break;

  case 80: /* importacion: BRING IDENTIFIER alias_opcional SEMICOLON  */
#line 475 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_IMPORTACION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2502 "src/Analisis_Sintactico/parser.c"
    break;

  case 81: /* alias_opcional: %empty  */
#line 485 "src/Analisis_Sintactico/gramatica.y"
      { yyval = NULL; }
#line 2508 "src/Analisis_Sintactico/parser.c"
    break;

  case 82: /* alias_opcional: AKA IDENTIFIER  */
#line 487 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2514 "src/Analisis_Sintactico/parser.c"
    break;

  case 83: /* tipo_parametro: tipo  */
#line 492 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2520 "src/Analisis_Sintactico/parser.c"
    break;

  case 84: /* tipo_parametro: tipo dimensiones_parametro  */
#line 494 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_TIPO_ARREGLO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2531 "src/Analisis_Sintactico/parser.c"
    break;

  case 85: /* tipo_parametro: DECLARE_LIST tipo  */
#line 501 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_TIPO_LISTA, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2541 "src/Analisis_Sintactico/parser.c"
    break;

  case 86: /* tipo_variable: tipo  */
#line 510 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2547 "src/Analisis_Sintactico/parser.c"
    break;

  case 87: /* tipo_variable: tipo dimensiones  */
#line 512 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_TIPO_ARREGLO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2558 "src/Analisis_Sintactico/parser.c"
    break;

  case 88: /* dimensiones: dimension  */
#line 522 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_DIMENSIONES, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2568 "src/Analisis_Sintactico/parser.c"
    break;

  case 89: /* dimensiones: dimensiones dimension  */
#line 528 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2574 "src/Analisis_Sintactico/parser.c"
    break;

  case 90: /* dimensiones_parametro: dimension_parametro  */
#line 533 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_DIMENSIONES, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2584 "src/Analisis_Sintactico/parser.c"
    break;

  case 91: /* dimensiones_parametro: dimensiones_parametro dimension_parametro  */
#line 539 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2590 "src/Analisis_Sintactico/parser.c"
    break;

  case 92: /* dimension: LBRACKET expresion RBRACKET  */
#line 544 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_DIMENSION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2600 "src/Analisis_Sintactico/parser.c"
    break;

  case 93: /* dimension_parametro: dimension  */
#line 553 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2606 "src/Analisis_Sintactico/parser.c"
    break;

  case 94: /* dimension_parametro: LBRACKET RBRACKET  */
#line 555 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_DIMENSION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2615 "src/Analisis_Sintactico/parser.c"
    break;

  case 95: /* declarador: IDENTIFIER STAR tipo_variable inicializador_opcional  */
#line 563 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_DECLARACION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2627 "src/Analisis_Sintactico/parser.c"
    break;

  case 96: /* inicializador_opcional: %empty  */
#line 574 "src/Analisis_Sintactico/gramatica.y"
      { yyval = NULL; }
#line 2633 "src/Analisis_Sintactico/parser.c"
    break;

  case 97: /* inicializador_opcional: ASSIGN expresion  */
#line 576 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2639 "src/Analisis_Sintactico/parser.c"
    break;

  case 98: /* declaradores: declarador  */
#line 581 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2645 "src/Analisis_Sintactico/parser.c"
    break;

  case 99: /* declaradores: declaradores COMMA declarador  */
#line 583 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = yyvsp[-2];
        if (yyval->tipo != AST_DECLARACIONES) {
            yyval = ast_crear(ctx->arbol, AST_DECLARACIONES, NULL, (yyloc));
            if (!yyval) YYNOMEM;
            ast_agregar_hijo(yyval, yyvsp[-2]);
        }
        yyval->ubicacion = (yyloc);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2660 "src/Analisis_Sintactico/parser.c"
    break;

  case 100: /* listas: declarador_lista  */
#line 597 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2666 "src/Analisis_Sintactico/parser.c"
    break;

  case 101: /* listas: listas COMMA declarador_lista  */
#line 599 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = yyvsp[-2];
        if (yyval->tipo != AST_DECLARACIONES) {
            yyval = ast_crear(ctx->arbol, AST_DECLARACIONES, NULL, (yyloc));
            if (!yyval) YYNOMEM;
            ast_agregar_hijo(yyval, yyvsp[-2]);
        }
        yyval->ubicacion = (yyloc);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2681 "src/Analisis_Sintactico/parser.c"
    break;

  case 102: /* constante: DECLARE_CONST IDENTIFIER STAR tipo_variable ASSIGN expresion SEMICOLON  */
#line 613 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_CONSTANTE, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-5]);
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2693 "src/Analisis_Sintactico/parser.c"
    break;

  case 103: /* declaracion_lista: DECLARE_LIST listas SEMICOLON  */
#line 624 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); }
#line 2699 "src/Analisis_Sintactico/parser.c"
    break;

  case 104: /* declarador_lista: IDENTIFIER STAR tipo inicializador_opcional  */
#line 629 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_LISTA, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2711 "src/Analisis_Sintactico/parser.c"
    break;

  case 105: /* recorrido: CYCLE IDENTIFIER LET expresion UNTIL expresion paso_opcional bloque ENDGAME SEMICOLON  */
#line 640 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_RECORRIDO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-8]);
        ast_agregar_hijo(yyval, yyvsp[-6]);
        ast_agregar_hijo(yyval, yyvsp[-4]);
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-2]);
      }
#line 2725 "src/Analisis_Sintactico/parser.c"
    break;

  case 106: /* paso_opcional: %empty  */
#line 653 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ENTERO, "1", (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2734 "src/Analisis_Sintactico/parser.c"
    break;

  case 107: /* paso_opcional: STEP expresion  */
#line 658 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2740 "src/Analisis_Sintactico/parser.c"
    break;

  case 108: /* intentar: SEEK bloque capturas  */
#line 663 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_INTENTAR, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-1]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2751 "src/Analisis_Sintactico/parser.c"
    break;

  case 109: /* capturas: captura  */
#line 673 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_CAPTURAS, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2761 "src/Analisis_Sintactico/parser.c"
    break;

  case 110: /* capturas: capturas captura  */
#line 679 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2767 "src/Analisis_Sintactico/parser.c"
    break;

  case 111: /* captura: SEIZE LPAREN IDENTIFIER IDENTIFIER RPAREN bloque  */
#line 684 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_CAPTURA, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2779 "src/Analisis_Sintactico/parser.c"
    break;

  case 112: /* referencia: IDENTIFIER  */
#line 695 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2785 "src/Analisis_Sintactico/parser.c"
    break;

  case 113: /* referencia: referencia DOT IDENTIFIER  */
#line 697 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_ACCESO_MIEMBRO, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-2]);
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2796 "src/Analisis_Sintactico/parser.c"
    break;

  case 114: /* referencia: referencia LBRACKET expresion RBRACKET  */
#line 704 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_INDICE, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[-3]);
        ast_agregar_hijo(yyval, yyvsp[-1]);
      }
#line 2807 "src/Analisis_Sintactico/parser.c"
    break;

  case 115: /* destino_llamada: referencia  */
#line 714 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2813 "src/Analisis_Sintactico/parser.c"
    break;

  case 116: /* destino_llamada: MAIN  */
#line 716 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2819 "src/Analisis_Sintactico/parser.c"
    break;

  case 117: /* literal_coleccion: LBRACKET elementos_opcionales RBRACKET  */
#line 721 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-1]; yyval->ubicacion = (yyloc); }
#line 2825 "src/Analisis_Sintactico/parser.c"
    break;

  case 118: /* elementos_opcionales: %empty  */
#line 726 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_LITERAL_COLECCION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
      }
#line 2834 "src/Analisis_Sintactico/parser.c"
    break;

  case 119: /* elementos_opcionales: elementos  */
#line 731 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[0]; }
#line 2840 "src/Analisis_Sintactico/parser.c"
    break;

  case 120: /* elementos: expresion  */
#line 736 "src/Analisis_Sintactico/gramatica.y"
      {
        yyval = ast_crear(ctx->arbol, AST_LITERAL_COLECCION, NULL, (yyloc));
        if (!yyval) YYNOMEM;
        ast_agregar_hijo(yyval, yyvsp[0]);
      }
#line 2850 "src/Analisis_Sintactico/parser.c"
    break;

  case 121: /* elementos: elementos COMMA expresion  */
#line 742 "src/Analisis_Sintactico/gramatica.y"
      { yyval = yyvsp[-2]; ast_agregar_hijo(yyval, yyvsp[0]); }
#line 2856 "src/Analisis_Sintactico/parser.c"
    break;


#line 2860 "src/Analisis_Sintactico/parser.c"

        default: break;
      }
    if (yychar_backup != yychar)
      YY_LAC_DISCARD ("yychar change");
  }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yyesa, &yyes, &yyes_capacity, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        if (yychar != YYEMPTY)
          YY_LAC_ESTABLISH;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, ctx, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc, ctx);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp, ctx);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  /* If the stack popping above didn't lose the initial context for the
     current lookahead token, the shift below will for sure.  */
  YY_LAC_DISCARD ("error recovery");

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, ctx, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc, ctx);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp, ctx);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yyes != yyesa)
    YYSTACK_FREE (yyes);
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 745 "src/Analisis_Sintactico/gramatica.y"


/* yylex y yyerror estan implementadas en puente_lexer.c.
 * Despues de este segundo separador solo va codigo C, no reglas.
 * Ejemplo de entrada:
 * create_funk #declare_infinite_void# main() {
 *     resultado * declare_int : 0;
 *     resultado : 2 gauss 3 pitagoras 4;
 *     cumple * declare_boolean : resultado <= 14;
 * }
 */
