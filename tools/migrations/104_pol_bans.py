import mariadb

# PlayOnline accounts banned from this world (sql/pol_bans.sql), for databases made before it existed.


def migration_name():
    return "Adding pol_bans"


def check_preconditions(cur):
    return


def needs_to_run(cur):
    cur.execute("SHOW TABLES LIKE 'pol_bans'")
    if not cur.fetchone():
        return True
    return False


def migrate(cur, db):
    try:
        cur.execute("CREATE TABLE IF NOT EXISTS `pol_bans` ( \
                `pol_id` bigint(20) unsigned NOT NULL, \
                `banned_until` int(10) unsigned NOT NULL DEFAULT 0, \
                `reason` varchar(128) NOT NULL DEFAULT '', \
                `banned_by` varchar(15) NOT NULL DEFAULT '', \
                `created` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP, \
                PRIMARY KEY (`pol_id`) \
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;")
        db.commit()
    except mariadb.Error as err:
        print("Something went wrong: {}".format(err))
