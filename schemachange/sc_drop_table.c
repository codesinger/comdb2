/*
   Copyright 2015 Bloomberg Finance L.P.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include <memory_sync.h>
#include <autoanalyze.h>
#include <translistener.h>

#include "schemachange.h"
#include "sc_drop_table.h"
#include "sc_schema.h"
#include "sc_global.h"
#include "sc_callbacks.h"
#include "sc_util.h"
#include "views.h"

static int delete_table(struct dbtable *db, tran_type *tran)
{
    remove_constraint_pointers(db);

    int rc, bdberr;
    if ((rc = bdb_close_only_sc(db->handle, tran, &bdberr))) {
        fprintf(stderr, "bdb_close_only rc %d bdberr %d\n", rc, bdberr);
        return -1;
    }

    char *table = db->tablename;
    delete_db(table);
    MEMORY_SYNC;
    delete_schema(table);
    bdb_del_table_csonparameters(tran, table);
    return 0;
}

int do_drop_history(struct ireq *iq, tran_type *tran)
{
    struct schema_change_type *s = iq->sc;
    struct dbtable *db = s->db;
    struct schema_change_type *scopy = new_schemachange_type();
    if (init_history_sc(s, db, scopy)) {
        reqerrstr(iq, ERR_SC, "History table name too long");
        return SC_CSC2_ERROR;
    }
    scopy->same_schema = 1;
    scopy->kind = SC_DROPTABLE;
    /* scopy->fastinit = 1; */
    if (get_csc2_file(scopy->tablename, -1, &scopy->newcsc2, NULL)) {
        reqerrstr(iq, ERR_SC, "History table %s schema not found",
                  scopy->tablename);
        sc_errf(s, "History table %s schema not found\n", scopy->tablename);
        free_schema_change_type(scopy);
        return SC_CSC2_ERROR;
    }

    iq->sc = scopy;
    s->history_rc = do_drop_table(iq, s, tran);
    iq->sc = s;
    if (s->history_rc != SC_OK && s->history_rc != SC_COMMIT_PENDING) {
        reqerrstr(iq, ERR_SC, "Failed to delete history table");
        sc_errf(s, "error deleting history table\n");
        logmsg(LOGMSG_ERROR, "%s failed with rc %d\n", __func__, s->history_rc);
        return s->history_rc;
    }

    sc_printf(s, "Drop history table %s ok\n", scopy->tablename);
    return SC_OK;
}

int do_drop_table(struct ireq *iq, struct schema_change_type *s,
                  tran_type *tran)
{
    struct dbtable *db;
    iq->usedb = db = s->db = get_dbtable_by_name(s->tablename);
    if (db == NULL) {
        sc_client_error(s, "Table doesn't exist");
        return SC_TABLE_DOESNOT_EXIST;
    }

    if ((!iq || iq->tranddl <= 1) && db->n_rev_constraints > 0 &&
        !self_referenced_only(db)) {
        sc_client_error(s, "Can't drop a table referenced by a foreign key");
        return -1;
    }
    if ((s->kind != SC_DROPTABLE) && (db->is_history_table || db->history_db)) {
        sc_errf(s, "Fastinit not supported for temporal tables\n");
        reqerrstr(iq, ERR_SC, "Fastinit not supported for temporal tables");
        return -1;
    }
    if (!s->is_history && db->is_history_table) {
        sc_errf(s, "Temporal history tables must be dropped with base table\n");
        reqerrstr(iq, ERR_SC,
                  "Temporal history tables must be dropped with base tables");
        return -1;
    }

    if (s->is_history == 0 && s->db && s->db->periods[PERIOD_SYSTEM].enable) {
        s->drop_history = 1;
        return do_drop_history(iq, tran);
    }

    return SC_OK;
}

// NB: this gets called from drop table and from fastinit
int finalize_drop_table(struct ireq *iq, struct schema_change_type *s,
                        tran_type *tran)
{
    struct dbtable *db = s->db;
    int rc = 0;
    int bdberr = 0;

    if (db->n_rev_constraints > 0 && !self_referenced_only(db)) {
        sc_client_error(s, "Can't drop a table referenced by a foreign key");
        return ERR_SC;
    }

    /* Before this handle is closed, lets wait for all the db reads to finish */
    if ((rc = bdb_lock_tablename_write(db->handle, "comdb2_tables", tran)) != 0) {
        sc_errf(s, "%s: failed to lock comdb2_tables rc: %d\n", __func__, rc);
        return rc;
    }

    if ((rc = bdb_lock_table_write(db->handle, tran)) != 0) {
        sc_errf(s, "%s: failed to lock table rc: %d\n", __func__, rc);
        return rc;
    }

    /* at this point if a backup is going on, it will be bad */
    gbl_sc_commit_count++;

    s->already_finalized = 1;

    if ((rc = delete_table_sequences(tran, db))) {
        sc_errf(s, "Failed deleting table sequences rc %d\n", rc);
        return rc;
    }

    delete_table(db, tran);
    /*Now that we don't have any data, please clear unwanted schemas.*/
    bdberr = bdb_reset_csc2_version(tran, db->tablename, db->schema_version, 0);
    if (bdberr != BDBERR_NOERROR) return -1;

    if ((rc = bdb_del_file_versions(db->handle, tran, &bdberr))) {
        sc_errf(s, "%s: bdb_del_file_versions failed with rc: %d bdberr: "
                   "%d\n",
                __func__, rc, bdberr);
        return rc;
    }

    if ((rc = table_version_upsert(db, tran, &bdberr)) != 0) {
        sc_errf(s, "Failed updating table version bdberr %d\n", bdberr);
        return rc;
    }

    if ((rc = llmeta_set_tables(tran, thedb)) != 0) {
        sc_errf(s, "Failed to set table names in low level meta\n");
        return rc;
    }

    /* Delete all access permissions related to this table. */
    if ((rc = bdb_del_all_table_access(db->handle, tran, db->tablename)) != 0)
    {
        sc_errf(s, "Failed to delete access permissions\n");
        return rc;
    }

    /* if this is a shard, remove the llmeta partition entry */
    if (s->partition.type == PARTITION_REMOVE && s->publish) {
        if (!db->timepartition_name) {
            sc_errf(s,
                    "Drop partitioned table failed, shard %s not registered\n",
                    db->tablename);
            return SC_INTERNAL_ERROR;
        }
        struct errstat err = {0};
        rc = partition_llmeta_delete(tran, s->timepartition_name, &err);
        if (rc) {
            sc_errf(s, "Failed to remove partition llmeta %d\n", err.errval);
            return SC_INTERNAL_ERROR;
        }
    }

    if (s->finalize) {
        if (create_sqlmaster_records(tran)) {
            sc_errf(s, "create_sqlmaster_records failed\n");
            return -1;
        }
        create_sqlite_master();
    }

    live_sc_off(db);

    if (!gbl_create_mode) {
        logmsg(LOGMSG_INFO, "Table %s is at version: %lld\n", db->tablename,
               db->tableversion);
    }

    if (gbl_replicate_local)
        local_replicant_write_clear(iq, tran, db);

    return 0;
}
