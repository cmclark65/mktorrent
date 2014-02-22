/*
This file is part of mktorrent
Copyright (C) 2007, 2009 Emil Renner Berthing

mktorrent is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

mktorrent is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#ifndef ALLINONE
#include <stdlib.h>       /* exit() */
#include <sys/types.h>    /* off_t */
#include <errno.h>        /* errno */
#include <string.h>       /* strerror() */
#include <stdio.h>        /* perror(), printf() etc. */
#include <sys/stat.h>     /* the stat structure */
#include <unistd.h>       /* getopt(), getcwd(), sysconf() */
#include <string.h>       /* strcmp(), strlen(), strncpy() */
#include <strings.h>      /* strcasecmp() */
#include <inttypes.h>     /* PRId64 etc */
#ifdef USE_LONG_OPTIONS
#include <getopt.h>       /* getopt_long() */
#endif

#include "mktorrent.h"
#include "ftw.h"

#define EXPORT

/* output.c */
extern int is_bencode_int(char *s);
#else  /* ALLINONE */
/* output.c is included after init.c */
int is_bencode_int(char *s);
#endif /* ALLINONE */

#ifndef MAX_OPENFD
#define MAX_OPENFD 100	/* Maximum number of file descriptors
			   file_tree_walk() will open */
#endif

static void strip_ending_dirseps(char *s)
{
	char *end = s;

	while (*end)
		end++;

	while (end > s && *(--end) == DIRSEP[0])
		*end = '\0';
}

static const char *basename(const char *s)
{
	const char *r = s;

	while (*s != '\0') {
		if (*s == DIRSEP[0])
			r = ++s;
		else
			++s;
	}

	return r;
}

static char *realloc_str(char *s, size_t l)
{
#ifdef DEBUG
	fprintf(stderr, PROGRAM ": allocating %u bytes\n", l);
#endif				/* DEBUG */
	s = realloc(s, l);
	if (s == NULL) {
		perror(PROGRAM ": Memory allocation error");
		exit(EXIT_FAILURE);
	}
	return s;
}

static void set_absolute_file_path(metafile_t *m)
{
	int is_dir;		/* is metainfo_file_path a dir */
	int is_rel;		/* is metainfo_file_path relative */
	char *string;		/* string to return */
	struct stat s;		/* stat structure for stat() to fill */
	size_t length = 128;	/* size of the string buffer */
	size_t newlength;	/* calculated length of path */

	/*
	 * There are five cases to handle; metafile_file_path not set, set
	 * to a directory path either absolute or relative, set to a non
	 * directory path (that likely currently does not exist) either
	 * absolute or relative.
	 *
	 * Four of those cases start with a path, so check if it is
	 * relative or not and check if it is a dir or not. The fifth case
	 * is effectively a path of ".", so no testing is required to mark
	 * it as both relative and a dir.
	 */
	if (m->metainfo_file_path) {
		/* is_rel set by checking first char for DIRSEP */
		is_rel = (*m->metainfo_file_path != DIRSEP[0]);
		/* stat the file_path */
		if (stat(m->metainfo_file_path, &s)) {
			if (errno == ENOENT) {
				is_dir = 0;
				errno = 0;
			} else {
				fprintf(stderr, PROGRAM
					": Error stat'ing '%s': %s\n",
					m->metainfo_file_path,
					strerror(errno));
				exit(EXIT_FAILURE);
			}
		} else
			is_dir = S_ISDIR(s.st_mode);
		/*
		 * if the file_path is already an absolute path and not a
		 * directory just return that
		 */
		if (!is_dir && !is_rel)
			return;
	} else {
		is_rel = 1;
		is_dir = 1;
	}
	/* allocate initial string */
	string = realloc_str(NULL, length);
	/*
	 * If it is relative, we need to get getcwd and prepend that in
	 * order to have an absolute path because the program will change
	 * working dirs before opening the file.
	 *
	 * If not relative, it must be the absolute dir case so copy the
	 * path into string.
	 */
	if (is_rel) {
		/* first get the current working directory
		   using getcwd is a bit of a PITA */
		while (getcwd(string, length) == NULL) {
			if (errno != ERANGE) {
				perror(PROGRAM
				       ": Error getting working directory");
				exit(EXIT_FAILURE);
			}
			/* double the buffer size */
			length *= 2;
			/* reallocate a new one twice as big muahaha */
			string = realloc_str(string, length);
		}
		if (m->metainfo_file_path) {
			newlength = strlen(string)
			    + strlen(m->metainfo_file_path) + 2;
			if (length < newlength) {
				length = newlength;
				string = realloc_str(string, length);
			}
			sprintf(string + strlen(string), DIRSEP "%s",
				m->metainfo_file_path);
		}
	} else {
		newlength = strlen(m->metainfo_file_path) + 1;
		if (length < newlength) {
			length = newlength;
			string = realloc_str(string, length);
		}
		strcpy(string, m->metainfo_file_path);
	}

	/*
	 * If it is a dir, add .torrent to the torrent_name and append that
	 * to the directory path.
	 */
	if (is_dir) {
		newlength = strlen(string) + strlen(m->torrent_name)
		    + strlen(".torrent") + 2;
		if (length < newlength) {
			length = newlength;
			string = realloc_str(string, length);
		}
		sprintf(string + strlen(string), DIRSEP "%s.torrent",
			m->torrent_name);
	}

	/*
	 * Change metainfo_file_path to point to the new string and done.
	 */
	m->metainfo_file_path = string;
}

/*
 * add an extra info dictionary field node from a colon separated string
 */
static void add_extra(metafile_t *m, char *s)
{
	int i;			/* loop iterator */
	elist_t *extra_new = NULL;	/* new extra node */
	elist_t *extra_cur = NULL;	/* for traversing the list */
	char *extra_value = NULL;	/* used to find start of value string */
	char *info_dict_fields[] = {
		"files",
		"length",
		"md5sum",
		"name",
		"piece length",
		"pieces",
		"private"
	};
	int num_info_dict_fields =
	    sizeof(info_dict_fields) / sizeof(info_dict_fields[0]);

	/* create new node */
	extra_new = malloc(sizeof(elist_t));
	if (extra_new == NULL) {
		fprintf(stderr, PROGRAM ": Out of memory.\n");
		exit(EXIT_FAILURE);
	}

	/* set new node's key */
	extra_new->key = s;
	/*
	 * set new node's value 
	 *
	 * find the : and terminate the key string by changing it to a null
	 * char; the value string starts with the following character.
	 */
	extra_new->value = NULL;
	extra_value = s;
	while (*extra_value != '\0') {
		if (*extra_value == ':') {
			*extra_value = '\0';
			extra_new->value = extra_value + 1;
		} else
			extra_value++;
	}
	/* didn't find a : */
	if (extra_new->value == NULL) {
		fprintf(stderr, PROGRAM ": Bad extra option string: %s\n",
			extra_new->key);
		fprintf(stderr, "The extra option must have a key string"
			" and a value separated by a colon.\n");
		exit(EXIT_FAILURE);
	}
	/* initialize new node's next ptr */
	extra_new->next = NULL;

	/* test against canonical fields */
	for (i = 0; i < num_info_dict_fields; i++)
		if (strcmp(extra_new->key, info_dict_fields[i]) == 0) {
			fprintf(stderr, PROGRAM ": Extra info dictionary"
				" key '%s' not allowed\n", extra_new->key);
			fprintf(stderr, "An extra info dictionary key must"
				" not be the same as any required key.\n");
			exit(EXIT_FAILURE);
		}

	/* insert new node in m->extra list */
	if (m->extra == NULL)
		m->extra = extra_new;
	else if (strcmp(extra_new->key, m->extra->key) < 0) {
		extra_new->next = m->extra;
		m->extra = extra_new;
	} else {
		extra_cur = m->extra;
		while (extra_cur && extra_cur->next) {
			i = strcmp(extra_cur->next->key, extra_new->key);
			if (i < 0)
				extra_cur = extra_cur->next;
			else if (i == 0) {
				fprintf(stderr, PROGRAM ": Duplicate extra"
					" info dictionary key '%s' not"
					" allowed\n", extra_new->key);
				fprintf(stderr, "An extra info dictionary"
					" key must be unique.\n");
				exit(EXIT_FAILURE);
			} else {
				extra_new->next = extra_cur->next;
				extra_cur->next = extra_new;
				extra_cur = NULL;
			}
		}
		if (extra_cur)
			extra_cur->next = extra_new;
	}
}

/*
 * parse a comma separated list of strings <str>[,<str>]* and
 * return a string list containing the substrings
 */
static slist_t *get_slist(char *s)
{
	slist_t *list, *last;
	char *e;

	/* allocate memory for the first node in the list */
	list = last = malloc(sizeof(slist_t));
	if (list == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(EXIT_FAILURE);
	}

	/* add URLs to the list while there are commas in the string */
	while ((e = strchr(s, ','))) {
		/* set the commas to \0 so the URLs appear as
		 * separate strings */
		*e = '\0';
		last->s = s;

		/* move s to point to the next URL */
		s = e + 1;

		/* append another node to the list */
		last->next = malloc(sizeof(slist_t));
		last = last->next;
		if (last == NULL) {
			fprintf(stderr, "Out of memory.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* set the last string in the list */
	last->s = s;
	last->next = NULL;

	/* return the list */
	return list;
}

/*
 * checks if target is a directory
 * sets the file_list and size if it isn't
 */
static int is_dir(metafile_t *m, char *target)
{
	struct stat s;		/* stat structure for stat() to fill */

	/* stat the target */
	if (stat(target, &s)) {
		fprintf(stderr, "Error stat'ing '%s': %s\n",
				target, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* if it is a directory, just return 1 */
	if (S_ISDIR(s.st_mode))
		return 1;

	/* if it isn't a regular file either, something is wrong.. */
	if (!S_ISREG(s.st_mode)) {
		fprintf(stderr,
			"'%s' is neither a directory nor regular file.\n",
				target);
		exit(EXIT_FAILURE);
	}

	/* since we know the torrent is just a single file and we've
	   already stat'ed it, we might as well set the file list */
	m->file_list = malloc(sizeof(flist_t));
	if (m->file_list == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(EXIT_FAILURE);
	}
	m->file_list->path = target;
	m->file_list->size = s.st_size;
	m->file_list->next = NULL;
	/* ..and size variable */
	m->size = s.st_size;

	/* now return 0 since it isn't a directory */
	return 0;
}

/*
 * called by file_tree_walk() on every file and directory in the subtree
 * counts the number of (readable) files, their commulative size and adds
 * their names and individual sizes to the file list
 */
static int process_node(const char *path, const struct stat *sb, void *data)
{
	flist_t **p;            /* pointer to a node in the file list */
	flist_t *new_node;      /* place to store a newly created node */
	metafile_t *m = data;

	/* skip non-regular files */
	if (!S_ISREG(sb->st_mode))
		return 0;

	/* ignore the leading "./" */
	path += 2;

	/* now path should be readable otherwise
	 * display a warning and skip it */
	if (access(path, R_OK)) {
		fprintf(stderr, "Warning: Cannot read '%s', skipping.\n", path);
		return 0;
	}

	if (m->verbose)
		printf("Adding %s\n", path);

	/* count the total size of the files */
	m->size += sb->st_size;

	/* find where to insert the new node so that the file list
	   remains ordered by the path */
	p = &m->file_list;
	while (*p && strcasecmp(path, (*p)->path) > 0)
		p = &((*p)->next);

	/* create a new file list node for the file */
	new_node = malloc(sizeof(flist_t));
	if (new_node == NULL ||
			(new_node->path = strdup(path)) == NULL) {
		fprintf(stderr, "Out of memory.\n");
		return -1;
	}
	new_node->size = sb->st_size;

	/* now insert the node there */
	new_node->next = *p;
	*p = new_node;

	/* insertion sort is a really stupid way of sorting a list,
	   but usually a torrent doesn't contain too many files,
	   so we'll probably be alright ;) */
	return 0;
}

/*
 * 'elp!
 */
static void print_help()
{
	printf(
	  "Usage: mktorrent [OPTIONS] <target directory or filename>\n\n"
	  "Options:\n"
	);
#ifdef USE_LONG_OPTIONS
	printf(
	  "-a, --announce=<url>[,<url>]* : specify the full announce URLs\n"
	  "                                at least one is required\n"
	  "                                additional -a adds backup trackers\n"
	  "-c, --comment=<comment>       : add a comment to the metainfo\n"
	  "-d, --no-date                 : don't write the creation date\n"
	);
	printf(
	  "-e, --extra=<key:value>       : extra optional info dictionary fields\n"
	  "                                value can be a string or integer, for example\n"
	  "                                sourced:from_monkeys or version:i87e\n"
	  "-f, --force                   : overwrite existing metainfo file\n"
	  "-h, --help                    : show this help screen\n"
	);
	printf(
	  "-l, --piece-length=<n>        : set the piece length to 2^n bytes,\n"
	  "                                default is calculated from the total size\n"
	  "-n, --name=<name>             : set the name of the torrent\n"
	  "                                default is the basename of the target\n"
	  "-o, --output=<filename>       : set the path and filename of the created file\n"
	);
	printf(
	  "                                default is <name>.torrent\n"
	  "-p, --private                 : set the private flag\n"
	);
#ifdef USE_PTHREADS
	printf(
	  "-t, --threads=<n>             : use <n> threads for calculating hashes\n"
	  "                                default is the number of CPU cores\n"
	);
#endif				/* USE_PTHREADS */
	printf(
	  "-v, --verbose                 : be verbose\n"
	  "-w, --web-seed=<url>[,<url>]* : add web seed URLs\n"
	  "                                additional -w adds more URLs\n"
	);
#else				/* USE_LONG_OPTIONS */
	printf(
	  "-a <url>[,<url>]* : specify the full announce URLs\n"
	  "                    at least one is required\n"
	  "                    additional -a adds backup trackers\n"
	  "-c <comment>      : add a comment to the metainfo\n"
	  "-d                : don't write the creation date\n"
	);
	printf(
	  "-e <key:value>    : extra optional info dictionary fields\n"
	  "                    value can be a string or integer, for example\n"
	  "                    sourced:from_monkeys or version:i87e\n"
	  "-f                : overwrite existing metainfo file\n"
	  "-h                : show this help screen\n"
	);
	printf(
	  "-l <n>            : set the piece length to 2^n bytes,\n"
	  "                    default is calculated from the total size\n"
	  "-n <name>         : set the name of the torrent,\n"
	  "                    default is the basename of the target\n"
	  "-o <filename>     : set the path and filename of the created file\n"
	);
	printf(
	  "                    default is <name>.torrent\n"
	  "-p                : set the private flag\n"
	);
#ifdef USE_PTHREADS
	printf(
	  "-t <n>            : use <n> threads for calculating hashes\n"
	  "                    default is the number of CPU cores\n"
	);
#endif				/* USE_PTHREADS */
	printf(
	  "-v                : be verbose\n"
	  "-w <url>[,<url>]* : add web seed URLs\n"
	  "                    additional -w adds more URLs\n"
	);
#endif				/* USE_LONG_OPTIONS */
	printf(
	  "\nPlease send bug reports, patches, feature requests, praise and\n"
	  "general gossip about the program to: esmil@users.sourceforge.net\n"
	);
}

/*
 * print the full announce list
 */
static void print_announce_list(llist_t *list)
{
	unsigned int n;

	for (n = 1; list; list = list->next, n++) {
		slist_t *l = list->l;

		printf("    %u : %s\n", n, l->s);
		for (l = l->next; l; l = l->next)
			printf("        %s\n", l->s);
	}
}

/*
 * print the list of web seed URLs
 */
static void print_web_seed_list(slist_t *list)
{
	printf("  Web Seed URL: ");

	if (list == NULL) {
		printf("none\n");
		return;
	}

	printf("%s\n", list->s);
	for (list = list->next; list; list = list->next)
		printf("                %s\n", list->s);
}

/*
 * print the extra user specified info dictionary fields
 */
#define PADDED_LEN_MAX 12
static void print_extra(elist_t *extra)
{
	const size_t stupid_long = 25;
	char *s;
	size_t c;
	size_t longest = 0;
	char padded_len[PADDED_LEN_MAX];
	int padding_l;
	elist_t *e;

	printf("  Extra fields:\n");
	/* find length of longest extra key, but ignore stupid long ones */
	e = extra;
	while (e) {
		if (((c = strlen(e->key)) > longest) && (c < stupid_long))
			longest = c;
		e = e->next;
	}
	/* print aligned columns of keys and values
	   surround keys and values with double quotes since they
	   may contain spaces or be 0 length strings */
	e = extra;
	while (e) {
		padding_l = longest - strlen(e->key);
		/* for stupid long keys padding_l will be negative. */
		padding_l = (padding_l < 0) ? 1 : padding_l + 1;
		/* getting a double quote char after the initial spaces but
		   prior to the printed key string length is painful. */
		if (snprintf(padded_len, PADDED_LEN_MAX, " %4d",
			     (int) strlen(e->key)) < 0) {
			perror(PROGRAM);
			exit(EXIT_FAILURE);
		}
		s = padded_len;
		while (s[1] == ' ')
			s++;
		*s = '"';
		printf("    %s:%s\"%*s--> ", padded_len, e->key,
		       padding_l, " ");
		if (is_bencode_int(e->value))
			printf("\"%s\"\n", e->value);
		else
			printf("\"%d:%s\"\n", (int) strlen(e->value),
			       e->value);
		e = e->next;
	}
}

/*
 * print out all the options
 */
static void dump_options(metafile_t *m)
{
	printf("Options:\n"
	       "  Announce URLs:\n");

	print_announce_list(m->announce_list);

	printf("  Torrent name: %s\n"
	       "  Metafile:     %s\n",
	       m->torrent_name, m->metainfo_file_path);

	printf("  Overwrite:    ");
	if (m->force)
		printf("yes\n");
	else
		printf("no\n");

	printf("  Private:      ");
	if (m->private)
		printf("yes\n");
	else
		printf("no\n");

	printf("  Piece length: ");
	if (m->piece_length)
		printf("%u\n", m->piece_length);
	else
		printf("automatic\n");

#ifdef USE_PTHREADS
	printf("  Threads:      %ld\n",
	       m->threads);
#endif
	printf("  Be verbose:   yes\n"
	       "  Write date:   ");
	if (m->no_creation_date)
		printf("no\n");
	else
		printf("yes\n");

	print_web_seed_list(m->web_seed_list);

	printf("  Comment:      ");
	if (m->comment == NULL)
		printf("none\n");
	else
		printf("\"%s\"\n", m->comment);

	if (m->extra)
		print_extra(m->extra);

	printf("\n");
}

/*
 * parse and check the command line options given
 * and fill out the appropriate fields of the
 * metafile structure
 */
EXPORT void init(metafile_t *m, int argc, char *argv[])
{
	int c;			/* return value of getopt() */
	int i;			/* loop iterator */
	llist_t *announce_last = NULL;
	slist_t *web_seed_last = NULL;
	int64_t piece_len_maxes[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		(int64_t) BIT15MAX * ONEMEG, (int64_t) BIT16MAX * ONEMEG,
		(int64_t) BIT17MAX * ONEMEG, (int64_t) BIT18MAX * ONEMEG,
		(int64_t) BIT19MAX * ONEMEG, (int64_t) BIT20MAX * ONEMEG,
		(int64_t) BIT21MAX * ONEMEG, (int64_t) BIT22MAX * ONEMEG
	};
	int num_piece_len_maxes = sizeof(piece_len_maxes) /
	    sizeof(piece_len_maxes[0]);
#ifdef USE_LONG_OPTIONS
	/* the option structure to pass to getopt_long() */
	static struct option long_options[] = {
		{"announce", 1, NULL, 'a'},
		{"comment", 1, NULL, 'c'},
		{"no-date", 0, NULL, 'd'},
		{"extra", 1, NULL, 'e'},
		{"force", 0, NULL, 'f'},
		{"help", 0, NULL, 'h'},
		{"piece-length", 1, NULL, 'l'},
		{"name", 1, NULL, 'n'},
		{"output", 1, NULL, 'o'},
		{"private", 0, NULL, 'p'},
#ifdef USE_PTHREADS
		{"threads", 1, NULL, 't'},
#endif
		{"verbose", 0, NULL, 'v'},
		{"web-seed", 1, NULL, 'w'},
		{NULL, 0, NULL, 0}
	};
#endif
#ifdef DEBUG
	int64_t pieces;
#endif				/* DEBUG */

	/* now parse the command line options given */
#ifdef USE_PTHREADS
#define OPT_STRING "a:c:de:fhl:n:o:pt:vw:"
#else
#define OPT_STRING "a:c:de:fhl:n:o:pvw:"
#endif
#ifdef USE_LONG_OPTIONS
	while ((c = getopt_long(argc, argv, OPT_STRING,
				long_options, NULL)) != -1) {
#else
	while ((c = getopt(argc, argv, OPT_STRING)) != -1) {
#endif
#undef OPT_STRING
		switch (c) {
		case 'a':
			if (announce_last == NULL) {
				m->announce_list = announce_last =
					malloc(sizeof(llist_t));
			} else {
				announce_last->next =
					malloc(sizeof(llist_t));
				announce_last = announce_last->next;

			}
			if (announce_last == NULL) {
				fprintf(stderr, "Out of memory.\n");
				exit(EXIT_FAILURE);
			}
			announce_last->l = get_slist(optarg);
			break;
		case 'c':
			m->comment = optarg;
			break;
		case 'd':
			m->no_creation_date = 1;
			break;
		case 'e':
			add_extra(m, optarg);
			break;
		case 'f':
			m->force = 1;
			break;
		case 'h':
			print_help();
			exit(EXIT_SUCCESS);
		case 'l':
			m->piece_length = atoi(optarg);
			if (m->piece_length < 15 || m->piece_length > 28) {
				fprintf(stderr, PROGRAM
					": Invalid piece length %s\n",
					optarg);
				fprintf(stderr, "The piece length must be"
					" a number between 15 and 28.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'n':
			m->torrent_name = optarg;
			break;
		case 'o':
			m->metainfo_file_path = optarg;
			break;
		case 'p':
			m->private = 1;
			break;
#ifdef USE_PTHREADS
		case 't':
			m->threads = atoi(optarg);
			break;
#endif
		case 'v':
			m->verbose = 1;
			break;
		case 'w':
			if (web_seed_last == NULL) {
				m->web_seed_list = web_seed_last =
					get_slist(optarg);
			} else {
				web_seed_last->next =
					get_slist(optarg);
				web_seed_last = web_seed_last->next;
			}
			while (web_seed_last->next)
				web_seed_last = web_seed_last->next;
			break;
		case '?':
			fprintf(stderr, "Use -h for help.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* user must specify at least one announce URL as it wouldn't make
	   any sense to have a default for this.
	   it is ok not to have any unless torrent is private. */
	if (m->announce_list == NULL && m->private == 1) {
		fprintf(stderr, "Must specify an announce URL. "
			"Use -h for help.\n");
		exit(EXIT_FAILURE);
	}
	if (announce_last != NULL)
		announce_last->next = NULL;

	/* ..and a file or directory from which to create the torrent */
	if (optind >= argc) {
		fprintf(stderr, "Must specify the contents, "
			"use -h for help\n");
		exit(EXIT_FAILURE);
	}

#ifdef USE_PTHREADS
	/* check the number of threads */
	if (m->threads) {
		if (m->threads > 20) {
			fprintf(stderr, "The number of threads is limited to "
			                "at most 20\n");
			exit(EXIT_FAILURE);
		}
	} else {
#ifdef _SC_NPROCESSORS_ONLN
		m->threads = sysconf(_SC_NPROCESSORS_ONLN);
		if (m->threads == -1)
#endif
			m->threads = 2; /* some sane default */
	}
#endif

	/* strip ending DIRSEP's from target */
	strip_ending_dirseps(argv[optind]);

	/* if the torrent name isn't set use the basename of the target */
	if (m->torrent_name == NULL)
		m->torrent_name = basename(argv[optind]);

	/* make sure m->metainfo_file_path is the absolute path to the file */
	set_absolute_file_path(m);

	/* if we should be verbose print out all the options
	   as we have set them */
	if (m->verbose)
		dump_options(m);

	/* check if target is a directory or just a single file */
	m->target_is_directory = is_dir(m, argv[optind]);
	if (m->target_is_directory) {
		/* change to the specified directory */
		if (chdir(argv[optind])) {
			fprintf(stderr, "Error changing directory to '%s': %s\n",
					argv[optind], strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (file_tree_walk("." DIRSEP, MAX_OPENFD, process_node, m))
			exit(EXIT_FAILURE);
	}

	/* determine the piece length based on the torrent size if
	   it was not user specified. */
	if (m->piece_length == 0) {
		for (i = 15; i < num_piece_len_maxes &&
		     m->piece_length == 0; i++)
			if (m->size <= piece_len_maxes[i])
				m->piece_length = i;
		if (m->piece_length == 0)
			m->piece_length = i;
	}
	/* convert the piece length from power of 2 to an integer. */
	m->piece_length = 1 << m->piece_length;

	/* calculate the number of pieces
	   pieces = ceil( size / piece_length ) */
#ifdef DEBUG
	pieces = m->size + m->piece_length - 1;
	fprintf(stderr, PROGRAM ": size + pl - 1 = %" PRId64 "\n", pieces);
#endif				/* DEBUG */
	m->pieces = (m->size + m->piece_length - 1) / m->piece_length;

	/* now print the size and piece count if we should be verbose */
	if (m->verbose)
		printf("\n%" PRId64 " bytes in all.\n"
			"That's %u pieces of %u bytes each.\n\n",
			m->size, m->pieces, m->piece_length);
}
