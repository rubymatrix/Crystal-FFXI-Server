import mariadb

# The PlayOnline account a character belongs to, as the numeric POL ID the lobby sends on create and enter.
# 0 until the lobby has sent it.


def migration_name():
    return "Adding pol_id to chars"


def check_preconditions(cur):
    return


def needs_to_run(cur):
    cur.execute("SHOW COLUMNS FROM chars LIKE 'pol_id'")
    if not cur.fetchone():
        return True
    return False


def migrate(cur, db):
    try:
        cur.execute("ALTER TABLE chars \
                ADD COLUMN IF NOT EXISTS `pol_id` bigint(20) unsigned NOT NULL DEFAULT '0' AFTER `original_accid`, \
                ADD KEY IF NOT EXISTS `pol_id` (`pol_id`);")
        db.commit()
    except mariadb.Error as err:
        print("Something went wrong: {}".format(err))
