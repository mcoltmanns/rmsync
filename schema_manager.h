// functions for managing the backup schema
#include "lib/tinyjson/tinyjson.h"
#include <stdio.h>

#ifndef SCHEMA_MANAGER_HEADER
#define SCHEMA_MANAGER_HEADER

#define SCHEMA_SUCCESS 1
#define SCHEMA_FAILURE 0

// load a schema string into a jvalue - mostly a format checker
// returns SCHEMA_SUCCESS if the data was a valid schema, SCHEMA_FAILURE otherwise
// (de)allocation of schema and data is left to the caller
int load_schema(FILE* f, jvalue* schema);

// add an entry object to the beginning of a schema
int schema_add_entry(const char* localpath, const char* remoteid, const char* format, const double lastbackup, jvalue* schema);

// remove an entry object from a schema
int schema_remove_entry(const char* localpath, jvalue* schema);

// set the last backup time of an entry to the current time (seconds since the unix epoch)
int schema_update_entry_backup(const char* localpath, jvalue* schema);

// property accessor functions
// remote id
char* schema_peek_remote(const char* localpath, jvalue* schema);
// format
char* schema_peek_format(const char* localpath, jvalue* schema);
// last backup
int schema_peek_lastbackup(const char* localpath, jvalue* schema);

#endif