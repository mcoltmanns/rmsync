// functions for talking to the remarkable
// what does this need to be able to do?
// all remote interface functions, given a schema
// - add files to the schema (given a remote path and a local path, start tracking)
// - remove files from the schema (given a local path, stop tracking) (cleanup?/delete local file - maybe as an option - should not be handled here!) 
// - fetch a single file (given a local path) based on local and remote timestamps
// - fetch all files

#include "remote_interface.h"
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <sys/stat.h> // for mkdir

struct cbuf {
    char* contents; // null-terminated string
    size_t size; // size of that string including the null byte
};

// given a path to a file, recursively make those directories
static void _mkdir(const char *dir) {
    char tmp[strlen(dir)];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
}

// write callback for curl - write to null-terminated character buffer
// data_in points to delivered data
// size is always 1
// nmeb is the size of the data delivered
// buffer is where we want the data written
size_t write_cbuf(char* data_in, size_t size, size_t nmeb, struct cbuf* buffer)
{
    size_t realsize = size * nmeb; // should be unnecessary, but can't hurt to be safe
    //struct cbuf* cbuf = (struct cbuf*)buffer; // cast the buffer
    char* new_mem = realloc(buffer->contents, buffer->size + realsize); // allocate new memory TODO: these pointers are getting lost very bad
    if(new_mem == NULL)
    {
        free(buffer->contents);
        printf("Out of memory!\n");
        return 0; // wrote no bytes
    }
    buffer->contents = new_mem;
    memcpy(buffer->contents + buffer->size - 1, data_in, realsize); // copy the new data into the buffer
    buffer->size += realsize; // increase the real size
    buffer->contents[buffer->size - 1] = 0; // put a null terminator on
    return realsize;
}

// delete all members that aren't ID, ModifiedClient, Parent, Type, VissibleName from the passed jvalue
static void format_remote_info(jvalue* target_info)
{
    if(target_info == NULL) return;
    jmember* prev = NULL;
    jmember* curr = target_info->members;
    while(curr != NULL)
    {
        if(strcmp("ID", curr->string) && strcmp("ModifiedClient", curr->string) && strcmp("Parent", curr->string) && strcmp("Type", curr->string) && strcmp("VissibleName", curr->string)) // no match with any of the keys we want - delete this object
        {
            if(prev == NULL) // have to delete first element
                target_info->members = curr->next;
            else // have to delete a given element
                prev->next = curr->next;
            jmember* next = curr->next; // free the member
            free(curr->string);
            json_free_value(curr->element);
            free(curr);
            curr = next;
        }
        else // matched something - don't delete
        {
            prev = curr;
            curr = curr->next;
        }
    }
}

// if the remote path is valid, return an object containing the VissibleName, ID, ModifiedClient, and Type fields of the file/folder
// if the path is invalid, return NULL
// this method should only be used when a schema doesn't contain valid information for a file, eg. when indexing a new file for the first time
jvalue* index_file(char* remote)
{
    int termcount = 0;
    for(int i = 0; remote[i]; i++) termcount += remote[i] == REMOTE_PATH_SEP[0] ? 1 : 0;
    char* terms[termcount];
    char remote_chunks[strlen(remote) + 1];
    strcpy(remote_chunks, remote);
    terms[0] = strtok(remote_chunks, REMOTE_PATH_SEP); // chunk first path term
    for(int i = 1; i < termcount; i++) terms[i] = strtok(NULL, REMOTE_PATH_SEP); // chunk rest of path
    for(int i = 0; i < termcount; i++) // make sure the chunking is valid (no paths like "/foo//bar")
    { 
        if(!terms[i])
        {
            printf("Invalid path \"%s\" (term %i)\n", remote, i);
            exit(1);
        }
    }
    // find the file
    CURL* curl_handle = curl_easy_init(); // get a curl handle
    CURLcode res;
    char url[28 + 37 + 1] = "http://10.11.99.1/documents/";
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cbuf); // tell curl to use the write_cbuf callback
    struct cbuf request_buffer;
    jvalue* target_info = NULL;
    for(int i = 0; i < termcount; i++) // for all terms
    {
        request_buffer.contents = malloc(1); // these MUST be initialized to 1!
        request_buffer.size = 1;
        if(request_buffer.contents == NULL) { printf("Out of memory!\n"); curl_easy_cleanup(curl_handle); return NULL; }
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &request_buffer); // tell curl to tell write_cbuf to write into request_buffer
        curl_easy_setopt(curl_handle, CURLOPT_URL, url); // point curl at the next json blob
        res = curl_easy_perform(curl_handle); // get the information for the parent of the current term
        if(res != CURLE_OK)
        {
            printf("CURL error %i\n", res);
            free(request_buffer.contents);
            curl_easy_cleanup(curl_handle);
            return NULL;
        }
        jvalue* term_parent_info = malloc(sizeof(jvalue)); // allocate space for a json object with information about this term's parent
        if(term_parent_info == NULL) { printf("Out of memory!\n"); free(request_buffer.contents); curl_easy_cleanup(curl_handle); return NULL; }
        const char* term_info_string = request_buffer.contents;
        if(json_parse_value(&term_info_string, term_parent_info) == JSON_FAILURE || term_parent_info->type != JSON_ARRAY) 
        {
            printf("Error: bad json!\n");
            free(request_buffer.contents);
            json_free_value(term_parent_info);
            curl_easy_cleanup(curl_handle);
            return NULL;
        }
        free(request_buffer.contents); // done with the buffer
        for(int j = 0; term_parent_info->elements[j] != NULL; j++) // check every item in the parent folder
        {
            jvalue* name = json_search_by_key("VissibleName", term_parent_info->elements[j]);
            if(!strcmp(name->string, terms[i])) // got a match!
            {
                if(i == termcount - 1) // on last term - package things up in term_info - don't free!
                {
                    target_info = term_parent_info->elements[j];
                }
                else // not on last term - set a new url and free the parent info element
                {
                    strcpy(url + 28, json_search_by_key("ID", term_parent_info->elements[j])->string);
                    json_free_value(term_parent_info->elements[j]);
                }
            }
            else
            {
                json_free_value(term_parent_info->elements[j]);
            }
        }
        free(term_parent_info->elements);
        free(term_parent_info);
    }
    curl_easy_cleanup(curl_handle);
    format_remote_info(target_info);
    return target_info;
}

// get information about an already indexed file
// accepts a schema entry
// returns a jvalue - must be freed when done!
jvalue* get_remote_info(jmember* schema_entry)
{
    char* parent_id = json_search_by_key("Parent", schema_entry->element)->string;
    char* target_id = json_search_by_key("ID", schema_entry->element)->string;
    char url[28 + 37 + 1] = "http://10.11.99.1/documents/";
    strcat(url, parent_id);
    CURL* curl_handle = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cbuf);
    struct cbuf request_buffer = { malloc(1), 1 };
    if(request_buffer.contents == NULL) { printf("Out of memory!\n"); curl_easy_cleanup(curl_handle); return NULL; }
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &request_buffer);
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    res = curl_easy_perform(curl_handle);
    if(res != CURLE_OK)
    {
        printf("CURL error %i\n", res);
        free(request_buffer.contents);
        curl_easy_cleanup(curl_handle);
        return NULL;
    }
    const char* cursor = request_buffer.contents;
    jvalue* parent_info = malloc(sizeof(jvalue));
    if(parent_info == NULL) { printf("Out of memory!\n"); free(request_buffer.contents); curl_easy_cleanup(curl_handle); return NULL; }
    if(json_parse_value(&cursor, parent_info) == JSON_FAILURE || parent_info->type != JSON_ARRAY) { printf("Error: bad json!\n"); free(request_buffer.contents); json_free_value(parent_info); curl_easy_cleanup(curl_handle); return NULL; }
    free(request_buffer.contents);
    jvalue* target_info = NULL;
    for(int i = 0; parent_info->elements[i] != NULL; i++)
    {
        jvalue* id = json_search_by_key("ID", parent_info->elements[i]);
        if(!strcmp(id->string, target_id)) // matched!
        {
            target_info = parent_info->elements[i];
        }
        else
            json_free_value(parent_info->elements[i]);
    }
    free(parent_info->elements);
    free(parent_info);
    curl_easy_cleanup(curl_handle);
    format_remote_info(target_info);
    return target_info;
}

void start_tracking(char* remote, char* local, jvalue* schema)
{
    if(json_search_by_key(local, schema) != NULL)
    {
        printf("File \"%s\" is already tracked.\n", local);
        return;
    }
    jvalue* remote_info = index_file(remote);
    if(remote_info == NULL)
    {
        printf("File \"%s\" could not be found.\n", remote);
        return;
    }
    json_add_member(local, remote_info, schema);
}

void stop_tracking(char* local, jvalue* schema)
{
    json_delete_first_member(local, schema);
}

// download a schema entry, only if the remote file is newer than the recorded last backup
// returns curl status code
CURLcode fetch(jmember* schema_entry, CURL* curl_handle)
{
    printf("Fetching %s... ", schema_entry->string);
    jvalue* remote_info = get_remote_info(schema_entry);
    char* last_edit = json_search_by_key("ModifiedClient", remote_info)->string;
    jvalue* last_backup = json_search_by_key("LastBackup", schema_entry->element);
    CURLcode res = CURLE_OK;
    if(last_backup == NULL || strcmp(last_backup->string, last_edit) < 0) // if never backed up or last backup is older than last edit
    {
        printf("remote is newer. ");
        // download!
        FILE* f = fopen(schema_entry->string, "wb"); // open the file to write to
        if(f == NULL)
        {
            printf("Creating parent directories... ");
            _mkdir(schema_entry->string); // try making the parent directories for the file
            f = fopen(schema_entry->string, "wb"); // try opening the file again
            if(f == NULL)
            {
                printf("could not open file %s.\n", schema_entry->string);
                json_free_value(remote_info);
                res = CURLE_WRITE_ERROR; // TODO: make this less hacky
                return res;
            }
            printf("done.");
        }
        char* remote_id = json_search_by_key("ID", schema_entry->element)->string; // get the id of the file we want to write to
        char url[27 + 37 + 1 + 3 + 1] = "http://10.11.99.1/download/"; // put together the url
        strcat(strcat(url, remote_id), "/pdf");
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, fwrite);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        time_t now;
        time(&now);
        printf("\nDownloading: %s ==> %s ... ", url, schema_entry->string);
        fflush(stdout);
        res = curl_easy_perform(curl_handle);
        printf("finished with CURL status %i.\n", res);
        fclose(f);
        char time_str[sizeof "0000-00-00T00:00:00Z"];
        strftime(time_str, sizeof time_str, "%FT%TZ", gmtime(&now)); // get the backup string
        if(last_backup == NULL) // file hasn't been backed up yet
        {
            last_backup = malloc(sizeof(jvalue)); // set up the backup string
            last_backup->type = JSON_STRING;
            last_backup->string = malloc(sizeof "0000-00-00T00:00:00Z");
            strcpy(last_backup->string, time_str);
            json_add_member("LastBackup", last_backup, schema_entry->element);
        }
        else
        {
            strcpy(last_backup->string, time_str); // update the backup string
        }
    }
    else printf("no new changes.\n");
    json_free_value(remote_info);
    return res;
}

// fetch all members in a schema
void fetch_all(jvalue* schema)
{
    CURL* curl_handle = curl_easy_init();
    jmember* curr = schema->members;
    while(curr != NULL)
    {
        fetch(curr, curl_handle);
        printf("\n");
        curr = curr->next;
    }
    curl_easy_cleanup(curl_handle);
}
