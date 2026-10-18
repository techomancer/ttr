# ttr themes

Color themes of To The Roots itself, in the format of the IRIX schemes next
door (see `../irix/README.md` for the roles). They differ in two ways:

- They have no gamma prefix: one value per role.
- A `name:` line gives the name the Settings dialog shows.

Roles a theme leaves out are derived: specific backgrounds (buttons, text
fields, lists, scroll bars) follow `BasicBackground`, and text colors follow
Motif's rule for their background. Your own themes go in
`~/.config/ttr/themes/<Name>.ad`.

The Classic theme (grey widgets, blue active frames) is built into ttr and
has no file.
