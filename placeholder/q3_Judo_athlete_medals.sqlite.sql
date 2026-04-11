SELECT
    a.name AS ATHLETE_NAME,
    COALESCE(w.cnt, 0) AS MEDAL_NUMBER
FROM
    (
        SELECT
            athletes_code AS code
        FROM
            teams
        WHERE
            discipline = 'Judo'
        UNION
        SELECT
            code
        FROM
            athletes
        WHERE
            disciplines = 'Judo'
    ) AS judo
    JOIN athletes AS a ON judo.code = a.code
    LEFT JOIN (
        SELECT
            code,
            SUM(cnt) as cnt
        FROM
            (
                SELECT
                    t.athletes_code AS code,
                    COUNT(*) AS cnt
                FROM
                    medals AS m
                    JOIN teams AS t ON m.winner_code = t.code
                GROUP BY
                    t.athletes_code
                UNION ALL
                SELECT
                    a.code,
                    COUNT(*) AS cnt
                FROM
                    medals AS m
                    JOIN athletes AS a ON m.winner_code = a.code
                GROUP BY
                    a.code
            )
        GROUP BY
            code
    ) AS w ON w.code = judo.code
ORDER BY
    MEDAL_NUMBER DESC,
    ATHLETE_NAME ASC;