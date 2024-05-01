// functions for managing the backup schema
#include "schema_manager.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// load a schema from a file into a jvalue - mostly a format checker
// returns SCHEMA_SUCCESS if the data was a valid schema, SCHEMA_FAILURE otherwise
// (de)allocation of schema and data, and closing the filestream is left to the caller
int load_schema(FILE* f, jvalue* schema)
{
    // read in the file
    char* data = 0;
    long length;
    if(f == NULL) return SCHEMA_FAILURE;
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    data = calloc(length, 1);
    fseek(f, 0, SEEK_SET);
    if(data)
        fread(data, 1, length, f);
    else
    {
        printf("Out of memory!\n");
        return SCHEMA_FAILURE;
    } // don't close the file - caller probably still wants to do something
    const char* cursor = (const char *)data;
    if(json_parse_value(&cursor, schema) != JSON_FAILURE)
    {
        if(schema->type != JSON_OBJECT) 
        {
            return SCHEMA_FAILURE; // make sure we actually have an object
        }
        jmember* current = schema->members;
        while(current != NULL)
        {
            if(current->element->type != JSON_OBJECT || // submembers should be objects
                // submembers must contain these keys, but we don't really care about anything else
                json_search_by_key("ID", current->element) == NULL ||
                json_search_by_key("ModifiedClient", current->element) == NULL ||
                json_search_by_key("Parent", current->element) == NULL ||
                json_search_by_key("Type", current->element) == NULL ||
                json_search_by_key("VissibleName", current->element) == NULL)
            {
                free(data);
                return SCHEMA_FAILURE;
            }
            current = current->next;
        }
        free(data);
        return SCHEMA_SUCCESS;
    }
    free(data);
    return SCHEMA_FAILURE;
}

// add an entry object to the beginning of a schema
int schema_add_entry(const char* localpath, const char* remoteid, const char* format, const double lastbackup, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    jmember* new = malloc(sizeof(jmember));
    if(new == NULL) return SCHEMA_FAILURE;
    new->string = malloc(strlen(localpath) + 1);
    if(new->string == NULL) return SCHEMA_FAILURE;
    new->element = malloc(sizeof(jvalue));
    if(new->element == NULL) return SCHEMA_FAILURE;
    strcpy(new->string, localpath);
    new->next = schema->members;
    schema->members = new;
    return SCHEMA_SUCCESS;
}

// remove an entry object from a schema
//TODO: test for leaks
int schema_remove_entry(const char* localpath, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    // check head
    if(!strcmp(schema->members->string, localpath))
    {
        jmember* next = schema->members->next;
        json_free_value(schema->members->element); // free the element
        free(schema->members->string); // free the element's string
        free(schema->members); // free the member struct
        schema->members = next;
        return SCHEMA_SUCCESS;
    }
    // check rest
    jmember* prev = schema->members;
    jmember* current = prev->next;
    while(current != NULL && strcmp(current->string, localpath))
    {
        prev = current;
        current = current->next;
    }
    if(current != NULL) // found the element to delete
    {
        prev->next = current->next; // relink the list
        json_free_value(current->element); // free the element
        free(current->string); // free the element's string
        free(current); // free the member struct
    }
    return SCHEMA_SUCCESS;
}

// set the last backup time of an entry to the current time (seconds since the unix epoch)
int schema_update_entry_backup(const char* localpath, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    jvalue* current = json_search_by_key(localpath, schema); // find the element to update
    if(current == NULL) return SCHEMA_FAILURE; // didnt' exist
    json_search_by_key("lastbackup", current)->number = time(NULL);
    return SCHEMA_SUCCESS;
}

// property accessor functions
// remote id
char* schema_peek_remote(const char* localpath, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    jvalue* current = json_search_by_key(localpath, schema);
    if(current == NULL) return SCHEMA_FAILURE;
    return json_search_by_key("remoteid", current)->string;
}

// format
char* schema_peek_format(const char* localpath, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    jvalue* current = json_search_by_key(localpath, schema);
    if(current == NULL) return SCHEMA_FAILURE;
    return json_search_by_key("format", current)->string;
}

// last backup
int schema_peek_lastbackup(const char* localpath, jvalue* schema)
{
    if(schema->type != JSON_OBJECT) return SCHEMA_FAILURE;
    jvalue* current = json_search_by_key(localpath, schema);
    if(current == NULL) return SCHEMA_FAILURE;
    return (int)json_search_by_key("lastbackup", current)->number;
}
