# decima-consensus.
decima-consensus is a simple C library implementing a blockchain-based consensus algorithm for a toy cryptocurrency I'm writing to learn more about them.

## issues.
it isn't really that parallel; adding blocks is only in the sense that multiple threads can add blocks, but processing actual block updates is highly serialised.\
every once in a while a thread becomes the `blockmaster`; that is, they're the only ones allowed to modify consensus state. the largest issue is that _only one thread is the blockmaster at a time_.
this isn't an issue when block updates are slow enough (like they are in bitcoin) & short chain reorgs infrequent, but I don't really like it & I did it like this out of laziness.

much work has to be done.
## building.
include `src/decima-consensus.h` for the header files, type `#define DECIMA_CONSENSUS_IMPLEMENTATION` before including to have the implementation too, or link against `src/decima-consensus.c`.
