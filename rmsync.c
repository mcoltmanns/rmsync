#include "schema_manager.h"
#include "remote_interface.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

enum STATUS {
	OK,
	NO_ACTION,
	UNKNOW_ARG,
	CONTAINS_RSYNC,
	INVALID_PATH,
	OUT_OF_MEMORY,
	NO_RSYNC,
	BAD_SCHEMA
};

/**
 * recurse upwards until we find a .rmschema. return a pointer to that schema in binary r/w
*/
FILE* find_schema_up(char* absolute_path)
{
	FILE* f = fopen(absolute_path, "r+"); // try to open the file
	printf("trying %s\n", absolute_path);
	if(f) return f; // if the file was there, open it
	else // go up a level
	{
		printf("path is %s\n", absolute_path);
		if(strcmp(absolute_path, "/.rmsync") == 0) return NULL;
		*(strstr(absolute_path, "/.rmsync")) = 0;
		char* last_dir = strrchr(absolute_path, '/') + 1;
		*(last_dir + sizeof ".rmsync") = 0;
		strcpy(last_dir, ".rmsync");
		return find_schema_up(absolute_path);
	}
}

/**
 * working schema is the first schema found when recursing upwards
 * args structure?
 * argv[1]: action
 * 		i(nit): make a schema at the specified location. if no location specified, make a schema here
 * 		f(etch): run fetch_all on the specified schema. if no schema specified, use the working schema
 * 		a(dd): associate a local path with a remote path, and add it to the working schema
 * 		r(emove): remove a local path from the working schema
*/
int main(int argc, char* argv[])
{
	if(argc <= 1)
	{
		printf("Please specifiy an action to perform (run \"%s help\" for a list of options).\n", argv[0]);
		return NO_ACTION;
	}

	if(strcmp(argv[1], "init") == 0 || strcmp(argv[1], "i") == 0)
	{
		// init - create an empty .rmsync file in the specified directory. if no directory specified, create in working directory
		// if an .rmsync file already exists in the given directory, fail.
		char* path; // current absolute working directory
		char* subpath = argc > 2 ? argv[2] : ""; // if a subpath was provided, use that. otherwise provide an empty subpath
		int needs_final_delim = *(strrchr(subpath, 0) - 1) != '/';
		if(subpath[0] == '/') // absolute path
		{
			path = malloc(strlen(subpath) + needs_final_delim + sizeof ".rmsync");
			strcpy(path, subpath);
			if(needs_final_delim) strcat(path, "/");
			strcat(path, ".rmsync");
		}
		else // relative path
		{
			path = getcwd(NULL, 0); // get current working directory (never has a final delimiter)
			path = realloc(path, strlen(path) + 1 + strlen(subpath) + needs_final_delim + sizeof ".rmsync");
			if(path == NULL)
			{
				printf("FATAL: Out of memory!\n");
				free(path);
				return OUT_OF_MEMORY;
			}
			strcat(path, "/");
			strcat(path, subpath);
			if(needs_final_delim) strcat(path, "/");
			strcat(path, ".rmsync");
		}
		char dir_path[strlen(path) + 1]; // path to the schema's directory
		strcpy(dir_path, path);
		*strrchr(dir_path, '/') = 0;
		printf("Initializing rmsync in directory \"%s\"\n", dir_path);
		FILE *f = fopen(path, "r"); // check if the file exists
		if(f) // file exists! so we can't initialize here
		{
			fclose(f);
			printf("FATAL: \"%s\" already contains a .rmsync schema file.\n", dir_path);
			free(path);
			return CONTAINS_RSYNC;
		}
		else // file doesn't exist - create it
		{
			f = fopen(path, "w"); // ok to overwrite because we already checked if exists
			if(f == NULL) // still can't open - means path was invalid
			{
				printf("FATAL: invalid path \"%s\"\n", dir_path);
				free(path);
				return INVALID_PATH;
			}
			fprintf(f, "{}"); // populate .rmsync with an empty json object
			fclose(f);
			printf("Initialized empty rmsync at \"%s\"\n", dir_path);
		}
		free(path);
		return OK;
	}
	
	else if (strcmp(argv[1], "fetch") == 0 || strcmp(argv[1], "f") == 0)
	{
		// fetch all
		char* path = getcwd(NULL, 0);
		path = realloc(path, strlen(path) + sizeof "/.rmsync");
		if(path == NULL)
		{
			printf("FATAL: Out of memory!\n");
			free(path);
			return OUT_OF_MEMORY;
		}
		strcat(path, "/.rmsync");
		FILE* schema_file = find_schema_up(path);
		if(schema_file) 
		{
			jvalue* schema = malloc(sizeof(jvalue));
			if(load_schema(schema_file, schema) == JSON_FAILURE)
			{
				printf("FATAL: invalid schema at \"%s\"", path);
				fclose(schema_file);
				json_free_value(schema);
				free(path);
				return BAD_SCHEMA;
			}
			fetch_all(schema);
			char* schema_string = jval_to_str(schema); // have to write schema again because of potentially changed backup times
			if(schema_string)
			{
				fseek(schema_file, 0, SEEK_SET); // go to beginning of file
				if(fputs(schema_string, schema_file) == EOF) printf("bad write! status %i\n", errno);
				free(schema_string);
			}
			json_free_value(schema);
			fclose(schema_file);
			free(path);
			return OK;
		}
		else
		{
			char* cwd = getcwd(NULL, 0);
			printf("FATAL: \"%s\" is not a child of an rmsync directory\n", cwd);
			free(cwd);
			free(path);
			return NO_RSYNC;
		}
	}

	else if (strcmp(argv[1], "add") == 0 || strcmp(argv[1], "a") == 0)
	{
		if(argc < 4)
		{
			printf("Bad options. Provide a remote and a local path to add a file.\n");
			return UNKNOW_ARG;
		}
		// add a file
		char* syncfile_path = getcwd(NULL, 0);
		syncfile_path = realloc(syncfile_path, strlen(syncfile_path) + sizeof "/.rmsync");
		if(syncfile_path == NULL)
		{
			printf("FATAL: Out of memory!\n");
			free(syncfile_path);
			return OUT_OF_MEMORY;
		}
		strcat(syncfile_path, "/.rmsync");
		FILE* schema_file = find_schema_up(syncfile_path);
		if(schema_file)
		{
			jvalue* schema = calloc(sizeof(jvalue), 1);
			if(load_schema(schema_file, schema) == JSON_FAILURE)
			{
				printf("FATAL: invalid schema at \"%s\"\n", syncfile_path);
				json_free_value(schema);
				free(syncfile_path);
				fclose(schema_file);
				return BAD_SCHEMA;
			}
			start_tracking(argv[2], argv[3], schema);
			char* schema_string = jval_to_str(schema);
			if(schema_string)
			{
				fseek(schema_file, 0, SEEK_SET); // go to beginning of file
				if(fputs(schema_string, schema_file) == EOF) printf("bad write! status %i\n", errno);
				free(schema_string);
			}
			json_free_value(schema);
			fclose(schema_file);
		}
		else
		{
			char* cwd = getcwd(NULL, 0);
			printf("FATAL: \"%s\" is not a child of an rmsync directory\n", cwd);
			free(cwd);
			free(syncfile_path);
			return NO_RSYNC;
		}
		free(syncfile_path);
		return OK;
	}

	else if (strcmp(argv[1], "remove") == 0 || strcmp(argv[1], "r") == 0)
	{
		// remove a file
		if(argc < 3)
		{
			printf("Bad options. Provide a remote and a local path to add a file.\n");
			return UNKNOW_ARG;
		}
		char* syncfile_path = getcwd(NULL, 0);
		syncfile_path = realloc(syncfile_path, strlen(syncfile_path) + sizeof "/.rmsync");
		if(syncfile_path == NULL)
		{
			printf("FATAL: Out of memory!\n");
			free(syncfile_path);
			return OUT_OF_MEMORY;
		}
		strcat(syncfile_path, "/.rmsync");
		FILE* schema_file = find_schema_up(syncfile_path);
		if(schema_file)
		{
			jvalue* schema = calloc(sizeof(jvalue), 1);
			if(load_schema(schema_file, schema) == JSON_FAILURE)
			{
				printf("FATAL: invalid schema at \"%s\"\n", syncfile_path);
				json_free_value(schema);
				free(syncfile_path);
				fclose(schema_file);
				return BAD_SCHEMA;
			}
			stop_tracking(argv[2], schema);
			char* schema_string = jval_to_str(schema);
			if(schema_string)
			{
				fseek(schema_file, 0, SEEK_SET); // go to beginning of file
				if(fputs(schema_string, schema_file) == EOF) printf("bad write! status %i\n", errno);
				free(schema_string);
			}
			json_free_value(schema);
			fclose(schema_file);
		}
		else
		{
			char* cwd = getcwd(NULL, 0);
			printf("FATAL: \"%s\" is not a child of an rmsync directory\n", cwd);
			free(cwd);
			free(syncfile_path);
			return NO_RSYNC;
		}
		free(syncfile_path);
		return OK;
	}

	else if(strcmp(argv[1], "help") == 0 || strcmp(argv[1], "h") == 0)
	{
		// print the help menu
		printf("Possible actions are:\n\ti(nit): initialize a schema in the working directory\n\tf(etch): back up all items in the first schema found when recursing upwards\n\ta(dd) <remote> <local>: associate a local path with a remote path, and add it to the first schema found when recursing upwards\n\tr(emove) <local>: remove a local path from the first schema found when recursing upwards\n");
		return OK;
	}

	else
	{
		printf("Unknown option \"%s\".\n", argv[1]);
		return UNKNOW_ARG;
	}
}
