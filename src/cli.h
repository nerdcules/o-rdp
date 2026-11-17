#pragma once

#include <QStringList>

// Commands that need no display, for scripts and agents. Returns true when
// the arguments named one.
bool isCliCommand(const QStringList &arguments);
int runCli(const QStringList &arguments);
