#ifndef __INTERPRET_HH__
#define __INTERPRET_HH__

#include <cstdint>
struct state_t;

void interpretAndBuildCFG(state_t *s);
void interpret(state_t *s);
void interpretAndBuildCFGEL(state_t *s);
void interpretEL(state_t *s);
void mkMonitorVectors(state_t *s);

#endif
