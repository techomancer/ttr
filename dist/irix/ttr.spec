product ttr
    id "To The Roots: window manager, desktop, terminal, commander and code editor"
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
        subsys commander default
            id "ttrc To The Roots Commander (tc)"
            replaces ttr.sw.commander 0 VERSION
            exp ttr.sw.commander
        endsubsys
        subsys code default
            id "ttrcode Code Editor"
            replaces ttr.sw.code 0 VERSION
            exp ttr.sw.code
        endsubsys
    endimage
endproduct
