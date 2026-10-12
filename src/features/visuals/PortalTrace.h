#pragma once
// L-148/L-149 research: what the Nether portal does to the local player on
// the client (the purple overlay's effect time, closing screens). Only active
// with `xmake f --portal_trace=y`.
namespace lamium::visuals::portalTrace {
void start();
void stop();
}
