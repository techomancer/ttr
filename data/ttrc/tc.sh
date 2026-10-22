# tc: run ttrc, To The Roots Commander, and stay in the directory it
# showed when you quit. For sh, ksh, bash and zsh; source it from your
# shell's startup file:
#
#   . /usr/share/ttrc/tc.sh
#
# (Linux has an unrelated /sbin/tc, iproute2's traffic control; this
# function hides it in interactive shells only. Call it by its path.)
tc() {
    _ttrc_pwd="${TMPDIR:-/tmp}/ttrc.pwd.$$"
    ttrc -P "$_ttrc_pwd" "$@"
    if [ -r "$_ttrc_pwd" ]; then
        _ttrc_dir=`cat "$_ttrc_pwd"`
        rm -f "$_ttrc_pwd"
        if [ -n "$_ttrc_dir" ] && [ -d "$_ttrc_dir" ] && [ "$_ttrc_dir" != "`pwd`" ]; then
            cd "$_ttrc_dir"
        fi
    fi
    unset _ttrc_pwd _ttrc_dir
}
