/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_SRC_ANALISIS_SINTACTICO_PARSER_H_INCLUDED
# define YY_YY_SRC_ANALISIS_SINTACTICO_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 18 "src/Analisis_Sintactico/gramatica.y"

#include "sintactico.h"

#line 53 "src/Analisis_Sintactico/parser.h"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    IDENTIFIER = 258,              /* IDENTIFIER  */
    INTEGER = 259,                 /* INTEGER  */
    STRING = 260,                  /* STRING  */
    CHARACTER = 261,               /* CHARACTER  */
    DECLARE_INT = 262,             /* DECLARE_INT  */
    DECLARE_BOOLEAN = 263,         /* DECLARE_BOOLEAN  */
    DECLARE_TEXT = 264,            /* DECLARE_TEXT  */
    DECLARE_CHAR = 265,            /* DECLARE_CHAR  */
    DECLARE_TRUE = 266,            /* DECLARE_TRUE  */
    DECLARE_FALSE = 267,           /* DECLARE_FALSE  */
    STAR = 268,                    /* STAR  */
    ASSIGN = 269,                  /* ASSIGN  */
    SEMICOLON = 270,               /* SEMICOLON  */
    COMMA = 271,                   /* COMMA  */
    GAUSS = 272,                   /* GAUSS  */
    NEUMANN = 273,                 /* NEUMANN  */
    PITAGORAS = 274,               /* PITAGORAS  */
    EUCLIDES = 275,                /* EUCLIDES  */
    EULER = 276,                   /* EULER  */
    DESCARTES = 277,               /* DESCARTES  */
    AND = 278,                     /* AND  */
    OR = 279,                      /* OR  */
    NOT = 280,                     /* NOT  */
    XOR = 281,                     /* XOR  */
    CYCLE = 282,                   /* CYCLE  */
    LET = 283,                     /* LET  */
    UNTIL = 284,                   /* UNTIL  */
    STEP = 285,                    /* STEP  */
    ENDGAME = 286,                 /* ENDGAME  */
    LBRACKET = 287,                /* LBRACKET  */
    RBRACKET = 288,                /* RBRACKET  */
    DOT = 289,                     /* DOT  */
    DECLARE_LIST = 290,            /* DECLARE_LIST  */
    ADD = 291,                     /* ADD  */
    REMOVE = 292,                  /* REMOVE  */
    SIZE = 293,                    /* SIZE  */
    BRING = 294,                   /* BRING  */
    AKA = 295,                     /* AKA  */
    DECLARE_CONST = 296,           /* DECLARE_CONST  */
    SEEK = 297,                    /* SEEK  */
    SEIZE = 298,                   /* SEIZE  */
    LPAREN = 299,                  /* LPAREN  */
    RPAREN = 300,                  /* RPAREN  */
    LBRACE = 301,                  /* LBRACE  */
    RBRACE = 302,                  /* RBRACE  */
    WHETHER = 303,                 /* WHETHER  */
    ALIF = 304,                    /* ALIF  */
    ALSO = 305,                    /* ALSO  */
    WHALE = 306,                   /* WHALE  */
    STOP = 307,                    /* STOP  */
    GIVE = 308,                    /* GIVE  */
    CREATE_FUNK = 309,             /* CREATE_FUNK  */
    HASH = 310,                    /* HASH  */
    MAIN = 311,                    /* MAIN  */
    DECLARE_INFINITE_VOID = 312,   /* DECLARE_INFINITE_VOID  */
    EQUAL = 313,                   /* EQUAL  */
    NOT_EQUAL = 314,               /* NOT_EQUAL  */
    LESS = 315,                    /* LESS  */
    GREATER = 316,                 /* GREATER  */
    LESS_EQUAL = 317,              /* LESS_EQUAL  */
    GREATER_EQUAL = 318,           /* GREATER_EQUAL  */
    NEGATIVO = 319                 /* NEGATIVO  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef AstNodo * YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
typedef AstUbicacion YYLTYPE;




int yyparse (ContextoSintactico *ctx);

/* "%code provides" blocks.  */
#line 22 "src/Analisis_Sintactico/gramatica.y"

int yylex(YYSTYPE *valor, YYLTYPE *ubicacion, ContextoSintactico *ctx);
void yyerror(YYLTYPE *ubicacion, ContextoSintactico *ctx, const char *mensaje);

#line 151 "src/Analisis_Sintactico/parser.h"

#endif /* !YY_YY_SRC_ANALISIS_SINTACTICO_PARSER_H_INCLUDED  */
