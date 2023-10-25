#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct editor_syntax {
   char * filetype;
   char ** filematch;
   char ** keywords;
   char * singleline_comment_start;
   char * multiline_comment_start;
   char * multiline_comment_end;
   int flags;
};

// === C/CPP ===
char * C_HL_extensions[] = { ".c", ".h", ".cpp", ".hpp", NULL };
char * C_HL_keywords[] = {
   "switch", "if", "while", "for", "break", "continue", "return", "else", "struct",
   "union", "typedef", "static", "enum", "class", "case", "true", "false", "auto",
   "const", "default", "do", "enum", "extern", "public", "private", "sizeof", 
   "union", "volatile", "#define", "#include", 

   "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
   "void|", "bool|", NULL
};

// === PYTHON ===
char * PY_HL_extensions[] = { ".py", NULL };
char * PY_HL_keywords[] = {
   "and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
   "else", "except", "finally", "for", "from", "global", "if", "import", "in",
   "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
   "while", "with", "yield",

   "False|", "None|", "True|", "False:|", "True|", NULL
};

// HIGHLIGHT DATABASE
struct editor_syntax HLDB[] = {
   {
      "c",
      C_HL_extensions,
      C_HL_keywords,
      "//", "/*", "*/",
      HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
   },
   {
      "python",
      PY_HL_extensions,
      PY_HL_keywords,
      "#", "\"\"\"", "\"\"\"",
      HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
   }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

