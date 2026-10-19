-- PlayOnline accounts banned from this world, by the numeric POL ID the lobby sends (chars.pol_id).
-- The lobby API refuses to create or admit a character of a banned account. Set with !banaccount.

DROP TABLE IF EXISTS `pol_bans`;
CREATE TABLE IF NOT EXISTS `pol_bans` (
  `pol_id` bigint(20) unsigned NOT NULL,
  `banned_until` int(10) unsigned NOT NULL DEFAULT 0, -- unix time; 0 until lifted
  `reason` varchar(128) NOT NULL DEFAULT '',
  `banned_by` varchar(15) NOT NULL DEFAULT '',
  `created` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`pol_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
