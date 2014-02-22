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
#include <sys/types.h>    /* off_t */
#include <stdio.h>        /* printf() etc. */
#include <string.h>       /* strlen() etc. */
#include <time.h>         /* time() */
#include <stdlib.h>       /* exit() */
#include <ctype.h>        /* isdigit() */

#ifdef USE_OPENSSL
#include <openssl/sha.h>  /* SHA_DIGEST_LENGTH */
#else
#include <inttypes.h>
#include "sha1.h"
#endif

#include "mktorrent.h"

#define EXPORT
#endif /* ALLINONE */

/*
 * write announce list
 */
static void write_announce_list(FILE *f, llist_t *list)
{
	/* the announce list is a list of lists of urls */
	fprintf(f, "13:announce-listl");
	/* go through them all.. */
	for (; list; list = list->next) {
		slist_t *l;

		/* .. and print the lists */
		fprintf(f, "l");
		for (l = list->l; l; l = l->next)
			fprintf(f, "%lu:%s",
					(unsigned long)strlen(l->s), l->s);
		fprintf(f, "e");
	}
	fprintf(f, "e");
}

/*
 * write file list
 */
static void write_file_list(FILE *f, flist_t *list)
{
	char *a, *b;

	fprintf(f, "5:filesl");

	/* go through all the files */
	for (; list; list = list->next) {
		/* the file list contains a dictionary for every file
		   with entries for the length and path
		   write the length first */
		fprintf(f, "d6:lengthi%" PRIoff "e4:pathl", list->size);
		/* the file path is written as a list of subdirectories
		   and the last entry is the filename
		   sorry this code is even uglier than the rest */
		a = list->path;
		/* while there are subdirectories before the filename.. */
		while ((b = strchr(a, DIRSEP[0])) != NULL) {
			/* set the next DIRSEP[0] to '\0' so fprintf
			   will only write the first subdirectory name */
			*b = '\0';
			/* print it bencoded */
			fprintf(f, "%lu:%s", (unsigned long)strlen(a), a);
			/* undo our alteration to the string */
			*b = DIRSEP[0];
			/* and move a to the beginning of the next
			   subdir or filename */
			a = b + 1;
		}
		/* now print the filename bencoded and end the
		   path name list and file dictionary */
		fprintf(f, "%lu:%see", (unsigned long)strlen(a), a);
	}

	/* whew, now end the file list */
	fprintf(f, "e");
}

/*
 * write web seed list
 */
static void write_web_seed_list(FILE *f, slist_t *list)
{
	/* print the entry and start the list */
	fprintf(f, "8:url-listl");
	/* go through the list and write each URL */
	for (; list; list = list->next)
		fprintf(f, "%lu:%s", (unsigned long)strlen(list->s), list->s);
	/* end the list */
	fprintf(f, "e");
}

/*
 * test for bencoded integer
 *
 * bencoded integer starts with 'i', ends with 'e', and between must be a
 * base ten number. it may start with a minus sign. it may be zero. it may
 * not be zero padded.
 */
int is_bencode_int(char *s)
{
	char *d;

	d = s;
	/* if the first char is not 'i', by far the most common case, s
	 * isn't a bencode int. if it is the null char, it isn't a bencode
	 * int. */
	if (*s != 'i')
		return 0;
	if (*s == '\0')
		return 0;
	/* move d to the final char before the null. if d doesn't now point
	 * to an 'e' it isn't a bencode int. */
	while (d[1] != '\0')
		d++;
	if (*d != 'e')
		return 0;
	/* if there isn't at least one char between the 'i' and the 'e', it
	 * isn't a bencode int. */
	s++;
	if (s == d)
		return 0;
	/* the first char must have special tests to allow a leading minus,
	 * and to test for zero but not zero padded. if the leading minus
	 * is present, there must be at least one more char and the next
	 * char must be a non-zero digit. */
	if (*s == '-') {
		s++;
		if (s == d)
			return 0;
		if (*s == '0')
			return 0;
		if (isdigit(*s))
			s++;
		else
			return 0;
	} else if (*s == '0') {
		/* if the first char is 0 it must be the only character. */
		s++;
		return (s == d);
	} else if (isdigit(*s)) {
		/* any other digit is a legal first character */
		s++;
	}
	/* if not returned by this point, there is a starting digit or a
	 * minus followed by a starting digit. s points to the character
	 * following that digit. zero or more digits between s and d will
	 * be a legal bencoded int, any non-digits will make it invalid. */
	while (s < d) {
		if (isdigit(*s))
			s++;
		else
			return 0;
	}
	return 1;
}

/*
 * write extra fields
 *
 * traverse the list writing the extra key and value from each node as long
 * as they sort as less than the reference key. return the first node in
 * the list not written. if the reference key is NULL, write all the
 * remaining nodes in the list.
 */
static elist_t *write_extra(FILE *f, elist_t *list, char *refkey)
{
	while ((list != NULL) &&
	       (refkey == NULL || strcmp(list->key, refkey) < 0)) {
		/* if the key and value strings are not null, print the key
		 * as a bencoded string. */
		if (list->key && list->value)
			fprintf(f, "%d:%s", (int) strlen(list->key),
				list->key);
		else {
			/* something is very broken if this happens */
			fprintf(stderr, PROGRAM
				": internal failure code extra\n");
			exit(EXIT_FAILURE);
		}
		/* if it is a bencode integer, write it. else write it as a
		 * bencoded string. */
		if (is_bencode_int(list->value))
			fprintf(f, "%s", list->value);
		else
			fprintf(f, "%d:%s", (int) strlen(list->value),
				list->value);
		list = list->next;
	}
	return list;
}

/*
 * write metainfo to the file stream using all the information
 * we've gathered so far and the hash string calculated
 */
EXPORT void write_metainfo(FILE *f, metafile_t *m, unsigned char *hash_string)
{
	elist_t *extra_list = m->extra;

	/* let the user know we've started writing the metainfo file */
	printf("Writing metainfo file... ");
	fflush(stdout);

	/* every metainfo file is one big dictonary */
	fprintf(f, "d");

	if (m->announce_list != NULL) {
		/* write the announce URL */
		fprintf(f, "8:announce%lu:%s",
			(unsigned long)strlen(m->announce_list->l->s),
			m->announce_list->l->s);
		/* write the announce-list entry if we have
		   more than one announce URL */
		if (m->announce_list->next || m->announce_list->l->next)
			write_announce_list(f, m->announce_list);
	}

	/* add the comment if one is specified */
	if (m->comment != NULL)
		fprintf(f, "7:comment%lu:%s",
				(unsigned long)strlen(m->comment),
				m->comment);
	/* I made this! */
	fprintf(f, "10:created by%lu:%s %s",
		(unsigned long) strlen(VERSION) + strlen(PROGRAM) + 1,
		PROGRAM, VERSION);
	/* add the creation date */
	if (!m->no_creation_date)
		fprintf(f, "13:creation datei%lde",
			(long)time(NULL));

	/* now here comes the info section; it is yet another dictionary.
	   the entries in a dictionary must be written in order sorted by
	   the keys. Before writing each key, there is an attempt to write
	   any user defined extra entries which might need to be written
	   first. */
	fprintf(f, "4:infod");
	/* first entry is either 'files', which specifies a list of files
	   and their respective sizes for a directory torrent, or 'length',
	   which specifies the length of a single file torrent */
	if (m->target_is_directory) {
		if (extra_list)
			extra_list = write_extra(f, extra_list, "files");
		write_file_list(f, m->file_list);
	} else {
		if (extra_list)
			extra_list = write_extra(f, extra_list, "length");
		fprintf(f, "6:lengthi%" PRIoff "e", m->file_list->size);
	}

	/* the info section also contains the name of the torrent,
	   the piece length and the hash string */
	if (extra_list)
		extra_list = write_extra(f, extra_list, "name");
	fprintf(f, "4:name%lu:%s",
		(unsigned long) strlen(m->torrent_name), m->torrent_name);
	if (extra_list)
		extra_list = write_extra(f, extra_list, "piece length");
	fprintf(f, "12:piece lengthi%ue", m->piece_length);
	if (extra_list)
		extra_list = write_extra(f, extra_list, "pieces");
	fprintf(f, "6:pieces%u:", m->pieces * SHA_DIGEST_LENGTH);
	fwrite(hash_string, 1, m->pieces * SHA_DIGEST_LENGTH, f);

	/* set the private flag */
	if (m->private) {
		if (extra_list)
			extra_list = write_extra(f, extra_list, "private");
		fprintf(f, "7:privatei1e");
	}

	/* add any remaining extra fields. */
	if (extra_list)
		extra_list = write_extra(f, extra_list, NULL);

	/* end the info section */
	fprintf(f, "e");

	/* add url-list if one is specified */
	if (m->web_seed_list != NULL) {
		if (m->web_seed_list->next == NULL)
			fprintf(f, "8:url-list%lu:%s",
					(unsigned long)strlen(m->web_seed_list->s),
					m->web_seed_list->s);
		else
			write_web_seed_list(f, m->web_seed_list);
	}

	/* end the root dictionary */
	fprintf(f, "e");

	/* let the user know we're done already */
	printf("done.\n");
	fflush(stdout);
}
