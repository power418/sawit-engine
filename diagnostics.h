#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

void diagnostics_log(const char* message);
void diagnostics_logf(const char* format, ...);
int diagnostics_get_recent_message_count(void);
const char* diagnostics_get_recent_message(int index);

#endif
