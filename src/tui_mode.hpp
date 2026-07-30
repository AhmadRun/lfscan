#pragma once

#include "history_db.hpp"
#include "scanner.hpp"

// Launch the TUI; returns 0 on success.
int run_tui(HistoryDB& history, const std::string& db_path);
