# Richterm

Richterm is a textual interface with support for links, different fonts,
and customizable menu.

# Interface

Processes can interact with richterm either by writing and reading to
`/dev/cons`, or by working with its file system, mounted over at
`/mnt/richterm`.

`/cons` is provided for backwards-compatibilty, text written here
will appear on screen as one would expect.

`/text` expects specially formatted text to present on screen.

`/ctl` will return events, such as menu hits or link follows.

Content of `/menu` file will appear in the right-click menu.

# Format

`/text` is parsed on line-by-line basis, with first character
being treated as command and rest as argument.

## Commands

- '.' - print text
- 'n' - print new line
- 't' - print tab
- 's' - print space
- 'l' - set link
- 'f' - set font

For 'n', 't', and 's' argument is ignored and should be empty.
For 'l' and 'f' empty argument will unset the parameter.

# Usage

Left mouse button is used for selecting text.

Middle-click menu provides operations on selected text.

Right-click menu combines link operations (Follow, Snarf, Plumb)
and user menu managed by `/menu` file.

On launch `richterm` will start `/bin/richrc` script by default.
It handles richterm's link and menu events and tries to open links
in appropriate programs or sends them to plumber.

For now, following programs are provided:

- `Dir` prints direcrory listing, supplied with appropriate link for every line.
- `Markdown` prints [markdown](https://daringfireball.net/projects/markdown/)-formatted files.
- `Gopher` prints gopher menus and text files from supplied gopher URL.
- `Gemini` does the same for gemini URLs.

In addition, `richrc` manages primitive link history via 'Back' option in
user menu.

# Installation

The usual `mk install` invocation will install richterm binary
into `/$objtype/bin directory` and extra binaries and scripts into
`/sys/lib/richterm/bin/...` directories.

`/sys/lib/richterm/bin/...` will be bound over `/bin` by richterm
on launch automatically.
