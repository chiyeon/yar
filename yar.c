#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define YAR_VERSION "0.1"
#define YAR_TAB_STOP 8
#define YAR_QUIT_TIMES 1
#define YAR_SHOW_LINE_NUMBERS 1 

// margin for line numbers
#define LEFT_MARGIN " │ "
#define LEFT_MARGIN_SIZE strlen(LEFT_MARGIN) - 2

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

typedef struct erow {
   int idx;
   int size;
   int rsize;
   char * chars;
   char * render;
   unsigned char * hl;
   int hl_open_comment;
} erow;

struct EditorConfig {
   int cx, cy;
   int rx;
   int rowoff;
   int coloff;
   int screenrows;
   int screencols;
   int numrows;
   erow * row;
   int dirty;
   char * filename;
   char statusmsg[80];
   time_t statusmsg_time;
   struct editor_syntax * syntax;
   struct termios orig_termios;
};

struct abuf {
   char *b;
   int len;
};

enum editor_key {
   BACKSPACE = 127,
   ARROW_LEFT = 1000,
   ARROW_RIGHT,
   ARROW_UP,
   ARROW_DOWN,
   PAGE_UP,
   PAGE_DOWN,
   HOME_KEY,
   END_KEY,
   DEL_KEY
};

enum editor_highlight {
   HL_NORMAL = 0,
   HL_COMMENT,
   HL_MLCOMMENT,
   HL_KEYWORD1,
   HL_KEYWORD2,
   HL_STRING,
   HL_NUMBER,
   HL_MATCH
};

void ab_append(struct abuf * ab, const char * s, int len) {
   char * new = realloc(ab->b, ab->len + len);

   if (new == NULL) return;
   memcpy(&new[ab->len], s, len);
   ab->b = new;
   ab->len += len;
}

void ab_free(struct abuf * ab) {
   free(ab->b);
}

int num_digits(int num)
{
   int count = 0;
   while (num != 0) {
      num = num / 10;
      count++;
   }

   return count;
}

struct EditorConfig E;

char * C_HL_extensions[] = { ".c", ".h", ".cpp", ".hpp", NULL };
char * C_HL_keywords[] = {
   "switch", "if", "while", "for", "break", "continue", "return", "else", "struct",
   "union", "typedef", "static", "enum", "class", "case",

   "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
   "void|", NULL
};

struct editor_syntax HLDB[] = {
   {
      "c",
      C_HL_extensions,
      C_HL_keywords,
      "//", "/*", "*/",
      HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
   }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void die(const char * s)
{
   write(STDOUT_FILENO, "\x1b[2J", 4);
   write(STDOUT_FILENO, "\x1b[H", 3);

   perror(s);
   exit(1);
}

void disable_raw_mode() {
   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
      die("tcsetattr");
}

void enable_raw_mode() {
   if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
      die("tcgetattr");
   atexit(disable_raw_mode);

   struct termios raw = E.orig_termios;
   raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
   raw.c_oflag &= ~(OPOST);
   raw.c_cflag |= (CS8);
   raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
   raw.c_cc[VMIN] = 0;
   raw.c_cc[VTIME] = 1;

   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editor_row_cx_to_rx(erow * row, int cx) {
   int rx = 0;
   int j;
   for (j = 0; j < cx; j++) {
      if (row->chars[j] == '\t')
         rx += (YAR_TAB_STOP - 1) - (rx % YAR_TAB_STOP);
      rx++;
   }
   if (YAR_SHOW_LINE_NUMBERS)
      return rx + num_digits(E.numrows) + LEFT_MARGIN_SIZE;
   return rx;
}

int editor_row_rx_to_cx(erow * row, int rx) {
   int cur_rx = 0;
   int cx;
   for (cx = 0; cx < row->size; cx++) {
      if (row->chars[cx] == '\t')
         cur_rx += (YAR_TAB_STOP - 1) - (cur_rx % YAR_TAB_STOP);
      cur_rx++;

      if (cur_rx > rx) return cx;
   }
   if (YAR_SHOW_LINE_NUMBERS)
      return cx - num_digits(E.numrows) - LEFT_MARGIN_SIZE;
   return cx;
}

int editor_syntax_to_color(int hl) {
   switch (hl) {
      case HL_COMMENT:
      case HL_MLCOMMENT: return 36;
      case HL_KEYWORD1: return 33;
      case HL_KEYWORD2: return 32;
      case HL_STRING: return 35;
      case HL_NUMBER: return 31;
      case HL_MATCH: return 34;
      default: return 37;
   }
}

void editor_scroll()
{
   E.rx = 0;
   if (E.cy < E.numrows) {
      E.rx = editor_row_cx_to_rx(&E.row[E.cy], E.cx);
   }

   if (E.cy < E.rowoff) {
      E.rowoff = E.cy;
   }
   if (E.cy >= E.rowoff + E.screenrows) {
      E.rowoff = E.cy - E.screenrows + 1;
   }
   if (E.cx < E.coloff) {
      E.coloff = E.rx;
   }
   if (E.rx >= E.coloff + E.screencols) {
      E.coloff = E.rx - E.screencols + 1;
   }
}

void editor_draw_rows(struct abuf * ab) {
   int y;
   for (y = 0; y < E.screenrows; ++y) {
      int filerow = y + E.rowoff; 
      if (filerow >= E.numrows) {
         if (E.numrows == 0 && y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Yar Text Editor -- Ver. %s", YAR_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
               ab_append(ab, "~", 1);
               padding--;
            }
            while (padding--) ab_append(ab, " ", 1);
            ab_append(ab, welcome, welcomelen);
         } else {
            ab_append(ab, "~", 1);
         }
      } else {
         int total_left_margin_size = num_digits(E.numrows) + LEFT_MARGIN_SIZE;
         char linenum[10]; // i think we can assume we prolly wont be opening million+ line files!
         int linenumlen = snprintf(linenum, sizeof(linenum), "%*d%s", num_digits(E.numrows), (E.rowoff + y + 1), LEFT_MARGIN);
         if (YAR_SHOW_LINE_NUMBERS) {
            ab_append(ab, "\x1b[36m", 5);
            ab_append(ab, linenum, linenumlen);
            ab_append(ab, "\x1b[39m", 5);
         }

         int len = E.row[filerow].rsize - E.coloff;
         if (len < 0) len = 0;
         if (YAR_SHOW_LINE_NUMBERS) {
            if (len > E.screencols - total_left_margin_size) 
               len = E.screencols - total_left_margin_size;
         } else {
            if (len > E.screencols)
               len = E.screencols;
         }
         char * c = &E.row[filerow].render[E.coloff];
         unsigned char * hl = &E.row[filerow].hl[E.coloff];
         int current_color = -1;
         int j;
         for (j = 0; j < len; j++) {
            if (iscntrl(c[j])) {
               char sym = (c[j] <= 26) ? '@' + c[j] : '?';
               ab_append(ab, "\x1b[7m", 4);
               ab_append(ab, &sym, 1);
               ab_append(ab, "\x1b[m", 3);
               if (current_color != -1) {
                  char buf[16];
                  int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                  ab_append(ab, buf, clen);
               }
            } else if (hl[j] == HL_NORMAL) {
               if (current_color != -1) {
                  ab_append(ab, "\x1b[39m", 5);
                  current_color = -1;
               }
               ab_append(ab, &c[j], 1);
            } else {
               int color = editor_syntax_to_color(hl[j]);
               if (color != current_color) {
                  current_color = color;
                  char buf[16];
                  int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                  ab_append(ab, buf, clen);
               }
               ab_append(ab, &c[j], 1);
            }
         }
         ab_append(ab, "\x1b[39m", 5);
      }

      ab_append(ab, "\x1b[K", 3);
      ab_append(ab, "\r\n", 2);
   }
}

void editor_draw_status_bar(struct abuf * ab) 
{
   ab_append(ab, "\x1b[7m", 4);

   char status[80], rstatus[80];
   int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
      E.filename ? E.filename : "[No Name]", E.numrows,
      E.dirty ? "(modified)" : "");
   int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
         E.syntax ? E.syntax->filetype : "no ft",
         E.cy + 1, E.numrows);
   if (len > E.screencols) len = E.screencols;
   ab_append(ab, status, len);

   while (len < E.screencols) {
      if (E.screencols - len == rlen) {
         ab_append(ab, rstatus, rlen);
         break;
      } else {
         ab_append(ab, " ", 1);
         len++;
      }
   }
   ab_append(ab, "\x1b[m", 3);
   ab_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct abuf * ab)
{
   ab_append(ab, "\x1b[K", 3);
   int msglen = strlen(E.statusmsg);
   if (msglen > E.screencols) msglen = E.screencols;
   if (msglen && time(NULL) - E.statusmsg_time < 5)
      ab_append(ab, E.statusmsg, msglen);
}

void editor_refresh_screen() {
   editor_scroll();

   struct abuf ab = ABUF_INIT;

   ab_append(&ab, "\x1b[?25l", 6);
   ab_append(&ab, "\x1b[H", 3);

   editor_draw_rows(&ab);
   editor_draw_status_bar(&ab);
   editor_draw_message_bar(&ab);

   char buf[32];
   snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                             (E.rx - E.coloff) + 1);
   ab_append(&ab, buf, strlen(buf));

   ab_append(&ab, "\x1b[?25h", 6);

   write(STDOUT_FILENO, ab.b, ab.len);
   ab_free(&ab);
}

void editor_set_status_message(const char * fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
   va_end(ap);
   E.statusmsg_time = time(NULL);
}

int editor_read_key() {
   int nread;
   char c;
   while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
      if (nread == -1 && errno != EAGAIN) die("read");
   }

   if (c == '\x1b') {
      char seq[3];

      if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
      if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

      if (seq[0] == '[') {
         if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] == '~') {
               switch (seq[1]) {
                  case '1': return HOME_KEY;
                  case '3': return DEL_KEY;
                  case '4': return END_KEY;
                  case '5': return PAGE_UP;
                  case '6': return PAGE_DOWN;
                  case '7': return HOME_KEY;
                  case '8': return END_KEY;
               }
            }
         } else {
            switch (seq[1]) {
               case 'A': return ARROW_UP;
               case 'B': return ARROW_DOWN;
               case 'C': return ARROW_RIGHT;
               case 'D': return ARROW_LEFT;
               case 'H': return HOME_KEY;
               case 'F': return END_KEY;
            }
         }
      } else if (seq[0] == 'O') {
         switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
         }
      }

      return '\x1b';
   } else {
      return c;
   }
}

int get_cursor_position(int *rows, int *cols)
{
   char buf[32];
   unsigned int i = 0;

   if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

   while (i < sizeof(buf) - 1) {
      if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
      if (buf[i] == 'R') break;
      i++;
   }
   buf[i] = '\0';

   if (buf[i] != '\x1b' || buf[1] != '[') return -1;
   if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

   return 0;
}

int get_window_size(int *rows, int *cols)
{
   struct winsize ws;

   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
      if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
      return get_cursor_position(rows, cols);
   } else {
      *cols = ws.ws_col;
      *rows = ws.ws_row;
      return 0;
   }
}

int is_separator(int c)
{
   return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editor_update_syntax(erow * row)
{
   row->hl = realloc(row->hl, row->rsize);
   memset(row->hl, HL_NORMAL, row->rsize);

   if (E.syntax == NULL) return;

   char ** keywords = E.syntax->keywords;

   char * scs = E.syntax->singleline_comment_start;
   char * mcs = E.syntax->multiline_comment_start;
   char * mce = E.syntax->multiline_comment_end;

   int scs_len = scs ? strlen(scs) : 0;
   int mcs_len = mcs ? strlen(mcs) : 0;
   int mce_len = mce ? strlen(mce) : 0;

   int prev_sep = 1;
   int in_string = 0;
   int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

   int i = 0;
   while (i < row->rsize) {
      char c = row->render[i];
      unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

      if (scs_len && !in_string && !in_comment) {
         if (!strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
         }
      }

      if (mcs_len && mce_len && !in_string) {
         if (in_comment) {
            row->hl[i] == HL_MLCOMMENT;
            if (!strncmp(&row->render[i], mce, mce_len)) {
               memset(&row->hl[i], HL_MLCOMMENT, mce_len);
               i += mce_len;
               in_comment = 0;
               prev_sep = 1;
               continue;
            } else {
               i++;
               continue;
            }
         } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
            memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
            i += mcs_len;
            in_comment = 1;
            continue;
         }
      }

      if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
         if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->rsize) {
               row->hl[i + 1] = HL_STRING;
               i += 2;
               continue;
            }
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = 1;
            continue;
         } else {
            if (c == '"' || c == '\'') {
               in_string = c;
               row->hl[i] = HL_STRING;
               i++;
               continue;
            }
         }
      }

      if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
         if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
               (c == '.' && prev_hl == HL_NUMBER)) {
            row->hl[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
         }
      }

      if (prev_sep) {
         int j;
         for (j = 0; keywords[j]; j++) {
            int klen = strlen(keywords[j]);
            int kw2 = keywords[j][klen - 1] == '|';
            if (kw2) klen--;

            if (!strncmp(&row->render[i], keywords[j], klen) &&
                  is_separator(row->render[i + klen])) {
               memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
               i += klen;
               break;
            }
         }

         if (keywords[j] != NULL) {
            prev_sep = 0;
            continue;
         }
      }

      prev_sep = is_separator(c);
      i++;
   }

   int changed = (row->hl_open_comment != in_comment);
   row->hl_open_comment = in_comment;
   if (changed && row->idx + 1 < E.numrows) {
      editor_update_syntax(&E.row[row->idx + 1]);
   }
}

void editor_select_syntax_highlight()
{
   E.syntax = NULL;
   if (E.filename == NULL) return;

   char * ext = strrchr(E.filename, '.');

   for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
      struct editor_syntax * s = &HLDB[j];
      unsigned int i = 0;

      while (s->filematch[i]) {
         int is_ext = (s->filematch[i][0] == '.');
         if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
               (!is_ext && strstr(E.filename, s->filematch[i]))) {
            E.syntax = s;

            int filerow;
            for (filerow = 0; filerow < E.numrows; filerow++) {
               editor_update_syntax(&E.row[filerow]);
            }

            return;
         }
         i++;
      }
   }
}

void editor_update_row(erow * row)
{
   int tabs = 0;
   int j;

   for (j = 0; j < row->size; j++)
      if (row->chars[j] == '\t') tabs++;

   free(row->render);
   row->render = malloc(row->size + tabs * (YAR_TAB_STOP - 1) + 1);

   int idx = 0;
   for (j = 0; j < row->size; j++) {
      if (row->chars[j] == '\t') {
         row->render[idx++] = ' ';
         while (idx % YAR_TAB_STOP != 0) row->render[idx++] = ' ';
      } else {
         row->render[idx++] = row->chars[j];
      }
   }
   row->render[idx] = '\0';
   row->rsize = idx;

   editor_update_syntax(row);
}

void editor_insert_row(int at, char * s, size_t len)
{
   if (at < 0 || at > E.numrows) return;

   E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
   memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
   for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

   E.row[at].idx = at;

   E.row[at].size = len;
   E.row[at].chars = malloc(len + 1);
   memcpy(E.row[at].chars, s, len);
   E.row[at].chars[len] = '\0';

   E.row[at].rsize = 0;
   E.row[at].render = NULL;
   E.row[at].hl = NULL;
   E.row[at].hl_open_comment = 0;
   editor_update_row(&E.row[at]);

   E.numrows++;
   E.dirty++;
}

void editor_free_row(erow * row)
{
   free(row->render);
   free(row->chars);
   free(row->hl);
}

void editor_del_row(int at)
{
   if (at < 0 || at >= E.numrows) return;
   editor_free_row(&E.row[at]);
   memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
   for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
   E.numrows--;
   E.dirty++;
}

void editor_row_insert_char(erow * row, int at, int c)
{
   if (at < 0 || at > row->size) at = row->size;
   row->chars = realloc(row->chars, row->size + 2);
   memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
   row->size++;
   row->chars[at] = c;
   editor_update_row(row);
   E.dirty++;
}

void editor_row_append_string(erow * row, char * s, size_t len)
{
   row->chars = realloc(row->chars, row->size + len + 1);
   memcpy(&row->chars[row->size], s, len);
   row->size += len;
   row->chars[row->size] = '\0';
   editor_update_row(row);
   E.dirty++;
}

void editor_row_del_char(erow * row, int at) {
   if (at < 0 || at >= row->size) return;
   memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
   row->size--;
   editor_update_row(row);
   E.dirty++;
}

void editor_insert_char(int c)
{
   if (E.cy == E.numrows) {
      editor_insert_row(E.numrows, "", 0);
   }
   editor_row_insert_char(&E.row[E.cy], E.cx, c);
   E.cx++;
}

void editor_insert_newline()
{
   if (E.cx == 0) {
      editor_insert_row(E.cy, "", 0);
   } else {
      erow * row = &E.row[E.cy];
      editor_insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
      row = &E.row[E.cy];
      row->size = E.cx;
      row->chars[row->size] = '\0';
      editor_update_row(row);
   }
   E.cy++;
   E.cx = 0;
}

void editor_del_char()
{
   if (E.cy == E.numrows) return;
   if (E.cx == 0 && E.cy == 0) return;

   erow * row = &E.row[E.cy];
   if (E.cx > 0) {
      editor_row_del_char(row, E.cx - 1);
      E.cx--;
   } else {
      E.cx = E.row[E.cy - 1].size;
      editor_row_append_string(&E.row[E.cy - 1], row->chars, row->size);
      editor_del_row(E.cy);
      E.cy--;
   }
}

char * editor_rows_to_string(int * buflen)
{
   int totlen = 0;
   int j;
   for (j = 0; j < E.numrows; j++)
      totlen += E.row[j].size + 1;
   *buflen = totlen;

   char * buf = malloc(totlen);
   char * p = buf;
   for (j = 0; j < E.numrows; j++) {
      memcpy(p, E.row[j].chars, E.row[j].size);
      p += E.row[j].size;
      *p = '\n';
      p++;
   }

   return buf;
}

void editor_open(char * filename)
{
   free(E.filename);
   E.filename = strdup(filename);

   editor_select_syntax_highlight();

   FILE *fp = fopen(filename, "r");
   if (!fp) die ("fopen");

   char *line = NULL;
   size_t linecap = 0;
   ssize_t linelen;
   while ((linelen = getline(&line, &linecap, fp)) != -1) {
      while (linelen > 0 && (line[linelen - 1] == '\n' ||
                            line[linelen - 1] == '\r'))
         linelen--;
      editor_insert_row(E.numrows, line, linelen);
   }
   free(line);
   fclose(fp);
   E.dirty = 0;
}

char * editor_prompt(char * prompt, void (*callback)(char *, int))
{
   size_t bufsize = 128;
   char * buf = malloc(bufsize);

   size_t buflen = 0;
   buf[0] = '\0';

   for(;;) {
      editor_set_status_message(prompt, buf);
      editor_refresh_screen();

      int c = editor_read_key();
      if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
         if (buflen != 0) buf[--buflen] = '\0';
      } else if (c == '\x1b') {
         editor_set_status_message("");
         free(buf);
         return NULL;
      } else if (c == '\r') {
         if (buflen != 0) {
            editor_set_status_message("");
            if (callback) callback(buf, c);
            return buf;
         }
      } else if (!iscntrl(c) && c < 128) {
         if (buflen == bufsize - 1) {
            bufsize *= 2;
            buf = realloc(buf, bufsize);
         }
         buf[buflen++] = c;
         buf[buflen] = '\0';
      }

      if (callback) callback(buf, c);
   }
}

void editor_save()
{
   if (E.filename == NULL) {
      E.filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
      if (E.filename == NULL) {
         editor_set_status_message("Save aborted");
         return;
      }
      editor_select_syntax_highlight();
   }

   int len;
   char * buf = editor_rows_to_string(&len);

   int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
   if (fd != -1) {
      if (ftruncate(fd, len) != -1) {
         if (write(fd, buf, len) == len) {
            close(fd);
            free(buf);
            E.dirty = 0;
            editor_set_status_message("%d bytes written to disk", len);
            return;
         }
      }
   close(fd);
   }

   free(buf);
   editor_set_status_message("Can't save! I/O error: %s", strerror(errno));
}

void editor_find_callback(char * query, int key)
{
   static int last_match = -1;
   static int direction = 1;

   static int saved_hl_line;
   static char * saved_hl = NULL;

   if (saved_hl) {
      memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
      free(saved_hl);
      saved_hl = NULL;
   }

   if (key == '\r' || key == '\x1b') {
      last_match = -1;
      direction = 1;
      return;
   } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
      direction = 1;
   } else if (key == ARROW_LEFT || key == ARROW_UP) {
      direction = -1;
   } else {
      last_match = -1;
      direction = 1;
   }
   
   if (last_match == -1) direction = 1;
   int current = last_match;

   int i;
   for (i = 0; i < E.numrows; i++) {
      current += direction;
      if (current == -1) current = E.numrows - 1;
      else if (current == E.numrows) current = 0;

      erow * row = &E.row[current];
      char * match = strstr(row->render, query);
      if (match) {
         last_match = current;
         E.cy = current;
         E.cx = editor_row_rx_to_cx(row, match - row->render);
         E.rowoff = E.numrows;

         saved_hl_line = current;
         saved_hl = malloc(row->rsize);
         memcpy(saved_hl, row->hl, row->rsize);
         memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
         break;
      }
   }
}

void editor_find()
{
   int saved_cx = E.cx;
   int saved_cy = E.cy;
   int saved_coloff = E.coloff;
   int saved_rowoff = E.rowoff;

   char * query = editor_prompt("Search: %s (ESC/Arrows/Enter)", editor_find_callback);

   if (query) free(query);
   else {
      E.cx = saved_cx;
      E.cy = saved_cy;
      E.coloff = saved_coloff;
      E.rowoff = saved_rowoff;
   }
}

void editor_move_cursor(int key) {
   erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

   switch (key) {
      case ARROW_LEFT:
         if (E.cx != 0) E.cx--;
         else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
         }
         break;
      case ARROW_RIGHT:
         if (row && E.cx < row->size) E.cx++;
         else if (row && E.cx == row->size) {
            E.cy++;
            E.cx = 0;
         }
         break;
      case ARROW_UP:
         if (E.cy != 0) E.cy--;
         break;
      case ARROW_DOWN:
         if (E.cy != E.numrows) E.cy++;
         break;
   }

   row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
   int rowlen = row ? row->size : 0;
   if (E.cx > rowlen) {
      E.cx = rowlen;
   }
}

void editor_process_keypress() {
   static int quit_times = YAR_QUIT_TIMES;
   int c = editor_read_key();

   switch(c) {
      case '\r':
         editor_insert_newline();
         break;

      case CTRL_KEY('q'):
         if (E.dirty && quit_times > 0) {
            editor_set_status_message("Warning: File has unsaved changes. Press Ctrl -Q again to quit.", quit_times);
            quit_times--;
            return;
         }
         write(STDOUT_FILENO, "\x1b[2J", 4);
         write(STDOUT_FILENO, "\x1b[H", 3);
         exit(0);
         break;

      case CTRL_KEY('s'):
         editor_save();
         break;

      case HOME_KEY:
         E.cx = 0;
         break;
      case END_KEY:
         if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
         break;

      case CTRL_KEY('f'):
         editor_find();
         break;

      case BACKSPACE:
      case CTRL_KEY('h'):
      case DEL_KEY:
         if (c == DEL_KEY) editor_move_cursor(ARROW_RIGHT);
         editor_del_char();
         break;

      case PAGE_UP:
      case PAGE_DOWN:
         {
            if (c == PAGE_UP) {
               E.cy = E.rowoff;
            } else if (c == PAGE_DOWN) {
               E.cy = E.rowoff + E.screenrows - 1;
               if (E.cy > E.numrows) E.cy = E.numrows;
            }

            int times = E.screenrows;
            while (times--)
               editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
         }
         break;

      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
         editor_move_cursor(c);
         break;

      case CTRL_KEY('l'):
      case '\x1b':
         break;

      default:
         editor_insert_char(c);
         break;
   }

   quit_times = YAR_QUIT_TIMES; 
}

void init_editor() {
   E.cx = 0;
   E.cy = 0;
   E.rx = 0;
   E.rowoff = 0;
   E.coloff = 0;
   E.numrows = 0;
   E.row = NULL;
   E.dirty = 0;
   E.filename = NULL;
   E.statusmsg[0] = '\0';
   E.statusmsg_time = 0;
   E.syntax = NULL;

   if (get_window_size(&E.screenrows, &E.screencols) == -1) die("get_window_size");
   E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
   enable_raw_mode();
   init_editor();

   if (argc >= 2) {
      editor_open(argv[1]);
   }

   editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl - F = find");

   for (;;) {
      editor_refresh_screen();
      editor_process_keypress();
   }

   return 0;
}
