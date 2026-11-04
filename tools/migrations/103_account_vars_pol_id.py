import mariadb

# account_vars (phoenix module) is keyed by the numeric POL ID now, which does not fit 32 bits.
# The lobby's backfill script moves existing rows onto POL IDs.


def migration_name():
    return "Widening account_vars.accountid for POL IDs"


def check_preconditions(cur):
    return


def needs_to_run(cur):
    cur.execute("SHOW TABLES LIKE 'account_vars'")
    if not cur.fetchone():
        return False
    cur.execute("SHOW COLUMNS FROM account_vars LIKE 'accountid'")
    row = cur.fetchone()
    return row is not None and not row[1].lower().startswith("bigint")


def migrate(cur, db):
    try:
        cur.execute(
            "ALTER TABLE account_vars MODIFY `accountid` bigint(20) unsigned NOT NULL;"
        )
        db.commit()
    except mariadb.Error as err:
        print("Something went wrong: {}".format(err))
