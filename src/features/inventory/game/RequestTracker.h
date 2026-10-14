#pragma once
#include "features/inventory/game/OwnedResponseBarrier.h"
class ContainerManagerController;
namespace lamium::inventory::game {
void installRequestTracker();
void removeRequestTracker();
// Forget only Lamium's observation state. Vanilla owns outstanding requests.
void cancelTransfer(TransferToken token);
std::optional<TransferToken> beginTransfer(ContainerManagerController& controller);
// Track the requests vanilla added so far; a batch captures after each move,
// in case vanilla sends part of it before the batch ends.
void captureTransfer(TransferToken token);
void endTransfer(TransferToken token);
ResponseBarrier::Result transferResult(TransferToken token);
}
