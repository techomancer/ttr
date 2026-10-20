product ttr
    id "To The Roots: window manager, desktop and terminal"
    image sw
        id "ttr Software"
        version VERSION
        order 9999
        subsys wm default
            id "ttr Window Manager and Desktop"
            replaces ttr.sw.wm 0 VERSION
            exp ttr.sw.wm
        endsubsys
        subsys term default
            id "ttrt Terminal Emulator"
            replaces ttr.sw.term 0 VERSION
            exp ttr.sw.term
        endsubsys
    endimage
endproduct
