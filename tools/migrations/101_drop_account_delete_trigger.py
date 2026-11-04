import mariadb

# chars.accid holds a PlayOnline content id on this server, not an accounts.id, so
# this trigger would delete whichever character sits on a content id that matches a
# deleted accounts row.


def migration_name():
    return "Dropping the account_delete trigger"


def check_preconditions(cur):
    return


def needs_to_run(cur):
    cur.execute("SHOW TRIGGERS WHERE `Trigger` = 'account_delete'")
    if cur.fetchone():
        return True
    return False


def migrate(cur, db):
    try:
        cur.execute("DROP TRIGGER IF EXISTS account_delete;")
        db.commit()
    except mariadb.Error as err:
        print("Something went wrong: {}".format(err))
