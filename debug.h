#pragma once

#ifdef DEBUG_LOG
#define DEBUG log_write
#else
#define DEBUG
#endif
