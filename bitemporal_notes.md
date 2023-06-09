# Major changes

`struct db` has become `struct dbtable`.


`schema_change_type` has some minor differences (`table` has become `tablename`).
But the major difference seems to be replacing a bunch of logically boolean
attributes (`addonly`, `alteronly`, etc) wwith the field `kind` that holds an
enumerated type (`SC_ADDTABLE`, `SC_DROP_VIEW`, etc.)

The pull request adds more logically boolean attributes to `schema_change_type` such
as `is_history`, `add_history`, `drop_history`, and `alter_history`.  Might it be
better to add these as enumerated types in `kind`?




# Queries

## Strange coding style

The pull request introduces some new external functions.  Instead of putting the
function declaration in a header file and including the header file, the code
manually declares the function just before use, as in...

```
    if (data->iq.usedb->overwrite_systime) {
        int temporal_overwrite_systime(struct ireq * iq, uint8_t * rec,
                                       int use_tstart);
        rc = temporal_overwrite_systime(&(data->iq), p_buf_data, 0);
        if (rc) {
            sc_errf(data->s, "temporal_overwrite_systime table %s failed\n",
                    data->iq.usedb->dbname);
            return -2;
        }
    }
```

Would it not be better to put the declaration of `temporal_overwrite_systime` in a
header file that was included?


## csc2/maccparse.y

Note that named constraint (introduced in r7), so there is an extra instance of
`start_constraint_list()` that did not exist when the pull request was created.

I have a feeling that a named "no overlap" constraint will not work.


## db/constraints.c

There is some code for checkong "no overlap" constraints that the pull request added
to the function `verify_record_constraint()` in `schemachange/sc_schema.c`.  That
code has been moved to the function `check_single_key_constraint()` in
`db/constraints.c`.

The original `verify_record_constraint()` in `schemachange/sc_schema.c` was passed
an argument of `struct db *db`.  The new function `check_single_key_constraint()` in
`db/constraints.c` is not passed a pointer to `struct dbtable`.  I added code to
extract a pointer to `struct dtable` from `constraint_t->lcltable`.  This seems
dangerous.

On looking at this again, the code in the pull request does not fit here.  But it
has to go somewhere.  Look for the line

```
if (ct->flags & CT_NO_OVERLAP) {
```

in the pull request version of `verify_record_constraint()` in
`schemachange/sc_schema.c`.


## db/osqlcomm.c

In the function `osql_process_packet()`, this case statement and all of its
associated code

```
    case OSQL_SCHEMACHANGE: {
```

has been replaced with

```
    case OSQL_SCHEMACHANGE: {
        /* handled in osql_process_schemachange */
        return 0;
    } break;
```

`osql_process_schemachange()` is a function in osqlcomm.c, but its code is quite
different.  I have no idea if the change that was put into `case OSQL_SCHEMACHANGE:`
five years ago is at all relevant.

The function `osql_log_packet()` no longer exists.  THis pull request has made
changes to it.

In the original code, a pointer to osql_log_packet was passed to the function
`process_this_session()` in `osqlblockproc.c`.

In the current version of the code, a pointer to the function
`osql_process_packet()` is passed to the function
`process_this_session()` in `osqlblockproc.c`.

The pull request adds some temporal code to `osql_log_packet()`.  No longer needed?

The pull request also adds some temporal code to `osql_process_packet()`.   Those
changes have been applied.


## db/record.c

The temporal code that inserts history records calls `add_record()` without the
final parameter, which has been added since the pull request was created.

I looked to see what other callers provided to `add_record()` as the final
parameter.  In every case I found, the final parater was zero except for this call
in osqlcomm.c, where the final argument is

```
dt.upsert_flags); /* do I need this?*/
```

## schemachange/sc_callbacks.c

This file has been extensively restructured since the pull request.  I do not know
how to merge its changes.

## schemachange/sc_util.c

The pull request adds two new functions that deal with data types in generating csc2
files.

Since the pull request was originally created, two new types have been added:
CLIENT_SEQUENCE, CLIENT_FUNCTION, SERVER_SEQUENCE, SERVER_FUNCTION.

Not sure how to modify these functions for the new types.

## schemchange/schemachange.h

The old version of `schema_change_type` had a field called `type`, which at one
point in the pull request is set as follows: `scopy->type = DBTYPE_TAGGED_TABLE;`

What, if anything, do I replace this with?

## schema_change_type

The old pull request used a version of `schema_change_type` that had ints to specify
things such as `addonly`, `alteronly`, etc.

I have replaced `addonly = 1` with `kind = SC_ADDTABLE` and
`alteronly = 1` with `kind = SC_ALTERTABLE`.  Is this right?

## schemachange/sc_logic.c

In the pull request version, `do_alter_table()` was located in this file.  In the
current version, `do_alter_table()` has been moved to `sc_alter_table.c`.

It is not clear if the temporal changes to the `do_alter_table()` in
`schemachange/sc_logic.c` apply to the `do_alter_table()` in `sc_alter_table.c`.

The pull request makes changes to the function `backout_schema_change()` in
`sc_logic.`.  I have not applied those changes to the current version of
`backout_schema_change()`.  Function seems to be fundamentaly different.

## schemachange/sc_schema.c

There is a line of code that the pull request adds to line 1241 of this file

```
newdb->n_rev_cascade_systime = db->n_rev_cascade_systime;
```

This line of code does not seem to fit in the new version of this file.

Same thing with the lines 1264 - 1266 in the pull request.  Look for the line

```
if ((ct->flags & (CT_UPD_CASCADE | CT_DEL_CASCADE)) != 0 &&
```

In fact, none of the changes in the pull request for the function
`restore_constraint_pointers_main()` seem to fit the new version of this function.

None of the changes to the function `remove_constraint_pointers()` in the pull
request could be migrated to the new code.  Too different.  These are the changes in
lines 1449 - 1451 in the pull request in `sc_schema.c`


## schemachange/sc_struct.c

The new function `deep_copy_schemachange_type()` added by the pull request looks
problematic.

For example, it simply copies from one `schema_change_type` to another the contents
of `newcsc2`.  Should it not malloc a new one for the copy?  It appears that the
length of the buffer is contained in `newcsc2_len`.


## sqlite/src/build.c

The pull request adds another argument (`pSelect`) to the function
`sqlite3SrcListAssignCursors()`.  But the extra argument is never referenced in the
function.

The function signature was also changed from returning `void` to returning `int`.

It recursively calls itself from inside of a loop and breaks out of the loop if the
return code is non-zero.  But the function never returns
anything other than zero.  So what is the point of returning `int`?



# Comments

## bdb/bdb_fetch.h

`bdb_fetch_next_genid` is deleted.  Only called from `db/glue.c`.

`bdb_fetch_next_nodta_genid` is deleted.  Not called by anyone.

`bdb_fetch_next_nodta_genid_tran` is added.  Called from new version of `db/glue.c`.

`bdb_fetch_prev_genid` is deleted.  Only called from `db/glue.c`

`bdb_fetch_prev_nodta_genid` is deleted.  Only called from `db/glue.c`

`bdb_fetch_prev_nodta_genid_tran` is added.  Called from new version of `db/glue.c`.

Does not look as though `bdb/bdb_fetch.h` has changed much since the pull request
was generated.  The struct `bdb_fetch_args_t` has been expanded to include new data
members.


## bdb/fetch.c

Unlike `bdb/bdb_fetch.h`, this file has had a number of changes but does not seem to
have changed in spirit.


## comdb2/cdb2api.c

Add code to `process_set_command` in `comdb2/cdb2api.c` to process the `set temporal
business time` and `set temporal system_time` commands.

This file is 36% bigger than it was five years ago.  But the change still looks
valid.  Three extra lines of code and a change to a comment.


## csc/dynschemaload.h

Declare function prototype for `dns_get_period()`

No major changes to this small file in the past five years.

This appears to be one of the header files of `csc/macc_so.c`.  The other is
`csc2/macc.h`.


## csc2/macc.h

Change `enum ct_flags { CT_UPD_CASCADE = 0x00000001, CT_DEL_CASCADE = 0x00000002 };`
to

```
enum ct_flags {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_NO_OVERLAP = 0x00000004
};
```

But since this pull request, `ct_flags` has been changed to

```
enum ct_flags {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_DEL_SETNULL = 0x00000008,
};
```

Should be able to merge the two.

Also add

```
enum pd_flags { PERIOD_SYSTEM = 0, PERIOD_BUSINESS = 1, PERIOD_MAX = 2 };

extern struct period {
    int enable;
    int start;
    int end;
} periods[PERIOD_MAX];

extern int nperiods;
```


## csc/macc_so.c

`struct table tables[MAXTBLS];` has moved from `csc2/maccglobals.c` to `csc2/macc.h`

References to `tables` have changed to `macc_globals->tables`.  But some functions
have left the unqualified reference by adding

`struct table *tables = macc_globals->tables;`

near the top of the function.

Similar change for `constraints`.


The pull request adds some stuff for dealing with temporal periods.  It addresses
`tables` and `constraints` the old way.  Code should be good if that is fixed.


Function `start_constraint_list()` has an extra argument to indicate "no overlap"
constraint exists.

Oddly enough, I can find no caller of the function `start_constraint_list()` in the
current main branch or in the pull request branch.

Actually, it is referenced from `csc2/macparse.y`

`fprintf(stderr, "Record \"%s\" has ARRAY fields. SQL does not "`
has been changed to
`csc1_error("Record \"%s\" has ARRAY fields. SQL does not "`

Seems a plausible thing to do.

## csc2/maccglobals.c

Most of what used to be global stuff in this file has been moved to `struct
macc_globals_t` defined in `csc2/macc.h`.  The function `dyns_init_globals()`
allocatyes an instance of `struct macc_globals_t` and initializes it.

The pull request adds two external variables to this file and initializes them.
These constants should be added to `struct macc_globals_t` and initialised in
`dyns_init_globals()`.


## csc2/macclex.l

Add "periods" and "no overlap" constraints

Specifically add

```
<INITIAL>periods                { return T_PERIODS; }
```

immediately after

```
<RECTYPE>ondisk                 { return T_ONDISK; }
```

Add

```
<CONSTRAINTS>"no_overlap"       { return T_CON_NOOVERLAP; }
```

immediately after

```
<CONSTRAINTS>delete             { return T_CON_DELETE; }
```

Both the main branch and the pull request branch have tabs in this file.  Should
they be preserved?


## csc2/maccparse.y

Add "periods" and "no overlap" constraints

`cnstrtbllist:` in the pull request has been changed to `cnstrtparentlist:` in the
main branch and I have never done anything with a yacc file.  Need help on this one.

Add extra argument to all instances of `start_constraint_list()`.  See description
above in `csc/macc_so.c`

Note that named constraint (introduced in r7), so there is an extra instance of
`start_constraint_list()` that did not exist when the pull request was created.

I have a feeling that a named "no overlap" constraint will not work.


## db/comdb2.c

This file is 30% bigger in the pull request than it is now in the main branch.

There is a change to the function `newdb_from_schema()` which is not in the new
version of this file.  There is a similar function in `db/macc_glue.c`.  The old
function returns a pointer to `struct db`.  The new version returns a pointer to `dtable`.

`struct db` in the pull request is defined in `db/comdb2.h`.  It appears that
`struct dtable` is simply the new name for `struct db`.

This line of code in `init_reverse_constraints()` in `db/comdb2`

```
   db->n_rev_constraints = 0;
```

should have this line added after:

```
    db->n_rev_cascade_systime = 0;
```

This line of code in `db/comdb2.c`

```
    init_file_locations(lrlname);
```

should have the following line added after

```
    set_datetime_dir();
```

The pull request moved the function `set_datetime_dir()` to an earlier place in the
file.  Not sure why.


## db/comdb2.h

In the pull request, `MAX_OSQL_TYPES` is defined in this file.  It is now defined in
`db/osqlrpltypes.h`.  Its value is 29.

In the pull request, `MAX_OSQL_TYPES` is changed from 27 to 28 and a new enum value,
`OSQL_TIMESPEC = 27` is added.

Need to add in `db/osqlrpltypes.h`, `OSQL_TIMESPEC` with a value of 29 and change
`MAX_OSQL_TYPES` to 30.

In the pull request in `db/comdb2.h` in the enum `DB_METADATA`, a new enum called
`META_START_TIME` is added.  Add it here with a new value.

In the pull request in `db/comdb2.h`, `enum CONSTRAINT_FLAGS` is changed from

```
enum CONSTRAINT_FLAGS {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_BLD_SKIP = 0x00000004
};
```

to

```
enum CONSTRAINT_FLAGS {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_NO_OVERLAP = 0x00000004,
    CT_BLD_SKIP = 0x00000008
};
```

But in the current main branch, `enum CONSTRAINT_FLAGS` is defined as

```
enum CONSTRAINT_FLAGS {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_BLD_SKIP    = 0x00000004,
    CT_DEL_SETNULL = 0x00000008,
```

in `csc2/macc.h`, one also finds the following enum

```
enum ct_flags {
    CT_UPD_CASCADE = 0x00000001,
    CT_DEL_CASCADE = 0x00000002,
    CT_DEL_SETNULL = 0x00000008,
};
```

At least one of fields referenced with `CT_DEL_SETNULL` is an `int`.  If they are
all `int`, then we can add `CT_NO_OVERLAP` as `0x0000000C`.

This is added to `struct dbtable` right above "Foreign key constraints" at line 666.


```
    /* temporal periods */
    struct timespec tstart;
    int is_history_table;
    period_t periods[PERIOD_MAX];
    struct db *history_db;
    struct db *orig_db;
    int overwrite_systime;
```

Add this line

```
    int n_rev_cascade_systime;
```

right after the line

```
    size_t n_rev_constraints;
```

at line 668.

Not sure if the data type of `n_rev_cascade_systime` should continue to be `int`.
No.  It is used as a counter.  It should be `size_t`.

In `struct irec`, add the folowing:

```

    /* temporal table */
    struct timespec tstart;
```

Right after the line

```
    int *osql_step_ix;
```

at line 1371.

Funcion signatures of `ix_next_trans()` and `ix_prev_trans()` appear to have changed
as have the implementations in `db/glue.c`.


## db/constraints.c

Lots of code added to handle "no overlap" constraint.  Lots of the details of this
file have changed.  Need to find a way to integrate the "no overlap" constraint code
from the pull request.

## db/glue.c

No big changes since the pull reuest, but dozens if not hundreds of small ones.
Need to find a way to integrate the temporal changes.


## db/osqlcomm.c

No big changes since the pull reuest, but dozens if not hundreds of small ones.
Need to find a way to integrate the temporal changes.

Fortunately, most of the temporal changes involve adding new code.


## db/osqlcomm.h

Add declaration of new function prototype.

## db/osqlshadtbl.c
