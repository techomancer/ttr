/*
 * The shell -> WM half of WmLink (docs/wm-design.md 3.5). In X11 mode the
 * WM implements this in-process. Toplevel ids are opaque to the shell.
 */
#ifndef TTR_CORE_WMLINK_H
#define TTR_CORE_WMLINK_H

namespace ttr {

typedef unsigned long ToplevelId;

enum WmAction {
    ACT_RESTORE,
    ACT_MOVE,
    ACT_SIZE,
    ACT_MINIMIZE,
    ACT_MAXIMIZE,
    ACT_LOWER,
    ACT_CLOSE,
    ACT_COUNT
};

/* Bit (1 << WmAction) for each allowed window action. */
typedef unsigned int ActionMask;

inline ActionMask all_actions()
{
    return (1u << ACT_COUNT) - 1;
}

inline bool action_allowed(ActionMask m, WmAction a)
{
    return (m & (1u << a)) != 0;
}

class WmLink {
public:
    virtual ~WmLink() {}
    virtual void request(ToplevelId id, WmAction action) = 0;
};

}

#endif
