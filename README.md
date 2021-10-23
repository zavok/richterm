# Richterm

Richterm is a textual interface with support for different fonts, links,
images and customizable menu.

# Interface

Processes can interact with richterm either by writing and reading to
/dev/cons, or by working with its file system, mounted over at
/mnt/richterm.

Reading from /new file will return id of freshly created element.

/ctl will return events, such as menu hits or link follows, and will
accept commands to 'clear' the screen or 'remove $id ...'.

Lines written to menu file will appear in the right-click menu.

Directory /$id will contain files representing element's properties
(text, link, image, font).

# Usage

Richterm runs rc shell by default. You can type text on keyboard
mostly like usual and it will be send to stdin of running process.

Left mouse button is used to select text.

Middle-click opens menu for text operations such as snarf, paste and plumb.

Right-click menu combines link operations (Follow, Snarf, Plumb)
and user menu managed by /mnt/richterm/menu file.

# Extra

Handler is a rc script that handles richterm's link and menu events.
It tries to open links in appropriate programs or sends them to
plumber.

For now, two programs are provided: Dir and Markdown:

Dir prints direcrory listing, supplied with appropriate link for
every line.

Markown reads text files and applies markdown formatting to them

In addition, Handler manages primitive link history via user menu.

# Installation

Proper installation and operation of richterm is work in progress.

For now you can run it in the following fashion:

	mk
	cd extra; mk install
	cd ..
	6.out bin/rc/Handler

