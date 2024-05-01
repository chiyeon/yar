# yar
```
~
~
~
~
~
~                 Yar Text Editor -- Ver. 0.3
~                     <i> for editing mode
~                    <ESC> for reading mode
~                  <CTRL+/> for command mode
~
~                       <CTRL+s> to Save
~                       <CTRL+f> to Find
~                       <CTRL+q> to Quit
~
~
~
 READING │ [No Name] - 0 lines                     no ft | 1/0
```

Yar is a simple terminal text editor written in C based on the [kilo tutorial](https://viewsourcecode.org/snaptoken/kilo/).

### Features
 - Distinct "vim-like" modes
 - File opening/editing/saving
 - Syntax Highlighting
 - Text Search
 - Configurable Settings
    - Line Numbers
    - Expand Tabs
    - Tab size

### Controls
These commands work regardless of the mode.
- `Ctrl + Q`: Quit. Press 2x to force quit an unsaved file.
- `Ctrl + S`: Save file
- `Ctrl + F`: Find word/phrase. Press `Esc` to cancel, and use `arrow keys` to navigate
- `Ctrl + /`: Enter command mode

These commands only work in READING mode.
- `:`: Enter command mode

### Commands
After pressing `Ctrl + /`, the user is greeted with the `: ` bar at the bottom to enter any of the following:

 - `help`: Prints all commands
 - `tabstop`: Accepts 1 numerical argument. Sets size of tabs in spaces.
 - `expandtab`: Accepts true/false. If true, tabs are written as spaces instead of tab characters.
 - `linenumbers`: Accepts true/false. If true, line numbers are rendered on the left margin.
 - `quit`: Attempts to close program. Warns of unsaved changes. Can be shortened to `q`. Add an `!` at the end to force quit
 - `write`: Saves file to disk. Can be shortened to `w`. Same as `save` and `s`
 - `writequit`: Saves file to disk & closes program. Can be shortened to `wq`
