WITH
    current_gold AS (
        SELECT
            c.code AS country_code,
            COUNT(*) AS gold_cnt
        FROM
            medals m
            JOIN medal_info mi ON m.medal_code = mi.code
            JOIN athletes a ON m.winner_code = a.code
            JOIN countries c ON a.country_code = c.code
        WHERE
            mi.name = 'Gold Medal'
        GROUP BY
            c.code
    ),
    tokyo_gold AS (
        SELECT
            country_code,
            gold_medal AS gold_cnt
        FROM
            tokyo_medals
    ),
    gold_diff AS (
        SELECT
            c.code AS country_code,
            COALESCE(g.gold_cnt, 0) - COALESCE(t.gold_cnt, 0) AS increased_gold
        FROM
            countries c
            LEFT JOIN current_gold g ON c.code = g.country_code
            LEFT JOIN tokyo_gold t ON c.code = t.country_code
    ),
    top5 AS (
        SELECT
            *
        FROM
            gold_diff
        ORDER BY
            increased_gold DESC,
            country_code ASC
        LIMIT
            5
    ),
    team_members AS (
        SELECT
            te.code AS team_code,
            te.country_code,
            a.code AS athlete_code,
            a.gender
        FROM
            teams te
            JOIN athletes a ON te.athletes_code = a.code
    ),
    female_teams AS (
        SELECT
            tm.team_code,
            tm.country_code
        FROM
            team_members tm
        GROUP BY
            tm.team_code,
            tm.country_code
        HAVING
            COUNT(*) = SUM(
                CASE
                    WHEN tm.gender = 1 THEN 1
                    ELSE 0
                END
            )
    )
SELECT
    t.country_code AS COUNTRY_CODE,
    t.increased_gold AS INCREASED_GOLD_MEDAL_NUMBER,
    ft.team_code AS TEAM_CODE
FROM
    top5 t
    JOIN female_teams ft ON t.country_code = ft.country_code
ORDER BY
    INCREASED_GOLD_MEDAL_NUMBER DESC,
    COUNTRY_CODE ASC,
    TEAM_CODE ASC;