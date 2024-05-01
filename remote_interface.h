#include "lib/tinyjson/tinyjson.h"

#define REMOTE_PATH_SEP "/"
#define REQUEST_BUF_INIT_SIZE 128

// add a file to the schema
void start_tracking(char* remote, char* local, jvalue* schema);

// remove a file from the schema
void stop_tracking(char* local, jvalue* schema);

// download all changed files in the schema
void fetch_all(jvalue* schema);
