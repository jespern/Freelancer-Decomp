// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// Written for the macOS port (https://github.com/jespern/freelancer-mac): how a full-screen
// session is presented there.
// Author: jespern.

// Port-only full screen (FREELANCER_BORDERLESS_FULLSCREEN): instead of an exclusive display
// mode, a borderless window covering the desktop with a windowed swap chain -- no mode switch,
// so switching applications, other displays and screen sharing keep working. The surface is the
// desktop's size (its pixels on a high-density display), or the chosen size when that is
// smaller with the desktop's aspect ratio (scaled up on present). See docs/re/deviations.md
// (BORDERLESS-FULLSCREEN). FLWIN_EXCLUSIVE_FULLSCREEN=1 restores the exclusive mode.
#pragma once

#include <cstdlib>

namespace PortDisplay
{
    inline bool BorderlessFullscreen()
    {
#ifdef FREELANCER_BORDERLESS_FULLSCREEN
        static const bool bBorderless = [] {
            const char* const Value = std::getenv("FLWIN_EXCLUSIVE_FULLSCREEN");
            return Value == nullptr || Value[0] == '\0' || (Value[0] == '0' && Value[1] == '\0');
        }();
        return bBorderless;
#else
        return false;
#endif
    }

    // Whether a Width x Height surface may stay as chosen under a borderless full screen on a
    // DesktopWidth x DesktopHeight desktop: not larger, and the same aspect ratio within 1%.
    inline bool KeepsChosenSize(int Width, int Height, int DesktopWidth, int DesktopHeight)
    {
        if (Width <= 0 || Height <= 0 || DesktopWidth <= 0 || DesktopHeight <= 0)
            return false;
        if (Width > DesktopWidth || Height > DesktopHeight)
            return false;
        long long const Cross = static_cast<long long>(Width) * DesktopHeight -
                                static_cast<long long>(DesktopWidth) * Height;
        long long const Scale = static_cast<long long>(DesktopWidth) * Height;
        return (Cross < 0 ? -Cross : Cross) * 100 <= Scale;
    }
}
