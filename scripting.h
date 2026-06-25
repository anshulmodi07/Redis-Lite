#pragma once

#include "commands.h"

void initScripting();
void shutdownScripting();
void registerScriptingCommands(CommandTable& table);

bool scriptingRunning();
