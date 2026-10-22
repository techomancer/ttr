# tc: run ttrc, To The Roots Commander, and stay in the directory it
# showed when you quit. For csh and tcsh; source it from ~/.cshrc:
#
#   source /usr/share/ttrc/tc.csh
alias tc 'set _ttrc_pwd="/tmp/ttrc.pwd.$$"; ttrc -P "$_ttrc_pwd" \!*; if ( -r "$_ttrc_pwd" ) cd "`cat $_ttrc_pwd`"; rm -f "$_ttrc_pwd"; unset _ttrc_pwd'
