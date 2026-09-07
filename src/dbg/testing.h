#pragma once

#include "_global.h"
#include "command.h"

void TestInitialize(bool enabled);
void TestShutdown();
bool TestIsEnabled();

bool TestRunStartupScript(const char* filename);
bool TestAssertCommand(bool condition, const char* expression, const char* message);
bool TestAssertPlugin(bool condition, const char* message);

bool cbInstrTestAssert(int argc, char* argv[]);
bool cbInstrTestFinalize(int argc, char* argv[]);
bool cbInstrTestScript(int argc, char* argv[]);
bool cbInstrSettingSet(int argc, char* argv[]);
