import mariadb

# The lobby's account service read sessions by this token; the map reads chars.pol_id instead now.


def migration_name():
    return "Dropping lobby_token from accounts_sessions"


def check_preconditions(cur):
    return


def needs_to_run(cur):
    cur.execute("SHOW COLUMNS FROM accounts_sessions LIKE 'lobby_token'")
    if cur.fetchone():
        return True
    return False


def migrate(cur, db):
    try:
        cur.execute(
            "ALTER TABLE accounts_sessions DROP COLUMN IF EXISTS `lobby_token`;"
        )
        db.commit()
    except mariadb.Error as err:
        print("Something went wrong: {}".format(err))
