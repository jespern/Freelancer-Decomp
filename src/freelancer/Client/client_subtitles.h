// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file adds subtitles to the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

#pragma once

// Port-only: subtitles for the voice lines the game plays. Retail has none; this follows the
// community FLSubs mod (a d3d8.dll wrapper that matched sound buffers by checksum), but keys
// on the archetype each playing source was started with instead.
//
// The table is bin/subtitles.txt, compiled from FLSubs' subtitles.xml by the port's
// tools/subtitles.py. Without it nothing is drawn. FLWIN_SUBTITLES=0 turns the overlay off;
// FLWIN_SUBTITLES_LOG=1 prints every sound that starts, with its key and caption.

// Called from EndSceneHalf, after the material batcher's flush and before EndScene: polls the
// playing sources and draws their captions over the finished frame.
void DrawClientSubtitles();

// Releases the cached font strings; main.cpp calls it before FontShutdown.
void ShutdownClientSubtitles();
