This is a simple GTK directory comparison tool.

It can display two (or more) directory trees side by side, and show which files
are different between them.

Currently, it supports reading from regular directories and from directories in
zip files.

It doesn't show the differences between files by itself (other than whether
files are different at all). Instead, it can run an external diff tool to
display the differences in a file.

Currently, there's no way to set the external diff tool in the GUI, so you have
to edit the config file instead.
