/*
 * =============================================================================
 * FILE : app_cli.h
 * WHAT : Line-based serial command interface.
 *        CONFIG | START | STOP | CLEAR | BAUD | STATUS | RESET
 * =============================================================================
 */

#ifndef APP_CLI_H
#define APP_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read pending console bytes (non-blocking) and execute complete lines */
void App_Cli_Poll(void);

#ifdef __cplusplus
}
#endif
#endif /* APP_CLI_H */
