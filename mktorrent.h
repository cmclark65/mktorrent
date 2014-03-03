#ifndef _MKTORRENT_H
#define _MKTORRENT_H

#ifdef _WIN32
#define DIRSEP      "\\"
#else
#define DIRSEP      "/"
#endif

/* name of the program */
#define PROGRAM		"mktorrent"

/* number of bytes in one MB */
#define ONEMEG		1048576

/* max torrent size in MB for a given piece length in bits */
/* where an X bit piece length equals a 2^X byte piece size */
#define	BIT23MAX	12800
#define	BIT22MAX	6400
#define	BIT21MAX	3200
#define	BIT20MAX	1600
#define	BIT19MAX	800
#define	BIT18MAX	400
#define	BIT17MAX	200
#define	BIT16MAX	100
#define	BIT15MAX	50

/* string list */
struct slist_s;
typedef struct slist_s slist_t;
struct slist_s {
	char *s;
	slist_t *next;
};

/* list of string lists */
struct llist_s;
typedef struct llist_s llist_t;
struct llist_s {
	slist_t *l;
	llist_t *next;
};

/* file list */
struct flist_s;
typedef struct flist_s flist_t;
struct flist_s {
	char *path;
	off_t size;
	flist_t *next;
};

/* extra fields list */
struct elist_s;
typedef struct elist_s elist_t;
struct elist_s {
	char *key;
	char *value;
	elist_t *next;
};

typedef struct {
	/* options */
	unsigned int piece_length; /* piece length */
	llist_t *announce_list;    /* announce URLs */
	char *comment;             /* optional comment */
	elist_t *extra;            /* optional extra fields some private trackers use */
	const char *torrent_name;  /* name of torrent (name of directory) */
	char *metainfo_file_path;  /* absolute path to the metainfo file */
	slist_t *web_seed_list;    /* web seed URLs */
	int target_is_directory;   /* target is a directory */
	int no_creation_date;      /* don't write the creation date */
	int private;               /* set the private flag */
	int verbose;               /* be verbose */
	int force;                 /* overwrite metainfo file */
#ifdef USE_PTHREADS
	long threads;              /* number of threads used for hashing */
#endif

	/* information calculated by read_dir() */
	int64_t size;              /* combined size of all files */
	flist_t *file_list;        /* list of files and their sizes */
	unsigned int pieces;       /* number of pieces */
} metafile_t;

#endif /* _MKTORRENT_H */
