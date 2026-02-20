# sheets

A minimal terminal spreadsheet. Reads and writes CSV files, supports
formulas with cell references, and uses vim-style keybindings.

![sheets screenshot](https://github.com/user-attachments/assets/ae21b6db-3bd6-401c-9712-5fc8e7d6b395)

## Building

sheets requires ncurses and a C99 compiler.

```
make
sudo make install
```

To customize, edit `config.h` (copied from `config.def.h` on first build).

## Usage

```
sheets [file.csv]
```

### Navigation

| Key              | Action                     |
|------------------|----------------------------|
| h j k l / arrows | Move between cells         |
| g                | Go to cell A1              |
| G                | Go to last used row        |
| 0 / Home         | Go to first column         |
| $ / End          | Go to last used column     |
| Tab / Shift-Tab  | Move right / left          |
| PgUp / PgDn      | Scroll page up / down      |

### Editing

| Key         | Action                        |
|-------------|-------------------------------|
| Enter / e   | Edit cell (keep content)      |
| i           | Edit cell (clear content)     |
| =           | Start entering a formula      |
| x / Delete  | Delete cell                   |
| y           | Yank (copy) cell              |
| p           | Paste yanked cell             |

### Commands

| Command     | Action                        |
|-------------|-------------------------------|
| Ctrl-S      | Save                          |
| :w [file]   | Save to file                  |
| :q          | Quit (warns if unsaved)       |
| :q!         | Quit without saving           |
| :wq         | Save and quit                 |
| :\<cell\>   | Go to cell (e.g. :B5)        |

## Formulas

Cells starting with `=` are evaluated as formulas.

```
=A1+B1
=A1*2+1
=SUM(A1:A10)
=AVG(B1:B20)
=MIN(C1:C5)
=MAX(C1:C5)
```

## Configuration

Edit `config.h` to change defaults:

- **colwidth** -- default column width
- **maxcols** -- number of columns (A-Z)
- **maxrows** -- number of rows
- **separator** -- CSV delimiter

## License

GPLv3. See [COPYING](COPYING) for details.
