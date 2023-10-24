#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define YAR_VERSION "0.1"

typedef struct erow {
   int size;
   char * chars;
} erow;

struct EditorConfig {
   int cx, cy;
   int screenrows;
   int screencols;
   int numrows;
   erow row;
   struct termios orig_termios;
};

struct abuf {
   char *b;
   int len;
};

enum editor_key {
   ARROW_LEFT = 'j',
   ARROW_RIGHT = 'l',
   ARROW_UP = 'i',
   ARROW_DOWN = 'k',
   PAGE_UP = 1000,
   PAGE_DOWN,
   HOME_KEY,
   END_KEY,
   DEL_KEY
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

struct EditorConfig E;

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

void editor_draw_rows(struct abuf * ab) {
   int y;
   for (y = -1; y < E.screenrows; ++y) {
      if (y >= E.numrows) {
         if (y == E.screenrows / 3) {
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
         int len = E.row.size;
         if (len > E.screencols) len = E.screencols;
         ab_append(ab, E.row.chars, len);
      }

      ab_append(ab, "\x1b[K", 3);
      if (y < E.screenrows - 1) {
         ab_append(ab, "\r\n", 2);
      }
   }
}

void editor_refresh_screen() {
   struct abuf ab = ABUF_INIT;

   ab_append(&ab, "\x1b[?25l", 6);
   ab_append(&ab, "\x1b[H", 3);

   editor_draw_rows(&ab);

   char buf[32];
   snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
   ab_append(&ab, buf, strlen(buf));

   ab_append(&ab, "\x1b[?25h", 6);

   write(STDOUT_FILENO, ab.b, ab.len);
   ab_free(&ab);
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

void editor_open(char * filename)
{
   FILE *fp = fopen(filename, "r");
   if (!fp) die ("fopen");

   char *line = NULL;
   size_t linecap = 0;
   ssize_t linelen;
   linelen = getline(&line, &linecap, fp);
   if (linelen != -1) {
      while (linelen >0 && (line[linelen - 1] == '\n' ||
                            line[linelen - 1] == '\r'))
         linelen--;
      E.row.size = linelen;
      E.row.chars = malloc(linelen + 1);
      memcpy(E.row.chars, line, linelen);
      E.row.chars[linelen] = '\0';
      E.numrows = 1;
   }
   free(line);
   fclose(fp);
}

void editor_move_cursor(int key) {
   switch (key) {
      case ARROW_LEFT:
         if (E.cx != 0) E.cx--;
         break;
      case ARROW_RIGHT:
         if (E.cx != E.screencols - 1) E.cx++;
         break;
      case ARROW_UP:
         if (E.cy != 0) E.cy--;
         break;
      case ARROW_DOWN:
         if (E.cy != E.screenrows - 1) E.cy++;
         break;
   }
}

void editor_process_keypress() {
   int c = editor_read_key();

   switch(c) {
      case CTRL_KEY('q'):
         write(STDOUT_FILENO, "\x1b[2J", 4);
         write(STDOUT_FILENO, "\x1b[H", 3);
         exit(0);
         break;

      case HOME_KEY:
         E.cx = 0;
         break;
      case END_KEY:
         E.cx = E.screencols - 1;
         break;

      case PAGE_UP:
      case PAGE_DOWN:
         {
            int times = E.screenrows;
            while (times--) editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
         }
         break;

      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
         editor_move_cursor(c);
         break;
   }

   editor_move_cursor(c);
}

void init_editor() {
   E.cx = 0;
   E.cy = 0;
   E.numrows = 0;

   if (get_window_size(&E.screenrows, &E.screencols) == -1) die("get_window_size");
}

int main(int argc, char *argv[]) {
   enable_raw_mode();
   init_editor();

   if (argc >= 2) {
      editor_open(argv[1]);
   }

   for (;;) {
      editor_refresh_screen();
      editor_process_keypress();
   }

   return 0;
}
